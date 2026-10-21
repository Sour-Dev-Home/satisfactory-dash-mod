import { createServer } from "node:http";
import type { AddressInfo } from "node:net";
import { describe, expect, it } from "vitest";
import { captureWindow, NODE_MARGIN_MS, PARITY_RECORDER_OPTIONS } from "../src/core-runs/parity";
import { type RecordedCall, startRecorder } from "../src/recorder";
import { evaluate, PARITY_NEIGHBOUR_GAP_MS } from "../../runs/parity/pass";

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

  it("through the parity recorder, a Node post over 2 MB is trimmed and counted, and a mod post over 2 MB fails the run", async () => {
    const up = createServer((req, res) => {
      req.resume();
      req.on("end", () => res.writeHead(200, { "content-type": "application/json" }).end("{}"));
    });
    await new Promise<void>((r) => up.listen(0, "127.0.0.1", () => r()));
    const rec = await startRecorder(`http://127.0.0.1:${(up.address() as AddressInfo).port}`, PARITY_RECORDER_OPTIONS);
    try {
      const body = (o: number) => JSON.stringify({
        agentVersion: "x", observedAt: iso(T0 + o), reachable: true, paused: false, status: { sessionName: "s" }, power: { circuits: [] },
        factory: { buildings: Array.from({ length: 30_000 }, (_, i) => ({ id: `b${i}`, pad: "p".repeat(70) })) },
      });
      const postTo = (ua: string, b: string) => fetch(`${rec.url}/agent/v1/snapshots`, { method: "POST", headers: { "user-agent": ua, "content-type": "application/json" }, body: b });
      await postTo("satisfactory-dash-agent/0.1.0", body(1000));
      await postTo("satisfactory-dash-mod/0.1.0", body(2000));
      const c = captureWindow(rec.entries, iso(T0 - 60_000), iso(Date.now() + 60_000));
      expect(c.accepted).toEqual({ mod: 1, node: 1 });
      expect(c.oversizedAccepted).toEqual({ mod: 0, node: 1 });
      expect(c.unrecordedAccepted).toEqual({ mod: 1, node: 0 });
      expect(c.node).toHaveLength(1);
      const v = evaluate({ lines: [], errorLines: [], leaks: { code: false, secret: null, bearer: false }, captures: { "parity.json": {
        windowStartedAt: null, windowEndedAt: null, state: { mod: null, node: null }, tokensInvalidated: true, nodeAgent: null, cleanup: null, ...c,
      } } });
      expect(v.pass).toBe(false);
      expect(v.reasons).toContain("1 of the mod's accepted snapshots couldn't be read by the recorder, so they weren't judged");
    } finally {
      await rec.close();
      await new Promise<void>((r) => up.close(() => r()));
    }
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
