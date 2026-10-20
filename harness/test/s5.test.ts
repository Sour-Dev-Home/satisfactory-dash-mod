import { describe, expect, it } from "vitest";
import { checkSnapshot } from "../src/snapshot-check";
import { evaluate, S5_AGENT_VERSION, s5SnapshotBody } from "../../spikes/S5/pass";

const line = "[t]LogSatisMod: Display: satis-mod s5 enroll=201 snapshot=200";
const good = { codeConsumed: true, agentVersion: S5_AGENT_VERSION, lastSeen: true, codeInLog: false, bearerInLog: false };

describe("S5", () => {
  it("the body the mod sends passes the vendored SnapshotRequestSchema", () => {
    expect(checkSnapshot(s5SnapshotBody("2026-10-20T12:00:00.000Z"))).toEqual({ ok: true });
  });
  it("passes on 201 then 200 with the database showing the enrolment and the snapshot", () => {
    expect(evaluate({ modLines: [line], echo: [], backend: good }).pass).toBe(true);
  });
  it("fails when the code or a bearer header shows up in the log", () => {
    expect(evaluate({ modLines: [line], echo: [], backend: { ...good, codeInLog: true } }).pass).toBe(false);
    expect(evaluate({ modLines: [line], echo: [], backend: { ...good, bearerInLog: true } }).pass).toBe(false);
  });
  it("fails on a refused enrolment, an unsent snapshot, or a database that disagrees", () => {
    expect(evaluate({ modLines: [line.replace("enroll=201", "enroll=400")], echo: [], backend: good }).pass).toBe(false);
    expect(evaluate({ modLines: [line], echo: [], backend: { ...good, lastSeen: false } }).pass).toBe(false);
    expect(evaluate({ modLines: [line], echo: [], backend: { ...good, agentVersion: "0.1.0" } }).pass).toBe(false);
  });
});
