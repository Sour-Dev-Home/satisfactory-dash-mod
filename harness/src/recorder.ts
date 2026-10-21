// The recorder (ruling 11): a loopback proxy between an agent and the backend host's relay. It forwards every request
// unchanged and keeps a public record of what passed: never the Authorization value, an enrolment body (the code) or
// its answer (the secret), and the session name only as its hash (bundles are public).
import { createServer, type IncomingHttpHeaders, type IncomingMessage, request, type ServerResponse } from "node:http";
import type { AddressInfo } from "node:net";
import { gunzipSync } from "node:zlib";
import { sessionHash } from "./status";

export interface RecordedCall {
  /** When the request arrived. */
  at: string;
  /** When the answer had finished being sent (or the exchange failed): the earliest the agent could know it. */
  doneAt: string;
  /** By User-Agent: the mod, satisfactory-dash's Node agent (parity run), or anything else. */
  agent: "mod" | "node" | "other";
  method: string;
  /** The path without its query. */
  path: string;
  /** 0 when the upstream couldn't be reached (the agent's socket was destroyed, as in a real outage). */
  status: number;
  authorization: boolean;
  snapshot?: unknown;
  commandIds?: string[];
  result?: unknown;
}
export interface Recorder { url: string; entries: RecordedCall[]; close(): Promise<void> }

const MAX_RECORDED = 2 * 1024 * 1024;
const SNAPSHOTS = "/agent/v1/snapshots";
const COMMANDS = "/agent/v1/commands";
const RESULT = /^\/agent\/v1\/commands\/[^/]+\/result$/;

/** Keeps at most MAX_RECORDED bytes of a stream that is forwarded whole; over that, nothing is recorded. */
class Capped {
  private chunks: Buffer[] = [];
  private size = 0;
  over = false;
  add(chunk: Buffer) {
    if (this.over) return;
    this.size += chunk.length;
    if (this.size > MAX_RECORDED) {
      this.over = true;
      this.chunks = [];
    } else {
      this.chunks.push(chunk);
    }
  }
  /** The body as JSON (gunzipped if it says gzip), or undefined when it was too big or isn't JSON. */
  json(headers: IncomingHttpHeaders): unknown {
    if (this.over || !this.size) return undefined;
    try {
      let raw = Buffer.concat(this.chunks);
      if (String(headers["content-encoding"] ?? "").toLowerCase() === "gzip") raw = gunzipSync(raw, { maxOutputLength: MAX_RECORDED });
      return JSON.parse(raw.toString("utf8"));
    } catch {
      return undefined;
    }
  }
}

function hashSession(snapshot: unknown): unknown {
  const status = (snapshot as { status?: Record<string, unknown> } | null)?.status;
  if (!status || typeof status !== "object" || typeof status.sessionName !== "string") return snapshot;
  return { ...(snapshot as object), status: { ...status, sessionName: sessionHash(status.sessionName) } };
}

function commandIds(body: unknown): string[] | undefined {
  const list = (body as { commands?: unknown } | null)?.commands;
  if (!Array.isArray(list)) return undefined;
  return list.map((c) => (c as { id?: unknown } | null)?.id).filter((id): id is string => typeof id === "string");
}

/** Starts the recorder on 127.0.0.1 in front of upstream (the relay's URL). */
export async function startRecorder(upstream: string): Promise<Recorder> {
  const target = new URL(upstream);
  const entries: RecordedCall[] = [];

  const handle = (req: IncomingMessage, res: ServerResponse) => {
    const path = new URL(req.url ?? "/", "http://127.0.0.1").pathname;
    const method = req.method ?? "GET";
    const ua = String(req.headers["user-agent"] ?? "");
    const entry: RecordedCall = {
      at: new Date().toISOString(),
      doneAt: "",
      // satisfactory-dash agent/src/backendClient.ts sends satisfactory-dash-agent/<version>.
      agent: ua.startsWith("satisfactory-dash-mod/") ? "mod" : ua.startsWith("satisfactory-dash-agent/") ? "node" : "other",
      method,
      path,
      status: 0,
      authorization: req.headers.authorization !== undefined,
    };
    const sent = new Capped();
    const got = new Capped();
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      entry.doneAt = new Date().toISOString();
      if (entry.status !== 0) {
        if (method === "POST" && path === SNAPSHOTS) {
          const s = sent.json(req.headers);
          if (s !== undefined) entry.snapshot = hashSession(s);
        } else if (method === "GET" && path === COMMANDS) {
          const ids = commandIds(got.json(upRes?.headers ?? {}));
          if (ids) entry.commandIds = ids;
        } else if (method === "POST" && RESULT.test(path)) {
          const r = sent.json(req.headers);
          if (r !== undefined) entry.result = r;
        }
      }
      entries.push(entry);
    };

    let upRes: IncomingMessage | undefined;
    const up = request(
      { protocol: target.protocol, hostname: target.hostname, port: target.port, method, path: req.url, headers: { ...req.headers, host: target.host } },
      (r) => {
        upRes = r;
        entry.status = r.statusCode ?? 0;
        res.writeHead(entry.status, r.headers);
        r.on("data", (c: Buffer) => got.add(c));
        r.on("end", finish);
        r.on("error", () => (res.destroy(), finish()));
        r.pipe(res);
      },
    );
    up.on("error", () => {
      // No answer at all, as the mod would see a real outage: no status line, the socket just goes.
      entry.status = upRes ? entry.status : 0;
      req.socket.destroy();
      res.destroy();
      finish();
    });
    req.on("data", (c: Buffer) => sent.add(c));
    req.pipe(up);
  };

  const server = createServer(handle);
  await new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => resolve());
  });
  return {
    url: `http://127.0.0.1:${(server.address() as AddressInfo).port}`,
    entries,
    close: () =>
      new Promise<void>((resolve) => {
        server.closeAllConnections();
        server.close(() => resolve());
      }),
  };
}
