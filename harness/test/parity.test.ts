import { describe, expect, it } from "vitest";
import { captureWindow, NODE_MARGIN_MS } from "../src/core-runs/parity";
import type { RecordedCall } from "../src/recorder";
import { PARITY_NEIGHBOUR_GAP_MS } from "../../runs/parity/pass";

const T0 = Date.parse("2026-10-21T12:00:00.000Z");
const iso = (ms: number) => new Date(ms).toISOString();
const post = (agent: "mod" | "node", ms: number, over: Partial<RecordedCall> = {}): RecordedCall => ({
  at: iso(ms), doneAt: iso(ms + 50), agent, method: "POST", path: "/agent/v1/snapshots", status: 200, authorization: true,
  snapshot: { observedAt: iso(ms - 100), status: { sessionName: "h" }, power: { circuits: [] } },
  ...over,
});

describe("captureWindow (the parity run's capture of the recorder)", () => {
  it("keeps the Node agent's posts a margin either side of the window, so the edge rounds have neighbours", () => {
    expect(NODE_MARGIN_MS).toBeGreaterThanOrEqual(PARITY_NEIGHBOUR_GAP_MS);
    const from = iso(T0);
    const to = iso(T0 + 60_000);
    const c = captureWindow([
      post("mod", T0 - 1000), post("mod", T0 + 1000), post("mod", T0 + 61_000),
      post("node", T0 - NODE_MARGIN_MS - 1), post("node", T0 - 3000), post("node", T0 + 63_000), post("node", T0 + 60_000 + NODE_MARGIN_MS + 1),
    ], from, to);
    expect(c.mod.map((s) => s.at)).toEqual([iso(T0 + 1000)]);
    expect(c.node.map((s) => s.at)).toEqual([iso(T0 - 3000), iso(T0 + 63_000)]);
    expect(c.accepted).toEqual({ mod: 1, node: 2 });
  });

  it("counts oversized accepted posts (kept trimmed) and accepted posts it couldn't read, so the counts are real", () => {
    const c = captureWindow([
      post("node", T0 + 1000, { oversized: true }),
      post("node", T0 + 2000, { snapshot: undefined }),
      post("mod", T0 + 3000),
    ], iso(T0), iso(T0 + 60_000));
    expect(c.accepted).toEqual({ mod: 1, node: 2 });
    expect(c.oversizedAccepted).toEqual({ mod: 0, node: 1 });
    expect(c.unrecordedAccepted).toEqual({ mod: 0, node: 1 });
    expect(c.node).toHaveLength(1);
  });

  it("lists refused posts inside the window only (a post cut by the Node agent's stop after it isn't the backend's refusal)", () => {
    const c = captureWindow([
      post("mod", T0 + 1000, { status: 422 }),
      post("node", T0 + 2000, { status: 0 }),
      post("node", T0 + 61_000, { status: 0 }),
      post("node", T0 + 3000, { method: "GET", path: "/agent/v1/commands", status: 500 }),
    ], iso(T0), iso(T0 + 60_000));
    expect(c.refused).toEqual({ mod: [{ at: iso(T0 + 1000), status: 422 }], node: [{ at: iso(T0 + 2000), status: 0 }] });
  });
});
