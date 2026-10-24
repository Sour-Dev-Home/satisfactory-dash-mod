import { describe, expect, it } from "vitest";
import { collectPowerPairs, pairReadings, parseGetPower, parseGetPowerFull, parseS3Lines, pickBracket, readAutoPause, readAutoPauseOptions, startFrmPoller } from "../src/frm";

// Shape from satisfactory-dash raw-sources/captured-responses/frm-getPower-2026-09-22-F-panel.json (FRM 1.5.3).
const frmBody = [
  { CircuitGroupID: 0, PowerProduction: 3633.3, PowerConsumed: 2692.1, PowerCapacity: 4083.3, AssociatedCircuits: [0], FuseTriggered: false },
  { CircuitGroupID: 1, PowerProduction: 100, PowerConsumed: 50, PowerCapacity: 120, AssociatedCircuits: [3, 4], FuseTriggered: false },
];

describe("parseGetPowerFull (rawSchemas.ts:125-136)", () => {
  const extra = { PowerMaxConsumed: 5000, BatteryCapacity: 100, BatteryPercent: 42.5, BatteryDifferential: -3.25 };
  const full = frmBody.map((g) => ({ ...g, ...extra }));

  it("reads every figure the enrol run compares", () => {
    expect(parseGetPowerFull(full)).toEqual([
      { groupId: 0, production: 3633.3, consumption: 2692.1, capacity: 4083.3, maxConsumption: 5000, batteryCapacity: 100, batteryPercent: 42.5, batteryDifferential: -3.25, fuseTriggered: false },
      { groupId: 1, production: 100, consumption: 50, capacity: 120, maxConsumption: 5000, batteryCapacity: 100, batteryPercent: 42.5, batteryDifferential: -3.25, fuseTriggered: false },
    ]);
  });

  it("refuses a body missing a figure", () => {
    expect(() => parseGetPowerFull(frmBody)).toThrow();
  });

  it("gives a joined group (two FRM circuits, one group id) one row, and refuses one whose rows disagree", () => {
    const row = { ...full[1]!, AssociatedCircuits: [3] };
    expect(parseGetPowerFull([row, { ...row, AssociatedCircuits: [4] }])).toHaveLength(1);
    expect(() => parseGetPowerFull([row, { ...row, PowerConsumed: 51 }])).toThrow(/group 1/);
  });
});

describe("FRM reads that bracket the mod's sample (owner, 2026-10-20)", () => {
  const read = (startedAt: number, endedAt: number) => ({ startedAt, endedAt, groups: [] });

  it("takes the last read that ended by the sample and the first that started after it", () => {
    const reads = [read(0, 100), read(400, 500), read(800, 900), read(1200, 1300)];
    expect(pickBracket(reads, 700, 3000)).toEqual({ before: reads[1], after: reads[2] });
  });

  it("a read in flight across the sample brackets nothing", () => {
    expect(pickBracket([read(600, 800)], 700, 3000)).toEqual({ before: null, after: null });
  });

  it("drops reads farther than the gap from the sample", () => {
    expect(pickBracket([read(0, 100), read(6300, 6400)], 3200, 3000)).toEqual({ before: null, after: null });
  });

  it("polls until stopped, keeping only successful reads with their times", async () => {
    let t = 0;
    let n = 0;
    const poller = startFrmPoller(async () => {
      n++;
      if (n === 2) throw new Error("FRM busy");
      return parseGetPowerFull([]);
    }, { now: () => (t += 10), sleep: async () => { if (n >= 4) await new Promise(() => {}); } });
    await new Promise((r) => setTimeout(r, 20));
    poller.stop();
    expect(poller.reads).toHaveLength(3);
    expect(poller.counts()).toEqual({ reads: 3, failed: 1 });
    expect(poller.reads.every((x) => x.endedAt > x.startedAt)).toBe(true);
  });
});

describe("FRM pairing", () => {
  it("parses getPower groups and their circuits", () => {
    expect(parseGetPower(frmBody)).toEqual([
      { groupId: 0, circuits: [0], production: 3633.3, consumption: 2692.1, capacity: 4083.3 },
      { groupId: 1, circuits: [3, 4], production: 100, consumption: 50, capacity: 120 },
    ]);
    expect(() => parseGetPower({ nope: 1 })).toThrow();
  });

  it("parses the mod's s3 lines", () => {
    expect(parseS3Lines(["[t]LogSatisMod: Display: satis-mod s3 circuit=0 production=3633.33 consumption=2690.00 capacity=4083.33"]))
      .toEqual([{ circuitId: 0, reading: { production: 3633.33, consumption: 2690, capacity: 4083.33 } }]);
  });

  it("pairs a mod circuit with the FRM group holding it, and records the group's circuits (ruling 6, amended by the owner 2026-10-20)", () => {
    const mod = [
      { circuitId: 0, reading: { production: 3633.33, consumption: 2690, capacity: 4083.33 } },
      { circuitId: 3, reading: { production: 60, consumption: 30, capacity: 70 } },
    ];
    const pairs = pairReadings("t1", mod, parseGetPower(frmBody));
    expect(pairs[0]).toMatchObject({ circuitId: 0, frm: { production: 3633.3, capacity: 4083.3 } });
    expect(pairs[0]).toMatchObject({ group: [0] });
    expect(pairs[1]).toMatchObject({ circuitId: 3, group: [3, 4], frm: { production: 100, consumption: 50, capacity: 120 } });
  });

  it("pairs with null when FRM couldn't be read (Review Focus 4)", () => {
    const mod = [{ circuitId: 0, reading: { production: 1, consumption: 1, capacity: 1 } }];
    expect(pairReadings("t1", mod, null)).toEqual([{ at: "t1", circuitId: 0, mod: mod[0]!.reading, frm: null }]);
  });

  it("reads FRM right after each new batch of s3 lines, and pairs a failed FRM read with null", async () => {
    let t = 0;
    const batches = [["satis-mod s3 circuit=0 production=10 consumption=5 capacity=12"], [], ["satis-mod s3 circuit=0 production=11 consumption=5 capacity=12"]];
    let frmCalls = 0;
    const out = await collectPowerPairs({
      readNew: () => batches.shift() ?? [],
      frm: async () => {
        frmCalls++;
        if (frmCalls === 2) throw new Error("timeout");
        return [{ groupId: 0, circuits: [0], production: 10, consumption: 5, capacity: 12 }];
      },
      sleep: async (ms) => void (t += ms),
      now: () => t,
      durationMs: 3000,
      intervalMs: 1000,
    });
    expect(frmCalls).toBe(2); // only for the two batches that had lines
    expect(out.pairs.map((p) => p.frm === null)).toEqual([false, true]);
    expect(out.frmCircuits).toEqual([0]);
    expect(out.modCircuits).toEqual([0]);
  });

  it("reads only FG.DSAutoPause from GetServerOptions, which also carries FRM's token (final review, finding 4)", async () => {
    const options = (v: string) => ({ call: async () => ({ status: 200, body: { data: { serverOptions: { "FG.DSAutoPause": v, "uWS.AuthenticationToken": "secret" }, pendingServerOptions: {} } } }) });
    expect(await readAutoPause(options("True"))).toBe(true);
    expect(await readAutoPause(options("False"))).toBe(false);
    await expect(readAutoPause({ call: async () => ({ status: 403, body: null }) })).rejects.toThrow(/GetServerOptions returned 403/);
    await expect(readAutoPause({ call: async () => ({ status: 200, body: { data: {} } }) })).rejects.toThrow(/no FG.DSAutoPause/);
  });

  it("reads FG.DSAutoPause's pending value too, as the mod does: none, True, False, or unreadable as none (2026-10-24)", async () => {
    const options = (pending: Record<string, unknown> | undefined) => ({ call: async () => ({ status: 200, body: { data: { serverOptions: { "FG.DSAutoPause": "False" }, pendingServerOptions: pending } } }) });
    expect(await readAutoPauseOptions(options({}))).toEqual({ applied: false, pending: null });
    expect(await readAutoPauseOptions(options({ "FG.DSAutoPause": "True" }))).toEqual({ applied: false, pending: true });
    expect(await readAutoPauseOptions(options({ "FG.DSAutoPause": "False" }))).toEqual({ applied: false, pending: false });
    expect(await readAutoPauseOptions(options({ "FG.DSAutoPause": "yes" }))).toEqual({ applied: false, pending: null });
    expect(await readAutoPauseOptions(options(undefined))).toEqual({ applied: false, pending: null });
  });

  it("lists every FRM circuit, including those in multi-circuit groups, so the circuit check sees them (final review, finding 3)", async () => {
    let t = 0;
    const batches = [["satis-mod s3 circuit=3 production=60 consumption=30 capacity=70"]];
    const out = await collectPowerPairs({
      readNew: () => batches.shift() ?? [],
      frm: async () => parseGetPower(frmBody),
      sleep: async (ms) => void (t += ms),
      now: () => t,
      durationMs: 1000,
      intervalMs: 1000,
    });
    expect(out.frmCircuits.sort()).toEqual([0, 3, 4]);
    expect(out.pairs).toEqual([expect.objectContaining({ circuitId: 3, group: [3, 4], frm: expect.objectContaining({ production: 100 }) })]);
  });
});
