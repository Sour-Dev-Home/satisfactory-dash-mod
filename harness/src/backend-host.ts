// The backend host (ruling 10): the owner runs the throwaway backend in their own session (Docker is theirs), and the
// runner's account drives it through a few fixed operations on loopback, authenticated by a token in a host file only
// the owner and the runner can read. No SQL and no shell ever come from the caller.
import { randomBytes, timingSafeEqual } from "node:crypto";
import { readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import type { AddressInfo } from "node:net";
import { z } from "zod";
import { type Backend, type BackendImages, UUID } from "./backend";
import { LOOPBACK_HOSTS } from "./config";

export type ServerKey = "mod" | "node";
export interface HostState { codeConsumed: boolean; agentVersion: string | null; lastSeen: boolean; revoked: boolean }
export interface HostClient {
  relayUrl: string;
  seedCode(server: ServerKey, code: string): Promise<void>;
  revoke(server: ServerKey): Promise<void>;
  queueCommand(server: ServerKey, params: Record<string, unknown>): Promise<string>;
  command(id: string): Promise<{ status: string; resultCode: string | null }>;
  state(server: ServerKey): Promise<HostState>;
  stopRelay(): Promise<void>;
  startRelay(): Promise<void>;
  /** The images the stack runs and their revisions (startBackend's image guard), for the run's evidence. */
  images(): Promise<BackendImages>;
}
export interface BackendHost { controlUrl: string; relayUrl: string; close(): Promise<void> }

const MAX_BODY = 4096;
const Server = z.enum(["mod", "node"]);
const Code = z.string().regex(/^[A-Z2-7]{4}-[A-Z2-7]{4}-[A-Z2-7]{4}$/);
const SeedBody = z.strictObject({ server: Server, code: Code });
const ServerBody = z.strictObject({ server: Server });
const CommandBody = z.strictObject({ server: Server, params: z.strictObject({ enabled: z.unknown() }) });

const loopbackHttp = (v: string) => {
  try {
    const u = new URL(v);
    return u.protocol === "http:" && LOOPBACK_HOSTS.has(u.hostname);
  } catch {
    return false;
  }
};
const HostFileSchema = z.strictObject({
  relayUrl: z.string().refine(loopbackHttp, "relayUrl must be http on loopback"),
  controlUrl: z.string().refine(loopbackHttp, "controlUrl must be http on loopback"),
  token: z.string().regex(/^[0-9a-f]{64}$/),
});
export type HostFile = z.infer<typeof HostFileSchema>;

class HttpError extends Error {
  constructor(readonly status: number) {
    super(String(status));
  }
}

async function readBody(req: IncomingMessage): Promise<unknown> {
  const chunks: Buffer[] = [];
  let size = 0;
  for await (const chunk of req) {
    size += (chunk as Buffer).length;
    if (size > MAX_BODY) throw new HttpError(413);
    chunks.push(chunk as Buffer);
  }
  if (!size) return undefined;
  try {
    return JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    throw new HttpError(400);
  }
}

function parse<T>(schema: z.ZodType<T>, body: unknown): T {
  const r = schema.safeParse(body);
  if (!r.success) throw new HttpError(400);
  return r.data;
}

/** Serves the backend's fixed operations on 127.0.0.1:<port> and writes the host file; close() runs down -v and deletes it. */
export async function serveBackendHost(opts: { backend: Backend; file: string; port: number }): Promise<BackendHost> {
  const { backend } = opts;
  const token = randomBytes(32).toString("hex");
  const expected = Buffer.from(`Bearer ${token}`, "utf8");
  const authorised = (req: IncomingMessage) => {
    const got = Buffer.from(req.headers.authorization ?? "", "utf8");
    return got.length === expected.length && timingSafeEqual(got, expected);
  };

  /** The route's answer: undefined is 204, anything else is JSON. Throws HttpError(404) for anything not listed. */
  const route = async (req: IncomingMessage): Promise<unknown> => {
    const path = new URL(req.url ?? "/", "http://127.0.0.1").pathname;
    const method = req.method ?? "";
    const post = (p: string) => method === "POST" && path === p;
    if (post("/seed")) {
      const b = parse(SeedBody, await readBody(req));
      return void (await backend.seedCode(b.server, b.code));
    }
    if (post("/revoke")) return void (await backend.revoke(parse(ServerBody, await readBody(req)).server));
    if (post("/command")) {
      const b = parse(CommandBody, await readBody(req));
      return { id: await backend.queueCommand(b.server, b.params as Record<string, unknown>) };
    }
    if (post("/relay/stop")) return void (await backend.stopRelay());
    if (post("/relay/start")) return void (await backend.startRelay());
    if (method === "GET") {
      if (path === "/images") return backend.images;
      const command = /^\/command\/([^/]+)$/.exec(path)?.[1];
      if (command !== undefined && UUID.test(command)) return await backend.command(command);
      const server = /^\/state\/([^/]+)$/.exec(path)?.[1];
      const key = Server.safeParse(server);
      if (key.success) return await backend.state(key.data);
    }
    throw new HttpError(404);
  };

  const handle = async (req: IncomingMessage, res: ServerResponse) => {
    let status = 200;
    let body: unknown;
    if (!authorised(req)) {
      status = 401;
    } else {
      try {
        body = await route(req);
        if (body === undefined) status = 204;
      } catch (e) {
        status = e instanceof HttpError ? e.status : 500;
        body = undefined;
        if (!(e instanceof HttpError)) console.error(`backend host: ${req.method} ${req.url} failed: ${e instanceof Error ? e.message : String(e)}`);
      }
    }
    req.resume();
    if (body === undefined) res.writeHead(status).end();
    else res.writeHead(status, { "content-type": "application/json" }).end(JSON.stringify(body));
  };

  const server = createServer((req, res) => void handle(req, res));
  await new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(opts.port, "127.0.0.1", () => resolve());
  });
  const controlUrl = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
  const hostFile: HostFile = { relayUrl: backend.url, controlUrl, token };
  writeFileSync(opts.file, JSON.stringify(hostFile, null, 2), { mode: 0o600 });

  let closed = false;
  return {
    controlUrl,
    relayUrl: backend.url,
    async close() {
      if (closed) return;
      closed = true;
      rmSync(opts.file, { force: true });
      server.closeAllConnections();
      await new Promise<void>((resolve) => server.close(() => resolve()));
      await backend.down();
    },
  };
}

/**
 * The CLI's lifecycle: the stop handler is registered before the stack starts and stays registered, so a Ctrl+C while
 * compose comes up still ends in down -v (once it is up), and a second Ctrl+C during the close is ignored rather than
 * killing it midway.
 */
export async function hostUntilStopped(opts: {
  start(): Promise<Backend>;
  serve(backend: Backend): Promise<BackendHost>;
  onStop(handler: () => void): void;
  log(s: string): void;
}): Promise<void> {
  let stopRequested = false;
  let wake: () => void = () => undefined;
  const stopped = new Promise<void>((resolve) => (wake = resolve));
  opts.onStop(() => {
    if (!stopRequested) opts.log("stopping: the stack goes down (down -v) as soon as it can; further Ctrl+C is ignored");
    stopRequested = true;
    wake();
  });
  const backend = await opts.start(); // a failed up already ran down -v (startBackend)
  if (stopRequested) {
    await backend.down();
    return;
  }
  let host: BackendHost;
  try {
    host = await opts.serve(backend);
  } catch (e) {
    await backend.down();
    throw e;
  }
  opts.log(`backend host up: relay ${host.relayUrl}; Ctrl+C to stop`);
  await stopped;
  await host.close(); // down -v, and the host file is deleted
  opts.log("backend host stopped; the throwaway database is gone");
}

/** Reads the host file and returns a client for its fixed operations; every call carries the token. */
export async function connectBackendHost(file: string): Promise<HostClient> {
  const host = HostFileSchema.parse(JSON.parse(readFileSync(file, "utf8")));
  const call = async (method: "GET" | "POST", path: string, body?: unknown): Promise<unknown> => {
    const res = await fetch(new URL(path, host.controlUrl), {
      method,
      headers: { authorization: `Bearer ${host.token}`, ...(body === undefined ? {} : { "content-type": "application/json" }) },
      ...(body === undefined ? {} : { body: JSON.stringify(body) }),
      signal: AbortSignal.timeout(10_000),
    });
    if (!res.ok) throw new Error(`backend host: ${method} ${path} answered ${res.status}`);
    return res.status === 204 ? undefined : await res.json();
  };
  return {
    relayUrl: host.relayUrl,
    seedCode: async (server, code) => void (await call("POST", "/seed", { server, code })),
    revoke: async (server) => void (await call("POST", "/revoke", { server })),
    queueCommand: async (server, params) => z.strictObject({ id: z.string() }).parse(await call("POST", "/command", { server, params })).id,
    command: async (id) => {
      if (!UUID.test(id)) throw new Error("a command id must be a UUID");
      return z.strictObject({ status: z.string(), resultCode: z.string().nullable() }).parse(await call("GET", `/command/${id}`));
    },
    state: async (server) =>
      z.strictObject({ codeConsumed: z.boolean(), agentVersion: z.string().nullable(), lastSeen: z.boolean(), revoked: z.boolean() })
        .parse(await call("GET", `/state/${server}`)),
    stopRelay: async () => void (await call("POST", "/relay/stop")),
    startRelay: async () => void (await call("POST", "/relay/start")),
    images: async () => {
      const image = z.strictObject({ image: z.string().min(1), revision: z.string().regex(/^[0-9a-f]{40}$/) });
      return z.strictObject({ app: image, ops: image }).parse(await call("GET", "/images"));
    },
  };
}
