import { describe, expect, it } from "vitest";
import { type Command, evaluate } from "../../runs/command/pass";
import type { CoreEvidence } from "../src/core-run";

const applied = (on: boolean) => `[t][1]LogSatisDash: Display: Applied set_auto_pause from the dashboard: auto-pause is now ${on ? "on" : "off"}`;

function capture(over: Partial<Command> = {}): Command {
  return {
    before: true,
    flipped: { id: "f", status: "succeeded", resultCode: null, apiAfter: false, resultStatuses: [200] },
    unsupported: { id: "u", status: "failed", resultCode: "unsupported", apiAfter: false, resultStatuses: [204] },
    restored: { id: "r", status: "succeeded", resultCode: null, apiAfter: true, resultStatuses: [200] },
    ...over,
  };
}

const ev = (c: Command | undefined, lines = [applied(false), applied(true)]): CoreEvidence => ({
  lines, errorLines: [], leaks: { code: false, secret: false, bearer: false }, captures: c ? { "command.json": c } : {},
});

describe("command pass", () => {
  it("passes a clean capture and states the counts", () => {
    const v = evaluate(ev(capture()));
    expect(v.pass).toBe(true);
    expect(v.reasons[0]).toContain("auto-pause on -> off -> on");
    expect(v.reasons[0]).toContain("Applied set_auto_pause logged 2 times");
  });

  it("passes starting from auto-pause off", () => {
    const c = capture({
      before: false,
      flipped: { id: "f", status: "succeeded", resultCode: null, apiAfter: true, resultStatuses: [200] },
      unsupported: { id: "u", status: "failed", resultCode: "unsupported", apiAfter: true, resultStatuses: [200] },
      restored: { id: "r", status: "succeeded", resultCode: null, apiAfter: false, resultStatuses: [200] },
    });
    expect(evaluate(ev(c)).pass).toBe(true);
  });

  it("fails without a capture", () => {
    expect(evaluate(ev(undefined))).toEqual({ pass: false, reasons: ["no command capture"] });
  });

  it("fails when a command never ran", () => {
    const v = evaluate(ev(capture({ flipped: null, unsupported: null, restored: null })));
    expect(v.reasons).toEqual(expect.arrayContaining(["the flip command never ran", "the unsupported command never ran", "the restore command never ran"]));
  });

  it("fails a flip that didn't succeed, or carries a result code", () => {
    const c = capture();
    expect(evaluate(ev({ ...c, flipped: { ...c.flipped!, status: "sent" } })).reasons).toContain("flip: status sent, not succeeded");
    expect(evaluate(ev({ ...c, flipped: { ...c.flipped!, resultCode: "upstream_error" } })).reasons).toContain("flip: result code upstream_error, expected none");
  });

  it("fails a flip that GetServerOptions doesn't show", () => {
    const c = capture();
    expect(evaluate(ev({ ...c, flipped: { ...c.flipped!, apiAfter: true } })).reasons).toContain("flip: GetServerOptions reads auto-pause true, expected false");
    expect(evaluate(ev({ ...c, flipped: { ...c.flipped!, apiAfter: null } })).reasons).toContain("flip: GetServerOptions didn't answer");
  });

  it.each([[[], 0], [[200, 200], 2], [[503, 503], 0], [[200, 204], 2]] as const)("fails a command whose results were accepted other than once (%j)", (statuses, n) => {
    const c = capture();
    expect(evaluate(ev({ ...c, flipped: { ...c.flipped!, resultStatuses: [...statuses] } })).reasons).toContain(`flip: ${n} result post(s) accepted, not 1`);
    expect(evaluate(ev({ ...c, restored: { ...c.restored!, resultStatuses: [...statuses] } })).reasons).toContain(`restore: ${n} result post(s) accepted, not 1`);
  });

  it("allows a transient no-answer/429/5xx refusal before the accepted result, and counts it in the pass reason", () => {
    const c = capture();
    const v = evaluate(ev({ ...c, flipped: { ...c.flipped!, resultStatuses: [503, 429, 0, 200] }, unsupported: { ...c.unsupported!, resultStatuses: [502, 204] } }));
    expect(v.pass).toBe(true);
    expect(v.reasons[0]).toContain("7 result post(s) in all, 4 refused transiently (no answer, 429 or 5xx) and retried");
  });

  it.each([400, 404, 409])("fails an extra result post refused with %i (not transient)", (s) => {
    const c = capture();
    expect(evaluate(ev({ ...c, flipped: { ...c.flipped!, resultStatuses: [s, 200] } })).reasons).toContain(`flip: a result post was answered ${s}, not 2xx or transient (no answer, 429 or 5xx)`);
  });

  it("fails a post that finished after the accepted one (re-review of 9e0f811)", () => {
    const c = capture();
    const v = evaluate(ev({ ...c, flipped: { ...c.flipped!, resultStatuses: [200, 503] } }));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("flip: a result post finished after the accepted one (answers in order: 200, 503)");
  });

  it("fails an unsupported command that succeeded, or failed for another reason", () => {
    const c = capture();
    expect(evaluate(ev({ ...c, unsupported: { ...c.unsupported!, status: "succeeded", resultCode: null } })).reasons).toEqual(
      expect.arrayContaining(["unsupported: status succeeded, not failed", "unsupported: result code none, expected unsupported"]),
    );
    expect(evaluate(ev({ ...c, unsupported: { ...c.unsupported!, resultCode: "upstream_error" } })).reasons).toContain("unsupported: result code upstream_error, expected unsupported");
  });

  it("fails an unsupported command that changed auto-pause", () => {
    const c = capture();
    expect(evaluate(ev({ ...c, unsupported: { ...c.unsupported!, apiAfter: true } })).reasons).toContain("unsupported: GetServerOptions reads auto-pause true, expected false");
    expect(evaluate(ev({ ...c, unsupported: { ...c.unsupported!, apiAfter: null } })).reasons).toContain("unsupported: GetServerOptions didn't answer");
  });

  it("fails a restore that didn't succeed or didn't restore", () => {
    const c = capture();
    expect(evaluate(ev({ ...c, restored: { ...c.restored!, status: "failed" } })).reasons).toContain("restore: status failed, not succeeded");
    expect(evaluate(ev({ ...c, restored: { ...c.restored!, apiAfter: false } })).reasons).toContain("restore: GetServerOptions reads auto-pause false, expected true");
  });

  it.each([[[applied(false)], 1], [[applied(false), applied(true), applied(true)], 3], [[], 0]] as const)("fails when Applied is logged other than twice", (lines, n) => {
    expect(evaluate(ev(capture(), [...lines])).reasons).toContain(`Applied set_auto_pause logged ${n} times, not 2`);
  });
});
