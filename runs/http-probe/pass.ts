// The HTTP probe (Plan 2B Task 1): S2, S3, User-Agent, and S1/S9 against an untrusted certificate. Gate A.
// Backend TLS minimum, from the dash session: <pending>
import type { CoreEvidence } from "../../harness/src/core-run";
import type { ProbeRequest } from "../../harness/src/probe-stub";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["S1", "S2", "S3", "S9", "user-agent"];
export const USER_AGENT = "satisfactory-dash-mod/0.1.0";
const TLS12 = 0x0303;

interface Probe {
  redirectingSince: string | null;
  requests: ProbeRequest[];
  hellos: { supportedVersions: number[] }[];
  untrusted: { handshakeFailures: number; requests: number };
}

export function evaluate(e: CoreEvidence): Verdict {
  const p = e.captures["probe.json"] as Probe | undefined;
  if (!p) return { pass: false, reasons: ["no probe capture"] };
  const reasons: string[] = [];
  const atBackend = p.requests.filter((r) => r.port === "backend");
  const elsewhere = p.requests.filter((r) => r.port === "elsewhere");

  const wrongUa = atBackend.filter((r) => r.userAgent !== USER_AGENT);
  if (!atBackend.length) reasons.push("the stub received nothing");
  if (wrongUa.length) reasons.push(`User-Agent: expected ${USER_AGENT}, got ${JSON.stringify(wrongUa[0]!.userAgent)}`);

  // Before the redirects, each answered snapshot is a new one: a refused (re-sent) answer would repeat its observedAt.
  const since = p.redirectingSince;
  const normal = atBackend.filter((r) => r.path === "/agent/v1/snapshots" && (since === null || r.at < since)).slice(0, 3);
  if (normal.length < 3) reasons.push(`only ${normal.length} snapshots before the redirects began`);
  else if (new Set(normal.map((r) => r.observedAt)).size !== 3) reasons.push("a snapshot answered 200 by the URL asked was sent again: the transport refused a direct answer");

  if (elsewhere.length) {
    const withAuth = elsewhere.filter((r) => r.authorization).length;
    reasons.push(`S3: the redirect target received ${elsewhere.length} request(s), ${withAuth} with an Authorization header`);
  }
  if (since !== null && !e.lines.some((l) => /The backend redirected a request; its answer was refused/.test(l))) {
    reasons.push("S9: no line saying a redirected answer was refused");
  }

  if (!p.hellos.length) reasons.push("S2: no ClientHello arrived");
  else {
    const lowest = Math.min(...p.hellos.flatMap((h) => h.supportedVersions));
    if (lowest < TLS12) reasons.push(`S2: the mod offers TLS below 1.2 (0x${lowest.toString(16)})`);
  }

  if (!p.untrusted.handshakeFailures) reasons.push("S1: the untrusted certificate's handshake never failed");
  if (p.untrusted.requests) reasons.push("S1: data arrived over a certificate nobody trusts: verification is off");
  if (!e.lines.some((l) => /Couldn't reach the backend \(/.test(l))) reasons.push("S9: no line for the failed TLS connection");

  return reasons.length
    ? { pass: false, reasons }
    : { pass: true, reasons: ["User-Agent kept; redirect target contacted by nobody; TLS 1.2 or newer offered; an untrusted certificate refused"] };
}
