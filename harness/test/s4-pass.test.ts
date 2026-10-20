import { describe, expect, it } from "vitest";
import { evaluate } from "../../spikes/S4/pass";

const base = { echo: [], credential: { length: 32, sha1Prefix: "a9993e364706", inLog: false } };
const line = "[t]LogSatisMod: Display: satis-mod s4 read length=32 sha1=a9993e364706";

describe("S4 verdict", () => {
  it("passes when the mod read exactly the written value and the log never shows it", () => {
    expect(evaluate({ ...base, modLines: [line] }).pass).toBe(true);
  });
  it("fails when the credential appears in the log", () => {
    const r = evaluate({ ...base, modLines: [line], credential: { ...base.credential, inLog: true } });
    expect(r).toMatchObject({ pass: false, reasons: [expect.stringMatching(/appears in the server log/)] });
  });
  it("fails when the mod read a different value", () => {
    const r = evaluate({ ...base, modLines: [line.replace("a9993e364706", "000000000000")] });
    expect(r.reasons.join(" ")).toMatch(/read a different value/);
  });
  it("fails with no read line, and with no credential written", () => {
    expect(evaluate({ ...base, modLines: [] }).reasons.join(" ")).toMatch(/no "satis-mod s4 read" line/);
    expect(evaluate({ echo: [], modLines: [line] }).reasons.join(" ")).toMatch(/wrote no credential/);
  });
});
