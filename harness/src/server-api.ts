import { request } from "node:https";
import { LOOPBACK_HOSTS } from "./config";

export type Transport = (url: URL, body: string) => Promise<{ status: number; text: string }>;

/**
 * POSTs JSON over HTTPS. The dedicated server's certificate is self-signed by default (dedicated-server-api.md, E3), so
 * verification is off. That is acceptable only because createServerApi allows loopback hosts alone.
 */
export const httpsLoopback: Transport = (url, body) =>
  new Promise((resolve, reject) => {
    const req = request(
      url,
      {
        method: "POST",
        rejectUnauthorized: false,
        timeout: 10_000,
        headers: { "content-type": "application/json", "content-length": Buffer.byteLength(body) },
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

/** The dedicated server HTTPS API: POST /api/v1 with {"function": name} (captured QueryServerState requests), plus
 *  {"data": …} for the functions that take it (ApplyServerOptions). */
export function createServerApi(baseUrl: string, transport: Transport = httpsLoopback): ServerApi {
  const url = new URL("/api/v1", baseUrl);
  if (url.protocol !== "https:" || !LOOPBACK_HOSTS.has(url.hostname)) {
    throw new Error(`refusing a non-loopback server API: ${baseUrl}`);
  }
  return {
    async call(fn, data) {
      const { status, text } = await transport(url, JSON.stringify(data ? { function: fn, data } : { function: fn }));
      if (!text) return { status, body: null };
      try {
        return { status, body: JSON.parse(text) as unknown };
      } catch {
        return { status, body: text };
      }
    },
  };
}
