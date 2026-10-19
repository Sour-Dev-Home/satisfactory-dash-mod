import { appendFileSync, existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { logOffset, modLines, readLogSince, resetLog, versionLines, waitForLine } from "../src/log-watch";

let dir = "";
let log = "";
beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), "log-"));
  log = join(dir, "FactoryGame.log");
});
afterEach(() => rmSync(dir, { recursive: true, force: true }));

/** A fake clock and sleep: each sleep advances time by ms and runs the next scripted action. */
function fakeTime(actions: Array<() => void> = []) {
  let t = 0;
  return { now: () => t, sleep: async (ms: number) => { t += ms; actions.shift()?.(); } };
}

describe("waitForLine", () => {
  it("ignores lines before fromOffset and finds the one written after", async () => {
    writeFileSync(log, "[old] satis-mod loaded\n");
    const time = fakeTime([() => appendFileSync(log, "[new] satis-mod loaded\n")]);
    const r = await waitForLine(log, /satis-mod loaded/, { fromOffset: logOffset(log), timeoutMs: 10_000, pollMs: 1000, ...time });
    expect(r.line).toBe("[new] satis-mod loaded");
  });

  it("waits for a file that does not exist yet", async () => {
    const time = fakeTime([() => writeFileSync(log, "satis-mod loaded\n")]);
    const r = await waitForLine(log, /satis-mod loaded/, { fromOffset: 0, timeoutMs: 10_000, ...time });
    expect(r.line).toBe("satis-mod loaded");
  });

  it("reads from the start when the file shrank (rotated)", async () => {
    writeFileSync(log, "x".repeat(500) + "\n");
    const offset = logOffset(log);
    const time = fakeTime([() => writeFileSync(log, "satis-mod loaded\n")]);
    const r = await waitForLine(log, /satis-mod loaded/, { fromOffset: offset, timeoutMs: 10_000, ...time });
    expect(r.line).toBe("satis-mod loaded");
  });

  it("times out with the pattern and path in the message", async () => {
    writeFileSync(log, "nothing\n");
    await expect(
      waitForLine(log, /satis-mod loaded/, { fromOffset: 0, timeoutMs: 3000, pollMs: 1000, ...fakeTime() }),
    ).rejects.toThrow(/timed out after 3000 ms waiting for \/satis-mod loaded\//);
  });

  it("refuses a global regex (its lastIndex makes test() unreliable)", async () => {
    await expect(waitForLine(log, /x/g, { fromOffset: 0, timeoutMs: 1, ...fakeTime() })).rejects.toThrow(/global/);
  });
});

describe("resetLog", () => {
  it("deletes the previous run's log so this run reads its own from byte 0", () => {
    writeFileSync(log, "x".repeat(5000) + "\n[old] satis-mod loaded\n");
    expect(resetLog(log)).toBe(0);
    expect(existsSync(log)).toBe(false);
    // A rotated, regrown log larger than the old one is still read whole.
    writeFileSync(log, "y".repeat(9000) + "\nsatis-mod loaded\n");
    expect(modLines(readLogSince(log, 0))).toEqual(["satis-mod loaded"]);
  });

  it("is fine when there is no log yet", () => {
    expect(resetLog(log)).toBe(0);
  });
});

describe("log text helpers", () => {
  it("reads only what follows an offset", () => {
    writeFileSync(log, "a\nb\n");
    const offset = logOffset(log);
    appendFileSync(log, "c\n");
    expect(readLogSince(log, offset)).toBe("c\n");
  });

  it("keeps mod lines and the first version-looking lines", () => {
    const text = "LogInit: Engine Version: 5.6.1\nLogInit: Net CL: 502094\nLogTemp: satis-mod loaded\nother\n";
    expect(modLines(text)).toEqual(["LogTemp: satis-mod loaded"]);
    expect(versionLines(text)).toEqual(["LogInit: Engine Version: 5.6.1", "LogInit: Net CL: 502094"]);
  });
});
