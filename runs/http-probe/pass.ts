// The HTTP probe (Plan 2B Task 1): S2, S3, User-Agent, and S1/S9 against an untrusted certificate. Gate A.
// Backend TLS minimum, from the dash session (2026-10-20): api.satis-manager.com accepts TLS 1.2 and 1.3 and refuses 1.1
// (alert 70, measured with OpenSSL 3.5.6); set at Cloudflare's edge (Minimum TLS Version, inferred from the handshakes).
// Owner decision after the Gate A re-run (2026-10-20, run 39499979562): UE's curl request always follows redirects
// (CurlHttp.cpp:127) and re-sent the snapshot body to the redirect target, so the mod makes its own libcurl requests
// with redirects off and a TLS 1.2 minimum (Plan 2B Task 1e). That replaces the earlier amendments, which let the
// target see body-less GETs and recorded TLS 1.0/1.1 offers without failing them:
// - S3: the mod never follows a redirect, so the redirect target receives nothing at all; a redirected answer is
//   refused, proven by a snapshot re-sent after the redirects began.
// - S2: the mod sets the floor itself (CURL_SSLVERSION_TLSv1_2): it offers TLS 1.3 and 1.2 and nothing older. The
//   backend's own floor (self-hosted backends default to 1.2+, satisfactory-dash #493) stays as a second line.
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

  // S3: the redirect target receives nothing; what reached it is detailed (the Bearer header, a body).
  const withAuth = elsewhere.filter((r) => r.authorization).length;
  const withBody = elsewhere.filter((r) => r.bodyBytes > 0).length;
  if (elsewhere.length) reasons.push(`S3: the redirect target received ${elsewhere.length} request(s)`);
  if (withAuth) reasons.push(`S3: ${withAuth} request(s) at the redirect target carried an Authorization header`);
  if (withBody) reasons.push(`S3: ${withBody} request(s) at the redirect target carried a body`);
  if (since === null) reasons.push("S3: the redirects never began, so no redirected answer was tested");
  else {
    // A refused (redirected) answer leaves the snapshot queued, so the next push repeats its observedAt.
    const after = atBackend.filter((r) => r.path === "/agent/v1/snapshots" && r.at >= since && r.observedAt !== null).map((r) => r.observedAt);
    if (new Set(after).size === after.length) reasons.push("S3: no snapshot was re-sent after the redirects began, so a redirected answer may have been accepted");
    if (!e.lines.some((l) => /The backend redirected a request; its answer was refused/.test(l))) reasons.push("S9: no line saying a redirected answer was refused");
  }

  if (!p.hellos.length) reasons.push("S2: no ClientHello arrived");
  else if (p.hellos.some((h) => h.supportedVersions === null)) {
    // Without the extension the hello names only the client's maximum version.
    reasons.push("S2: the ClientHello has no supported_versions, so its floor is unproven");
  } else {
    if (p.hellos.some((h) => !h.supportedVersions!.includes(TLS13) || !h.supportedVersions!.includes(TLS12))) reasons.push("S2: the mod doesn't offer TLS 1.3 and 1.2");
    const older = [...new Set(p.hellos.flatMap((h) => h.supportedVersions!).filter((v) => v < TLS12))].sort((a, b) => b - a);
    if (older.length) reasons.push(`S2: the mod offers TLS older than 1.2: ${older.map(tlsName).join(", ")}`);
  }

  if (!p.untrusted.handshakeFailures) reasons.push("S1: the untrusted certificate's handshake never failed");
  if (p.untrusted.requests) reasons.push("S1: data arrived over a certificate nobody trusts: verification is off");
  // The line must name the untrusted backend's origin: the hello phase's failure (another origin) doesn't count.
  if (!e.lines.some((l) => l.includes(`Couldn't reach the backend at ${p.untrusted.url} (`))) reasons.push("S9: no line for the failed TLS connection");

  return reasons.length
    ? { pass: false, reasons }
    : {
        pass: true,
        reasons: ["User-Agent kept; the redirect target received nothing; TLS 1.3 and 1.2 offered, nothing older; an untrusted certificate refused"],
      };
}
