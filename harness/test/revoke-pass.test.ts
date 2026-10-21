import { describe, expect, it } from "vitest";
import { evaluate, HALT_DISPATCH_GRACE_MS, HALT_LINE, HALT_WITHIN_MS, type Revoke } from "../../runs/revoke/pass";
import type { CoreEvidence } from "../src/core-run";
import type { RecordedCall } from "../src/recorder";

const HALT = `[t][1]LogSatisDash: Warning: ${HALT_LINE}: this server was revoked in the dashboard. Stopped; add a new code to SatisDashAgent.cfg to enrol again`;
const REVOKED_AT = "2026-10-20T16:00:00.000Z";
const at = (s: number) => new Date(Date.parse(REVOKED_AT) + s * 1000).toISOString();

/** doneAt defaults to 100 ms after at. */
const call = (over: Partial<RecordedCall>): RecordedCall => {
  const start = over.at ?? at(1);
  return {
    at: start, doneAt: new Date(Date.parse(start) + 100).toISOString(), agent: "mod", method: "POST", path: "/agent/v1/snapshots", status: 200, authorization: true, ...over,
  };
};
/** The halting 401 starts at 5 s and is delivered at 5.1 s, so the grace runs to 6.1 s. */
const ms = (s: number) => new Date(Date.parse(REVOKED_AT) + s).toISOString();

function capture(over: Partial<Revoke> = {}): Revoke {
  return {
    revokedAt: REVOKED_AT,
    halts: [at(6)],
    reseededAt: at(160),
    afterRevoke: [
      // A long poll that began before the revoke and ends with 200 after it is not "after the first 401".
      call({ at: at(-10), doneAt: at(4), method: "GET", path: "/agent/v1/commands" }),
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
    expect(v.reasons[0]).toContain(`0 request(s) in flight inside the ${HALT_DISPATCH_GRACE_MS} ms grace`);
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
  ])("fails when %s starts after the grace that follows the first 401's delivery", (_, extra) => {
    const c = capture();
    const v = evaluate(ev({ ...c, afterRevoke: [...c.afterRevoke, extra] }));
    expect(v.pass).toBe(false);
    expect(v.reasons.some((r) => r.startsWith(`1 request(s) started more than ${HALT_DISPATCH_GRACE_MS} ms after the first 401 was delivered`))).toBe(true);
  });

  it("declares a 1 s dispatch grace", () => {
    expect(HALT_DISPATCH_GRACE_MS).toBe(1000);
  });

  it("passes a second request already in flight inside the grace and answered 401, and lists it", () => {
    const c = capture();
    const poll = call({ at: ms(5050), doneAt: ms(5200), method: "GET", path: "/agent/v1/commands", status: 401 });
    const late = call({ at: ms(6050), doneAt: ms(6150), status: 401 }); // 950 ms after delivery: still inside
    const v = evaluate(ev({ ...c, afterRevoke: [...c.afterRevoke, poll, late] }));
    expect(v.pass).toBe(true);
    expect(v.reasons[0]).toContain(`2 request(s) in flight inside the ${HALT_DISPATCH_GRACE_MS} ms grace, all answered 401`);
  });

  it("fails a request inside the grace that was answered 200", () => {
    const c = capture();
    const v = evaluate(ev({ ...c, afterRevoke: [...c.afterRevoke, call({ at: ms(5300), status: 200 })] }));
    expect(v.pass).toBe(false);
    expect(v.reasons.some((r) => r.startsWith("1 request(s) inside the grace were not refused with 401: POST /agent/v1/snapshots answered 200"))).toBe(true);
  });

  it("fails a 401 that starts just after the grace", () => {
    const c = capture();
    const v = evaluate(ev({ ...c, afterRevoke: [...c.afterRevoke, call({ at: ms(6200), status: 401 })] }));
    expect(v.pass).toBe(false);
  });

  it("counts a request that started before the revoke and was answered 401 as the halting 401", () => {
    const c = capture({
      afterRevoke: [
        call({ at: ms(-200), doneAt: ms(300), status: 401 }),
        call({ at: ms(500), doneAt: ms(600), method: "GET", path: "/agent/v1/commands", status: 401 }),
      ],
      halts: [at(1)],
    });
    const v = evaluate(ev(c));
    expect(v.pass).toBe(true);
    expect(v.reasons[0]).toContain(`first 401 delivered at ${ms(300)}`);
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
