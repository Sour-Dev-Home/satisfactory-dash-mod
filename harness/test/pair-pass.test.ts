import { describe, expect, it } from "vitest";
import { evaluate, requirements, type Pair } from "../../runs/pair/pass";
import type { CoreEvidence } from "../src/core-run";

const BOOT = "2026-11-09T10:00:00.000Z";
const at = (s: number) => new Date(Date.parse(BOOT) + s * 1000).toISOString();
const call = (path: string, s: number, status = 200) => ({ path, status, at: at(s), doneAt: at(s + 0.1) });
const PAIRED = "[t][1]LogSatisDash: Display: Paired with the dashboard; reporting to the backend";

function capture(over: Partial<Pair> = {}): Pair {
  return {
    bootAt: BOOT, codeLineAt: at(5), userCode: "ABCD-EFGH-JKLM", codeCount: 1, secretsChecked: 2, approvedAt: at(10), approveStatus: 200, pairedLine: PAIRED,
    credentialWritten: true,
    calls: [call("/agent/v1/pairings", 5, 201), call("/agent/v1/pairings/poll", 6), call("/agent/v1/pairings/poll", 11, 201), call("/agent/v1/snapshots", 14), call("/agent/v1/snapshots", 19)],
    secretSeen: { inLog: false, inUrl: false, inHeader: false },
    ...over,
  };
}
const run = (c: Pair | undefined) => evaluate({ lines: [], errorLines: [], leaks: { code: false, secret: false, bearer: false }, captures: c ? { "pair.json": c } : {} } as CoreEvidence);
const fails = (c: Pair, re: RegExp) => {
  const v = run(c);
  expect(v.pass).toBe(false);
  expect(v.reasons).toHaveLength(1);
  expect(v.reasons[0]).toMatch(re);
};

describe("pair pass rules", () => {
  it("needs the backend and pair", () => expect(requirements).toEqual(["backend", "pair"]));
  it("passes a clean pairing", () => expect(run(capture())).toMatchObject({ pass: true }));
  it("fails without a capture", () => expect(run(undefined).pass).toBe(false));
  it("accepts the Paired line with an approver", () => {
    expect(run(capture({ pairedLine: `${PAIRED} (approved by "Player-4F2K")` })).pass).toBe(true);
  });
  it("fails a code line after 30 s", () => fails(capture({ codeLineAt: at(31) }), /30 s/));
  it("fails no code line", () => fails(capture({ codeLineAt: null }), /code line/));
  it("fails two starts for one code", () => {
    fails(capture({ calls: [call("/agent/v1/pairings", 5, 201), call("/agent/v1/pairings", 6, 201), call("/agent/v1/pairings/poll", 7), call("/agent/v1/snapshots", 14)] }), /start/);
  });
  it("fails no start", () => fails(capture({ calls: [call("/agent/v1/pairings/poll", 7), call("/agent/v1/snapshots", 14)] }), /start/));
  it("fails when no pairing poll was made", () => {
    fails(capture({ calls: [call("/agent/v1/pairings", 5, 201), call("/agent/v1/snapshots", 14)] }), /no pairing poll was made/);
  });
  it("fails when no pairing secret was observed", () => fails(capture({ secretsChecked: 0 }), /no pairing secret was observed/));
  it("fails polls 3 s apart", () => {
    fails(capture({ calls: [call("/agent/v1/pairings", 5, 201), call("/agent/v1/pairings/poll", 6), call("/agent/v1/pairings/poll", 9), call("/agent/v1/snapshots", 14)] }), /apart/);
  });
  it("fails a refused approval", () => fails(capture({ approveStatus: 403 }), /approv/));
  it("fails a snapshot at 61 s", () => {
    fails(capture({ calls: [call("/agent/v1/pairings", 5, 201), call("/agent/v1/pairings/poll", 6), call("/agent/v1/snapshots", 71)] }), /60 s/);
  });
  it("fails no accepted snapshot", () => fails(capture({ calls: [call("/agent/v1/pairings", 5, 201), call("/agent/v1/pairings/poll", 6), call("/agent/v1/snapshots", 14, 401)] }), /snapshot/));
  it("fails a poll after the snapshot", () => {
    fails(capture({ calls: [...capture().calls, call("/agent/v1/pairings/poll", 20)] }), /after the first accepted snapshot/);
  });
  it("fails no credential", () => fails(capture({ credentialWritten: false }), /credential/));
  it("fails a bad Paired line", () => fails(capture({ pairedLine: "[t][1]LogSatisDash: Display: Paired with the dashboard" }), /Paired/));
  it("fails no Paired line", () => fails(capture({ pairedLine: null }), /Paired/));
  it.each(["inLog", "inUrl", "inHeader"] as const)("fails the secret %s", (k) => {
    fails(capture({ secretSeen: { inLog: false, inUrl: false, inHeader: false, [k]: true } }), /secret/);
  });
  it("fails a missing approval time", () => expect(run(capture({ approvedAt: null })).pass).toBe(false));
});
