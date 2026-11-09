import { describe, expect, it } from "vitest";
import { PairingPollResponseSchema, PairingStartRequestSchema, PAIRING_EXPIRES_IN_SECONDS } from "../contract/src/pairing";

describe("vendored pairing contract", () => {
  it("accepts the mod's own version string", () => {
    expect(PairingStartRequestSchema.safeParse({ agentVersion: "mod/0.3.0" }).success).toBe(true);
  });
  it("keeps approvedBy optional on approved", () => {
    const approved = { status: "approved", agentSecret: "a".repeat(43), serverId: "srv-1", cadence: { statusSeconds: 5, powerSeconds: 5, factorySeconds: 30 } };
    expect(PairingPollResponseSchema.safeParse(approved).success).toBe(true);
  });
  it("pins the lifetime", () => expect(PAIRING_EXPIRES_IN_SECONDS).toBe(900));
});
