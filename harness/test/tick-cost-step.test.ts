import { describe, expect, it } from "vitest";
import { step } from "../src/core-runs/tick-cost";

describe("tick-cost step labels (run 40562669608 failed with only 'read ECONNRESET')", () => {
  it("passes a value through", async () => {
    await expect(step("loading the world", Promise.resolve(3))).resolves.toBe(3);
  });
  it("names the step in front of the error and keeps the cause", async () => {
    const reset = Object.assign(new Error("read ECONNRESET"), { code: "ECONNRESET" });
    const e = await step("enrolling the mod", Promise.reject(reset)).catch((x: unknown) => x as Error);
    expect(e.message).toBe("enrolling the mod: read ECONNRESET");
    expect(e.cause).toBe(reset);
  });
});
