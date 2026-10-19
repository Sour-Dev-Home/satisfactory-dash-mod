import { createServer } from "node:http";
import type { AddressInfo } from "node:net";

/** The fixed loopback port the spike mods post to (the spike contract in the plan's Global Constraints). */
export const ECHO_PORT = 18080;
export const ECHO_MAX_BODY = 1024 * 1024;

export interface CapturedRequest {
  at: string;
  method: string;
  path: string;
  headers: Record<string, string | string[] | undefined>;
  body: string;
  tooLarge: boolean;
}
export interface Echo { url: string; port: number; requests: CapturedRequest[]; close(): Promise<void> }

export function startEcho(port = ECHO_PORT, now: () => Date = () => new Date()): Promise<Echo> {
  const requests: CapturedRequest[] = [];
  const server = createServer((req, res) => {
    const chunks: Buffer[] = [];
    let size = 0;
    let tooLarge = false;
    req.on("data", (chunk: Buffer) => {
      size += chunk.length;
      if (size > ECHO_MAX_BODY) tooLarge = true;
      else chunks.push(chunk);
    });
    req.on("end", () => {
      requests.push({
        at: now().toISOString(),
        method: req.method ?? "",
        path: req.url ?? "",
        headers: { ...req.headers },
        body: tooLarge ? "" : Buffer.concat(chunks).toString("utf8"),
        tooLarge,
      });
      res.writeHead(tooLarge ? 413 : 200, { "content-type": "application/json" });
      res.end(tooLarge ? '{"ok":false}' : '{"ok":true}');
    });
  });
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(port, "127.0.0.1", () => {
      const bound = (server.address() as AddressInfo).port;
      resolve({
        url: `http://127.0.0.1:${bound}`,
        port: bound,
        requests,
        close: () =>
          new Promise<void>((done) => {
            server.closeAllConnections();
            server.close(() => done());
          }),
      });
    });
  });
}
