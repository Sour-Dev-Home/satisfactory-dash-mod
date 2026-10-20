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
  it("passes a clean probe", () => {
    expect(evaluate(evidence(probe()))).toEqual({
      pass: true,
      reasons: ["User-Agent kept; redirect target contacted by nobody; TLS 1.2 or newer offered; an untrusted certificate refused"],
    });
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

  it("fails a request at the redirect target, naming how many carried Authorization", () => {
    const p = probe();
    p.requests.push(req({ port: "elsewhere", authorization: false }), req({ port: "elsewhere", authorization: true }));
    const v = evaluate(evidence(p));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("S3: the redirect target received 2 request(s), 1 with an Authorization header, 0 never completed");
  });

  it("fails a request at the redirect target whose body never came, and counts it as incomplete", () => {
    const p = probe();
    p.requests.push(
      req({ port: "elsewhere", authorization: true, bodyBytes: 0, complete: false }),
      req({ port: "elsewhere", authorization: true, bodyBytes: 0, complete: false }),
    );
    const v = evaluate(evidence(p));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("S3: the redirect target received 2 request(s), 2 with an Authorization header, 2 never completed");
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

  it("fails a hello offering TLS 1.0", () => {
    const v = evaluate(evidence(probe({ hellos: [{ supportedVersions: [0x0303, 0x0301] }] })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("S2: the mod offers TLS below 1.2 (0x301)");
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
