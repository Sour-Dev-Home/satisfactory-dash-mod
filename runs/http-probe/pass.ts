// The HTTP probe (Plan 2B Task 1): S2, S3, User-Agent, and S1/S9 against an untrusted certificate. Gate A.
// Backend TLS minimum, from the dash session (2026-10-20): api.satis-manager.com accepts TLS 1.2 and 1.3 and refuses 1.1
// (alert 70, measured with OpenSSL 3.5.6); set at Cloudflare's edge (Minimum TLS Version, inferred from the handshakes).
// Owner decisions after Gate A (2026-10-20), amending S2 and S3:
// - S3: a redirected answer is refused, and neither the Bearer header nor any request body reaches another address.
//   libcurl follows a 302 as a body-less GET without Authorization, so the redirect target may be contacted, but only
//   like that; the refusal is proven by a snapshot re-sent after the redirects began.
// - S2: the mod relies on the backend's floor (self-hosted backends default to 1.2+, satisfactory-dash #493). It must
//   offer TLS 1.3 and 1.2; older versions (UE's libcurl offers 1.0-1.3) are recorded, not failed. A self-hosted backend
//   that still accepts old TLS is an accepted risk for the security review (Plan 2B Task 10).
import type { CoreEvidence } from "../../harness/src/core-run";
import type { ProbeRequest } from "../../harness/src/probe-stub";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["S1", "S2", "S3", "S9", "user-agent"];
export const USER_AGENT = "satisfactory-dash-mod/0.1.0";
const TLS13 = 0x0304;
const TLS12 = 0x0303;
const tlsName = (v: number) => (v === 0x0302 ? "1.1" : v === 0x0301 ? "1.0" : `0x${v.toString(16)}`);

interface Probe {
  redirectingSince: string | null;
  requests: ProbeRequest[];
  /** supportedVersions is null when the hello has no supported_versions extension (probe-stub.ts parseClientHello). */
  hellos: { supportedVersions: number[] | null }[];
  untrusted: { url: string; handshakeFailures: number; requests: number };
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

  // S3 (amended): the redirect target may be contacted (libcurl follows as a body-less GET), but never with the Bearer
  // header or a body.
  const withAuth = elsewhere.filter((r) => r.authorization).length;
  const withBody = elsewhere.filter((r) => r.bodyBytes > 0).length;
  if (withAuth) reasons.push(`S3: ${withAuth} request(s) at the redirect target carried an Authorization header`);
  if (withBody) reasons.push(`S3: ${withBody} request(s) at the redirect target carried a body`);
  const clean = elsewhere.length - elsewhere.filter((r) => r.authorization || r.bodyBytes > 0).length;
  if (since === null) reasons.push("S3: the redirects never began, so no redirected answer was tested");
  else {
    // A refused (redirected) answer leaves the snapshot queued, so the next push repeats its observedAt.
    const after = atBackend.filter((r) => r.path === "/agent/v1/snapshots" && r.at >= since && r.observedAt !== null).map((r) => r.observedAt);
    if (new Set(after).size === after.length) reasons.push("S3: no snapshot was re-sent after the redirects began, so a redirected answer may have been accepted");
    if (!e.lines.some((l) => /The backend redirected a request; its answer was refused/.test(l))) reasons.push("S9: no line saying a redirected answer was refused");
  }

  let older: number[] = [];
  if (!p.hellos.length) reasons.push("S2: no ClientHello arrived");
  else if (p.hellos.some((h) => h.supportedVersions === null)) {
    // Without the extension the hello names only the client's maximum version.
    reasons.push("S2: the ClientHello has no supported_versions, so its floor is unproven");
  } else {
    if (p.hellos.some((h) => !h.supportedVersions!.includes(TLS13) || !h.supportedVersions!.includes(TLS12))) reasons.push("S2: the mod doesn't offer TLS 1.3 and 1.2");
    older = [...new Set(p.hellos.flatMap((h) => h.supportedVersions!).filter((v) => v < TLS12))].sort((a, b) => b - a);
  }

  if (!p.untrusted.handshakeFailures) reasons.push("S1: the untrusted certificate's handshake never failed");
  if (p.untrusted.requests) reasons.push("S1: data arrived over a certificate nobody trusts: verification is off");
  // The line must name the untrusted backend's origin: the hello phase's failure (another origin) doesn't count.
  if (!e.lines.some((l) => l.includes(`Couldn't reach the backend at ${p.untrusted.url} (`))) reasons.push("S9: no line for the failed TLS connection");

  return reasons.length
    ? { pass: false, reasons }
    : {
        pass: true,
        reasons: [
          `User-Agent kept; ${clean} request(s) reached the redirect target without Authorization or a body; TLS 1.3 and 1.2 offered` +
            `${older.length ? `, also offers TLS ${older.map(tlsName).join(", ")} (the backend refuses them)` : ""}; an untrusted certificate refused`,
        ],
      };
}
