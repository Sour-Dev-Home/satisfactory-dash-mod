import { describe, expect, it } from "vitest";
import { evaluate, USER_AGENT } from "../../runs/http-probe/pass";
import type { CoreEvidence } from "../src/core-run";
import type { ProbeRequest } from "../src/probe-stub";

const REDIRECTED = "[t][1]LogSatisDash: Warning: The backend redirected a request; its answer was refused (redirects are never accepted)";
const UNTRUSTED_URL = "https://127.0.0.1:4443";
const HELLO_UNREACHABLE = "[t][2]LogSatisDash: Warning: Couldn't reach the backend at https://127.0.0.1:4442 (ConnectionError)";
const UNREACHABLE = `[t][3]LogSatisDash: Warning: Couldn't reach the backend at ${UNTRUSTED_URL} (ConnectionError)`;

const req = (over: Partial<ProbeRequest>): ProbeRequest => ({
  at: "2026-10-20T16:00:01.000Z",
  port: "backend",
  method: "POST",
  path: "/agent/v1/snapshots",
  userAgent: USER_AGENT,
  authorization: true,
  bodyBytes: 100,
  observedAt: null,
  complete: true,
  ...over,
});

function probe(over: Record<string, unknown> = {}) {
  return {
    redirectingSince: "2026-10-20T16:01:00.000Z",
    requests: [
      req({ path: "/agent/v1/enroll", authorization: false, at: "2026-10-20T16:00:00.000Z" }),
      req({ observedAt: "2026-10-20T16:00:01.000Z", at: "2026-10-20T16:00:01.000Z" }),
      req({ observedAt: "2026-10-20T16:00:06.000Z", at: "2026-10-20T16:00:06.000Z" }),
      req({ observedAt: "2026-10-20T16:00:11.000Z", at: "2026-10-20T16:00:11.000Z" }),
      req({ observedAt: "2026-10-20T16:00:16.000Z", at: "2026-10-20T16:01:01.000Z" }),
      req({ observedAt: "2026-10-20T16:00:16.000Z", at: "2026-10-20T16:01:02.000Z" }),
      req({ method: "GET", path: "/agent/v1/commands?waitSeconds=25", bodyBytes: 0, at: "2026-10-20T16:01:03.000Z" }),
    ],
    hellos: [{ supportedVersions: [0x0304, 0x0303] }],
    untrusted: { url: UNTRUSTED_URL, handshakeFailures: 1, requests: 0 },
    ...over,
  };
}

const evidence = (p: unknown, lines = [REDIRECTED, HELLO_UNREACHABLE, UNREACHABLE]): CoreEvidence => ({
  lines,
  errorLines: [],
  leaks: { code: false, secret: false, bearer: false },
  captures: p === undefined ? {} : { "probe.json": p },
});

describe("http-probe pass conditions", () => {
  it("passes a clean probe: nothing at the redirect target, only TLS 1.3 and 1.2 offered", () => {
    expect(evaluate(evidence(probe()))).toEqual({
      pass: true,
      reasons: ["User-Agent kept; the redirect target received nothing; TLS 1.3 and 1.2 offered, nothing older; an untrusted certificate refused"],
    });
  });

  it("fails one body-less GET at the redirect target: the mod never follows a redirect", () => {
    const p = probe();
    p.requests.push(req({ port: "elsewhere", method: "GET", authorization: false, bodyBytes: 0, at: "2026-10-20T16:01:01.500Z" }));
    const v = evaluate(evidence(p));
    expect(v.pass).toBe(false);
    expect(v.reasons).toEqual(["S3: the redirect target received 1 request(s)"]);
  });

  it("fails when no commands poll was redirected (Task 6 Step 8)", () => {
    const p = probe();
    p.requests = p.requests.filter((r) => !r.path.startsWith("/agent/v1/commands"));
    const v = evaluate(evidence(p));
    expect(v.pass).toBe(false);
    expect(v.reasons).toEqual(["S3: no commands poll reached the stub after the redirects began, so a redirected poll was not tested"]);
  });

  it("a commands poll before the redirects began doesn't count as redirected", () => {
    const p = probe();
    p.requests[6] = { ...p.requests[6]!, at: "2026-10-20T16:00:30.000Z" };
    expect(evaluate(evidence(p)).reasons).toEqual(["S3: no commands poll reached the stub after the redirects began, so a redirected poll was not tested"]);
  });

  it("fails a redirected poll that the target received with its Authorization header", () => {
    const p = probe();
    p.requests.push(req({ port: "elsewhere", method: "GET", path: "/agent/v1/commands", bodyBytes: 0, at: "2026-10-20T16:01:03.500Z" }));
    const v = evaluate(evidence(p));
    expect(v.pass).toBe(false);
    expect(v.reasons).toEqual(["S3: the redirect target received 1 request(s)", "S3: 1 request(s) at the redirect target carried an Authorization header"]);
  });

  it("fails an offer that includes TLS 1.1, naming every older version", () => {
    const v = evaluate(evidence(probe({ hellos: [{ supportedVersions: [0x0304, 0x0303, 0x0302] }, { supportedVersions: [0x0304, 0x0303, 0x0301] }] })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toEqual(["S2: the mod offers TLS older than 1.2: 1.1, 1.0"]);
  });

  it("fails no snapshot re-sent after the redirects began", () => {
    const p = probe();
    p.requests[5] = { ...p.requests[5]!, observedAt: "2026-10-20T16:00:21.000Z" };
    const v = evaluate(evidence(p));
    expect(v.pass).toBe(false);
    expect(v.reasons).toEqual(["S3: no snapshot was re-sent after the redirects began, so a redirected answer may have been accepted"]);
  });

  it("fails an offer without TLS 1.3", () => {
    const v = evaluate(evidence(probe({ hellos: [{ supportedVersions: [0x0303] }] })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toEqual(["S2: the mod doesn't offer TLS 1.3 and 1.2"]);
  });

  it("fails an offer without TLS 1.2", () => {
    const v = evaluate(evidence(probe({ hellos: [{ supportedVersions: [0x0304] }] })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toEqual(["S2: the mod doesn't offer TLS 1.3 and 1.2"]);
  });

  it("fails when the redirects never began: S3 is untested", () => {
    const v = evaluate(evidence(probe({ redirectingSince: null })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("S3: the redirects never began, so no redirected answer was tested");
  });

  it("fails without a capture", () => {
    expect(evaluate(evidence(undefined))).toEqual({ pass: false, reasons: ["no probe capture"] });
  });

  it("fails a wrong User-Agent", () => {
    const p = probe();
    p.requests[2] = { ...p.requests[2]!, userAgent: "curl/8" };
    const v = evaluate(evidence(p));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain(`User-Agent: expected ${USER_AGENT}, got "curl/8"`);
  });

  it("fails an Authorization header at the redirect target, counting them", () => {
    const p = probe();
    p.requests.push(
      req({ port: "elsewhere", method: "GET", authorization: false, bodyBytes: 0 }),
      req({ port: "elsewhere", method: "GET", authorization: true, bodyBytes: 0 }),
    );
    const v = evaluate(evidence(p));
    expect(v.pass).toBe(false);
    expect(v.reasons).toEqual([
      "S3: the redirect target received 2 request(s)",
      "S3: 1 request(s) at the redirect target carried an Authorization header",
    ]);
  });

  it("fails a body at the redirect target, counting them", () => {
    const p = probe();
    p.requests.push(req({ port: "elsewhere", authorization: false, bodyBytes: 120 }), req({ port: "elsewhere", authorization: false, bodyBytes: 0 }));
    const v = evaluate(evidence(p));
    expect(v.pass).toBe(false);
    expect(v.reasons).toEqual(["S3: the redirect target received 2 request(s)", "S3: 1 request(s) at the redirect target carried a body"]);
  });

  it("fails a snapshot sent again in the normal phase", () => {
    const p = probe();
    p.requests[3] = { ...p.requests[3]!, observedAt: p.requests[2]!.observedAt };
    const v = evaluate(evidence(p));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("a snapshot answered 200 by the URL asked was sent again: the transport refused a direct answer");
  });

  it("fails fewer than three snapshots before the redirects", () => {
    const p = probe({ redirectingSince: "2026-10-20T16:00:05.000Z" });
    expect(evaluate(evidence(p)).reasons).toContain("only 1 snapshots before the redirects began");
  });

  it("fails without a hello", () => {
    expect(evaluate(evidence(probe({ hellos: [] }))).reasons).toContain("S2: no ClientHello arrived");
  });

  it("fails data over the untrusted certificate, and a handshake that never failed", () => {
    const v = evaluate(evidence(probe({ untrusted: { url: UNTRUSTED_URL, handshakeFailures: 0, requests: 2 } })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("S1: data arrived over a certificate nobody trusts: verification is off");
    expect(v.reasons).toContain("S1: the untrusted certificate's handshake never failed");
  });

  it("fails a missing redirect line, and a missing TLS failure line", () => {
    const noRedirect = evaluate(evidence(probe(), [UNREACHABLE]));
    expect(noRedirect.pass).toBe(false);
    expect(noRedirect.reasons).toContain("S9: no line saying a redirected answer was refused");
    expect(evaluate(evidence(probe(), [REDIRECTED])).reasons).toContain("S9: no line for the failed TLS connection");
  });

  it("needs the TLS failure line for the untrusted backend's origin: the hello phase's line doesn't count", () => {
    const onlyHello = evaluate(evidence(probe(), [REDIRECTED, HELLO_UNREACHABLE]));
    expect(onlyHello.pass).toBe(false);
    expect(onlyHello.reasons).toEqual(["S9: no line for the failed TLS connection"]);
    expect(evaluate(evidence(probe(), [REDIRECTED, HELLO_UNREACHABLE, UNREACHABLE])).pass).toBe(true);
  });

  it("fails a ClientHello without supported_versions: its floor is unproven", () => {
    const v = evaluate(evidence(probe({ hellos: [{ supportedVersions: [0x0304, 0x0303] }, { supportedVersions: null }] })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toEqual(["S2: the ClientHello has no supported_versions, so its floor is unproven"]);
  });
});
