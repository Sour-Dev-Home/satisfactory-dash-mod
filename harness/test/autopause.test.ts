import { describe, expect, it } from "vitest";
import { collectAutoPauseSteps, parseS7Line, readAutoPause, readAutoPauseOptions } from "../src/autopause";

describe("readAutoPause", () => {
  it("reads only FG.DSAutoPause from GetServerOptions, which carries other options too", async () => {
    const options = (v: string) => ({ call: async () => ({ status: 200, body: { data: { serverOptions: { "FG.DSAutoPause": v, "other.Secret": "secret" }, pendingServerOptions: {} } } }) });
    expect(await readAutoPause(options("True"))).toBe(true);
    expect(await readAutoPause(options("False"))).toBe(false);
    await expect(readAutoPause({ call: async () => ({ status: 403, body: null }) })).rejects.toThrow(/GetServerOptions returned 403/);
    await expect(readAutoPause({ call: async () => ({ status: 200, body: { data: {} } }) })).rejects.toThrow(/no FG.DSAutoPause/);
  });
  it("reads the pending value too: none, True, False, or unreadable as none", async () => {
    const options = (pending: Record<string, unknown> | undefined) => ({ call: async () => ({ status: 200, body: { data: { serverOptions: { "FG.DSAutoPause": "False" }, pendingServerOptions: pending } } }) });
    expect(await readAutoPauseOptions(options({}))).toEqual({ applied: false, pending: null });
    expect(await readAutoPauseOptions(options({ "FG.DSAutoPause": "True" }))).toEqual({ applied: false, pending: true });
    expect(await readAutoPauseOptions(options({ "FG.DSAutoPause": "False" }))).toEqual({ applied: false, pending: false });
    expect(await readAutoPauseOptions(options({ "FG.DSAutoPause": "yes" }))).toEqual({ applied: false, pending: null });
    expect(await readAutoPauseOptions(options(undefined))).toEqual({ applied: false, pending: null });
  });
});

describe("parseS7Line", () => {
  it("reads a read step with its pending flag", () => {
    expect(parseS7Line("[t]LogSatisMod: Display: satis-mod s7 step=read value=True pending=0")).toEqual({ step: "read", value: true, pending: false });
  });
  it("reads a set step, which has no pending flag", () => {
    expect(parseS7Line("satis-mod s7 step=set value=False")).toEqual({ step: "set", value: false, pending: null });
  });
  it("ignores error and done lines", () => {
    expect(parseS7Line("satis-mod s7 error=no-option step=read")).toBeNull();
    expect(parseS7Line("satis-mod s7 done")).toBeNull();
  });
});

describe("collectAutoPauseSteps", () => {
  const clock = () => {
    let t = 0;
    return { now: () => t, sleep: async (ms: number) => void (t += ms) };
  };
  it("pairs each step with GetServerOptions read after it, and stops at done", async () => {
    const batches = [["satis-mod s7 step=read value=True pending=0"], ["satis-mod s7 step=set value=False"], ["satis-mod s7 done", "satis-mod s7 step=read value=True pending=0"]];
    const api = [true, false];
    const c = clock();
    const steps = await collectAutoPauseSteps({ readNew: () => batches.shift() ?? [], api: async () => api.shift()!, ...c, durationMs: 60_000, intervalMs: 500 });
    expect(steps.map((s) => [s.step, s.mod, s.pending, s.api])).toEqual([["read", true, false, true], ["set", false, null, false]]);
  });
  it("keeps a failed API read as null", async () => {
    const batches = [["satis-mod s7 step=read value=True pending=0", "satis-mod s7 done"]];
    const c = clock();
    const steps = await collectAutoPauseSteps({ readNew: () => batches.shift() ?? [], api: async () => { throw new Error("down"); }, ...c, durationMs: 60_000, intervalMs: 500 });
    expect(steps[0]?.api).toBeNull();
  });
  it("stops at the deadline without a done line", async () => {
    const c = clock();
    expect(await collectAutoPauseSteps({ readNew: () => [], api: async () => true, ...c, durationMs: 2_000, intervalMs: 500 })).toEqual([]);
  });
});
