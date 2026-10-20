import { randomBytes } from "node:crypto";
import { request } from "node:https";
import { LOOPBACK_HOSTS } from "./config";

export type Transport = (url: URL, body: string | Buffer, contentType?: string) => Promise<{ status: number; text: string }>;

/**
 * POSTs JSON over HTTPS. The dedicated server's certificate is self-signed by default (dedicated-server-api.md, E3), so
 * verification is off. That is acceptable only because createServerApi allows loopback hosts alone.
 */
export const httpsLoopback: Transport = (url, body, contentType = "application/json") =>
  new Promise((resolve, reject) => {
    const req = request(
      url,
      {
        method: "POST",
        rejectUnauthorized: false,
        // A save upload is a few MB; everything else is a small JSON body.
        timeout: typeof body === "string" ? 10_000 : 120_000,
        headers: { "content-type": contentType, "content-length": Buffer.byteLength(body) },
      },
      (res) => {
        let text = "";
        res.setEncoding("utf8");
        res.on("data", (chunk: string) => (text += chunk));
        res.on("end", () => resolve({ status: res.statusCode ?? 0, text }));
      },
    );
    req.on("timeout", () => req.destroy(new Error("request timed out")));
    req.on("error", reject);
    req.end(body);
  });

export interface ServerApi { call(fn: string, data?: Record<string, unknown>): Promise<{ status: number; body: unknown }> }
export interface UploadingServerApi extends ServerApi {
  /** Multipart: a "data" part holding the usual envelope, and one file part (UploadSaveGame, saves capture E4). */
  upload(fn: string, data: Record<string, unknown>, file: { field: string; name: string; bytes: Buffer }): Promise<{ status: number; body: unknown }>;
}

const parse = (status: number, text: string) => {
  if (!text) return { status, body: null };
  try {
    return { status, body: JSON.parse(text) as unknown };
  } catch {
    return { status, body: text };
  }
};

/** The dedicated server HTTPS API: POST /api/v1 with {"function": name} (captured QueryServerState requests), plus
 *  {"data": …} for the functions that take it (ApplyServerOptions). */
export function createServerApi(baseUrl: string, transport: Transport = httpsLoopback): UploadingServerApi {
  const url = new URL("/api/v1", baseUrl);
  if (url.protocol !== "https:" || !LOOPBACK_HOSTS.has(url.hostname)) {
    throw new Error(`refusing a non-loopback server API: ${baseUrl}`);
  }
  return {
    async call(fn, data) {
      const { status, text } = await transport(url, JSON.stringify(data ? { function: fn, data } : { function: fn }));
      return parse(status, text);
    },
    async upload(fn, data, file) {
      const boundary = `satis-harness-${randomBytes(12).toString("hex")}`;
      const head = (disposition: string, type: string) =>
        `--${boundary}\r\nContent-Disposition: form-data; ${disposition}\r\nContent-Type: ${type}\r\n\r\n`;
      const body = Buffer.concat([
        Buffer.from(`${head('name="data"', "application/json")}${JSON.stringify({ function: fn, data })}\r\n`, "utf8"),
        Buffer.from(head(`name="${file.field}"; filename="${file.name}"`, "application/octet-stream"), "utf8"),
        file.bytes,
        Buffer.from(`\r\n--${boundary}--\r\n`, "utf8"),
      ]);
      const { status, text } = await transport(url, body, `multipart/form-data; boundary=${boundary}`);
      return parse(status, text);
    },
  };
}
