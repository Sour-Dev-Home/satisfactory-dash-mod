// S1 (claim C2): one HTTP POST of a fixed JSON to the loopback echo server at startup.
import { isDeepStrictEqual } from "node:util";
import type { Evidence, Verdict } from "../../harness/src/verdict";

export const claim = "C2";
export const S1_PATH = "/s1";
export const S1_BODY = { spike: "S1", hello: "satis" };

export function evaluate(e: Evidence): Verdict {
  const posts = e.echo.filter((r) => r.method === "POST" && r.path === S1_PATH);
  if (posts.length === 0) return { pass: false, reasons: [`the echo server got no POST to ${S1_PATH}`] };
  const matches = posts.some((r) => {
    if (!String(r.headers["content-type"] ?? "").startsWith("application/json")) return false;
    try {
      return isDeepStrictEqual(JSON.parse(r.body), S1_BODY);
    } catch {
      return false;
    }
  });
  return matches
    ? { pass: true, reasons: [`a POST to ${S1_PATH} carried the expected JSON body and content type`] }
    : { pass: false, reasons: [`a POST reached ${S1_PATH}, but its body or content type was wrong`] };
}
