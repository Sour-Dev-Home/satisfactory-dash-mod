import { describe, expect, it } from "vitest";
import { evaluate, HALT_LINE, HALT_WITHIN_MS, type Revoke } from "../../runs/revoke/pass";
import type { CoreEvidence } from "../src/core-run";
import type { RecordedCall } from "../src/recorder";

const HALT = `[t][1]LogSatisDash: Warning: ${HALT_LINE}: this server was revoked in the dashboard. Stopped; add a new code to SatisDashAgent.cfg to enrol again`;
const REVOKED_AT = "2026-10-20T16:00:00.000Z";
const at = (s: number) => new Date(Date.parse(REVOKED_AT) + s * 1000).toISOString();

const call = (over: Partial<RecordedCall>): RecordedCall => ({
  at: at(1), agent: "mod", method: "POST", path: "/agent/v1/snapshots", status: 200, authorization: true, ...over,
});

function capture(over: Partial<Revoke> = {}): Revoke {
  return {
    revokedAt: REVOKED_AT,
    halts: [at(6)],
    reseededAt: at(160),
    afterRevoke: [
      // A long poll that began before the revoke and ends with 200 is not "after the first 401".
      call({ at: at(-10), method: "GET", path: "/agent/v1/commands" }),
      call({ at: at(3), status: 200 }),
      call({ at: at(5), status: 401 }),
    ],
    credentialAfterHalt: false,
    reenrolled: { enrolStatus: 201, acceptedAfter: 3 },
    ...over,
  };
}

const ev = (c: Revoke | undefined, lines = [HALT]): CoreEvidence => ({
  lines, errorLines: [], leaks: { code: false, secret: false, bearer: false }, captures: c ? { "revoke.json": c } : {},
});

describe("revoke pass", () => {
  it("passes a clean capture and states the counts", () => {
    const v = evaluate(ev(capture()));
    expect(v.pass).toBe(true);
    expect(v.reasons[0]).toContain("halted once, 6000 ms after the revoke");
    expect(v.reasons[0]).toContain("0 request(s) after the first 401");
    expect(v.reasons[0]).toContain("3 snapshot(s) accepted after it");
  });

  it("fails without a capture", () => {
    expect(evaluate(ev(undefined))).toEqual({ pass: false, reasons: ["no revoke capture"] });
  });

  it("allows 15 s for the halt and no more", () => {
    expect(HALT_WITHIN_MS).toBe(15_000);
    expect(evaluate(ev(capture({ halts: [at(15)] }))).pass).toBe(true);
    expect(evaluate(ev(capture({ halts: [at(16)] }))).reasons).toContain("the halt line came 16000 ms after the revoke, more than 15000 ms");
  });

  it("fails a halt line seen before the revoke", () => {
    expect(evaluate(ev(capture({ halts: [at(-1)] }))).reasons).toContain("the halt line came 1000 ms before the revoke");
  });

  it("fails when the halt line is missing from the log", () => {
    const v = evaluate(ev(capture(), []));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("the halt line appears 0 times in the log, not once");
  });

  it("fails when the halt line appears twice", () => {
    expect(evaluate(ev(capture(), [HALT, HALT])).reasons).toContain("the halt line appears 2 times in the log, not once");
    expect(evaluate(ev(capture({ halts: [at(6), at(8)] }))).reasons).toContain("the harness saw the halt line 2 times, not once");
  });

  it("fails when no time was recorded for the halt", () => {
    expect(evaluate(ev(capture({ halts: [] }))).reasons).toContain("the harness never saw the halt line");
  });

  it("fails when nothing was refused with 401", () => {
    const v = evaluate(ev(capture({ afterRevoke: [call({ at: at(3) })] })));
    expect(v.reasons).toContain("no request was refused with 401 after the revoke");
  });

  it.each([
    ["a snapshot", call({ at: at(20) })],
    ["a poll", call({ at: at(20), method: "GET", path: "/agent/v1/commands", status: 401 })],
    ["a result", call({ at: at(20), path: "/agent/v1/commands/abc/result", status: 401 })],
  ])("fails when %s reaches the recorder after the first 401", (_, extra) => {
    const c = capture();
    const v = evaluate(ev({ ...c, afterRevoke: [...c.afterRevoke, extra] }));
    expect(v.pass).toBe(false);
    expect(v.reasons.some((r) => r.startsWith("1 request(s) reached the recorder after the first 401"))).toBe(true);
  });

  it("ignores requests of another agent and the re-enrolment itself", () => {
    const c = capture();
    const v = evaluate(ev({ ...c, afterRevoke: [...c.afterRevoke, call({ at: at(20), agent: "other" }), call({ at: at(170), path: "/agent/v1/enroll", status: 201 })] }));
    expect(v.pass).toBe(true);
  });

  it("fails when credential.json is still there, or wasn't checked", () => {
    expect(evaluate(ev(capture({ credentialAfterHalt: true }))).reasons).toContain("credential.json is still there after the halt");
    expect(evaluate(ev(capture({ credentialAfterHalt: null }))).reasons).toContain("credential.json was never checked after the halt");
  });

  it("fails a re-enrolment that didn't answer 201, or never happened", () => {
    expect(evaluate(ev(capture({ reenrolled: { enrolStatus: 403, acceptedAfter: 3 } }))).reasons).toContain("the re-enrolment answered 403, not 201");
    expect(evaluate(ev(capture({ reenrolled: { enrolStatus: null, acceptedAfter: 0 } }))).reasons).toContain("no enrolment request after the new code");
  });

  it("fails fewer than 3 snapshots accepted after the re-enrolment", () => {
    expect(evaluate(ev(capture({ reenrolled: { enrolStatus: 201, acceptedAfter: 2 } }))).reasons).toContain("only 2 snapshot(s) accepted after the re-enrolment, not 3");
  });

  it("fails when the new code was never seeded", () => {
    expect(evaluate(ev(capture({ reseededAt: null }))).reasons).toContain("the new code was never seeded");
  });
});
