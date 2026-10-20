import { execFile } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { createServer as createHttp, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import { type AddressInfo, createServer as createTcp, type Server as TcpServer } from "node:net";
import { delimiter, join } from "node:path";
import { createServer as createTls } from "node:tls";

/** The contract's secret shape (43 base64url characters). A fixed fake: never a real credential. */
export const FAKE_SECRET = `${"a".repeat(42)}_`;
const CADENCE = { statusSeconds: 5, powerSeconds: 5, factorySeconds: 30 };
const MAX_BODY = 1024 * 1024;

export interface ProbeRequest {
  at: string;
  port: "backend" | "elsewhere";
  method: string;
  path: string;
  userAgent: string | null;
  /** Whether an Authorization header arrived; its value is never kept. */
  authorization: boolean;
  bodyBytes: number;
  /** A snapshot body's observedAt, so a re-sent snapshot shows (Task 1's normal phase). */
  observedAt: string | null;
  /** False until the body's end event: a request whose body never came stays false. */
  complete: boolean;
}
export interface ProbeStub {
  backendUrl: string;
  elsewhereUrl: string;
  requests: ProbeRequest[];
  startRedirecting(): void;
  redirectingSince(): string | null;
  close(): Promise<void>;
}

const listen = (server: Server | TcpServer) =>
  new Promise<number>((resolve) => server.listen(0, "127.0.0.1", () => resolve((server.address() as AddressInfo).port)));
const closeServer = (server: Server | TcpServer) => new Promise<void>((resolve) => server.close(() => resolve()));

/** Reads the body, calling onChunk with the running byte count; resolves when the body ends (never, if it never does). */
function readBody(req: IncomingMessage, onChunk: (total: number) => void = () => undefined): Promise<Buffer> {
  return new Promise((resolve) => {
    const chunks: Buffer[] = [];
    let size = 0;
    req.on("data", (c: Buffer) => {
      size += c.length;
      if (size <= MAX_BODY) chunks.push(c);
      onChunk(size);
    });
    req.on("end", () => resolve(Buffer.concat(chunks)));
    req.on("error", () => undefined);
  });
}

function observedAtOf(body: Buffer): string | null {
  try {
    const v = (JSON.parse(body.toString("utf8")) as { observedAt?: unknown }).observedAt;
    return typeof v === "string" ? v : null;
  } catch {
    return null;
  }
}

const json = (res: ServerResponse, status: number, body: unknown) => {
  res.writeHead(status, { "content-type": "application/json; charset=utf-8" });
  res.end(JSON.stringify(body));
};

/** Task 1's stub backend: answers enrol and snapshots, then on cue redirects snapshots to a second port. */
export async function startProbeStub(now: () => Date = () => new Date()): Promise<ProbeStub> {
  const requests: ProbeRequest[] = [];
  let redirecting: string | null = null;
  /** Records the request now (headers are in), then fills in the body as it arrives. Resolves with the body when it ends. */
  const record = async (port: ProbeRequest["port"], req: IncomingMessage): Promise<Buffer> => {
    const entry: ProbeRequest = {
      at: now().toISOString(),
      port,
      method: req.method ?? "",
      path: req.url ?? "",
      userAgent: req.headers["user-agent"] ?? null,
      authorization: req.headers.authorization !== undefined,
      bodyBytes: 0,
      observedAt: null,
      complete: false,
    };
    requests.push(entry);
    if (entry.method === "GET" || entry.method === "HEAD") {
      // libcurl follows a 302 as a GET but keeps the POST's Content-Length, so the body the header promises never comes
      // and waiting for its end stalls the answer. A GET has no body: answer now, and count whatever does arrive.
      req.on("data", (c: Buffer) => void (entry.bodyBytes += c.length));
      req.on("error", () => undefined);
      entry.complete = true;
      return Buffer.alloc(0);
    }
    const body = await readBody(req, (total) => void (entry.bodyBytes = total));
    entry.bodyBytes = body.length;
    entry.observedAt = req.url === "/agent/v1/snapshots" ? observedAtOf(body) : null;
    entry.complete = true;
    return body;
  };

  const elsewhere = createHttp(async (req, res) => {
    await record("elsewhere", req);
    // A valid answer: if the mod accepted a redirected answer, its queue would move on and the next request would show it.
    json(res, 200, { cadence: CADENCE, commandsPending: false });
  });
  const elsewherePort = await listen(elsewhere);
  const elsewhereUrl = `http://127.0.0.1:${elsewherePort}`;

  const backend = createHttp(async (req, res) => {
    await record("backend", req);
    if (req.method === "POST" && req.url === "/agent/v1/enroll") return json(res, 201, { agentSecret: FAKE_SECRET, serverId: "probe", cadence: CADENCE });
    if (req.method === "POST" && req.url === "/agent/v1/snapshots") {
      if (redirecting) {
        res.writeHead(302, { location: `${elsewhereUrl}/agent/v1/snapshots` });
        return res.end();
      }
      return json(res, 200, { cadence: CADENCE, commandsPending: false });
    }
    json(res, 404, { error: "not_found" });
  });
  const backendPort = await listen(backend);

  return {
    backendUrl: `http://127.0.0.1:${backendPort}`,
    elsewhereUrl,
    requests,
    startRedirecting: () => void (redirecting ??= now().toISOString()),
    redirectingSince: () => redirecting,
    close: async () => {
      const done = Promise.all([closeServer(backend), closeServer(elsewhere)]);
      // A request whose body never ends would hold its connection open and stall close().
      backend.closeAllConnections();
      elsewhere.closeAllConnections();
      await done;
    },
  };
}

/**
 * TLS record type 22 (handshake), message type 1 (ClientHello); extension 43 is supported_versions (RFC 8446 4.2.1).
 * Without that extension supportedVersions is null: the legacy version is the client's maximum, so its floor is unknown.
 */
export function parseClientHello(buf: Buffer): { legacyVersion: number; supportedVersions: number[] | null } | null {
  try {
    if (buf[0] !== 0x16 || buf[5] !== 0x01) return null;
    let at = 9;
    const legacyVersion = buf.readUInt16BE(at);
    at += 2 + 32;
    at += 1 + buf[at]!; // session id
    at += 2 + buf.readUInt16BE(at); // cipher suites
    at += 1 + buf[at]!; // compression methods
    if (at >= buf.length) return { legacyVersion, supportedVersions: null };
    const end = at + 2 + buf.readUInt16BE(at);
    if (end > buf.length) return null;
    at += 2;
    while (at + 4 <= end) {
      const type = buf.readUInt16BE(at);
      const len = buf.readUInt16BE(at + 2);
      if (type === 0x002b) {
        const versions: number[] = [];
        for (let i = at + 5; i + 1 < at + 4 + len; i += 2) versions.push(buf.readUInt16BE(i));
        const real = versions.filter((v) => (v & 0x0f0f) !== 0x0a0a); // GREASE (RFC 8701)
        return { legacyVersion, supportedVersions: real };
      }
      at += 4 + len;
    }
    return { legacyVersion, supportedVersions: null };
  } catch {
    return null;
  }
}

export interface HelloCapture { url: string; hellos: { at: string; legacyVersion: number; supportedVersions: number[] | null }[]; close(): Promise<void> }

/** S2: a plain TCP listener that reads the first TLS record, keeps the versions offered, and hangs up. */
export async function startHelloCapture(now: () => Date = () => new Date()): Promise<HelloCapture> {
  const hellos: HelloCapture["hellos"] = [];
  const server = createTcp((socket) => {
    let buf = Buffer.alloc(0);
    socket.on("data", (chunk: Buffer) => {
      buf = Buffer.concat([buf, chunk]);
      const parsed = buf.length >= 5 && buf.length >= 5 + buf.readUInt16BE(3) ? parseClientHello(buf) : null;
      if (parsed) {
        hellos.push({ at: now().toISOString(), ...parsed });
        socket.destroy();
      } else if (buf.length > 16 * 1024) socket.destroy();
    });
    socket.on("error", () => undefined);
  });
  const port = await listen(server);
  return { url: `https://127.0.0.1:${port}`, hellos, close: () => closeServer(server) };
}

/** Git for Windows' standard install locations (not a home folder), in the order tried. */
const GIT_OPENSSL = ["C:\\Program Files\\Git\\usr\\bin\\openssl.exe", "C:\\Program Files\\Git\\mingw64\\bin\\openssl.exe"];

/** Where openssl is: SATIS_OPENSSL, then PATH, then Git for Windows' folders. A service account may lack Git on PATH. */
export function findOpenssl(env: NodeJS.ProcessEnv = process.env, exists: (p: string) => boolean = existsSync): string | null {
  const override = env.SATIS_OPENSSL;
  if (override && exists(override)) return override;
  const name = process.platform === "win32" ? "openssl.exe" : "openssl";
  for (const dir of (env.PATH ?? "").split(delimiter)) {
    if (!dir) continue;
    const candidate = join(dir, name);
    if (exists(candidate)) return candidate;
  }
  if (process.platform === "win32") for (const candidate of GIT_OPENSSL) if (exists(candidate)) return candidate;
  return null;
}

export interface UntrustedTls { url: string; handshakeFailures: number; requests: number; close(): Promise<void> }

/**
 * S1 and S9: TLS with a certificate for another name, issued by nobody the mod trusts. The handshake must fail and no
 * request may arrive. The certificate is made per run with openssl (Git for Windows ships it) into a temp dir.
 */
export async function startUntrustedTls(dir: string, openssl: string): Promise<UntrustedTls> {
  const key = join(dir, "untrusted-key.pem");
  const cert = join(dir, "untrusted-cert.pem");
  await new Promise<void>((resolve, reject) =>
    execFile(
      openssl,
      ["req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", cert, "-days", "1", "-subj", "/CN=wrong.invalid"],
      { env: { ...process.env, MSYS2_ARG_CONV_EXCL: "*" } },
      (err) => (err ? reject(new Error(`openssl failed (${openssl}): ${err.message}`)) : resolve()),
    ),
  );
  const state = { handshakeFailures: 0, requests: 0 };
  const server = createTls({ key: readFileSync(key), cert: readFileSync(cert) }, (socket) => {
    socket.on("data", () => void state.requests++);
    socket.on("error", () => undefined);
  });
  server.on("tlsClientError", () => void state.handshakeFailures++);
  const port = await listen(server);
  return {
    url: `https://127.0.0.1:${port}`,
    get handshakeFailures() { return state.handshakeFailures; },
    get requests() { return state.requests; },
    close: () => closeServer(server),
  };
}
