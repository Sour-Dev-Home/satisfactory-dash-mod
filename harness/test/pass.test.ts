import { describe, expect, it } from "vitest";
import * as S0 from "../../spikes/S0/pass";
import * as S1 from "../../spikes/S1/pass";
import * as S1b from "../../spikes/S1b/pass";
import * as S2 from "../../spikes/S2/pass";
import type { CapturedRequest } from "../src/echo";
import type { TickSeries } from "../src/ticks";
import type { Evidence } from "../src/verdict";

const ev = (over: Partial<Evidence> = {}): Evidence => ({ modLines: [], echo: [], ...over });
const req = (path: string, body: string, contentType = "application/json"): CapturedRequest => ({
  at: "2026-10-19T16:00:00.000Z",
  method: "POST",
  path,
  headers: { "content-type": contentType },
  body,
  tooLarge: false,
});
const series = (median: number): TickSeries => ({ warmup: [], samples: [{ at: "x", value: median }], errors: 0, median });
const s2Posts = (ns: number[]) => ns.map((n) => req(S2.S2_PATH, JSON.stringify({ spike: "S2", n })));
const oneTo = (k: number) => Array.from({ length: k }, (_, i) => i + 1);

describe("S0 (C1)", () => {
  it("passes on the loaded line and fails without it", () => {
    expect(S0.claim).toBe("C1");
    expect(S0.evaluate(ev({ modLines: ["LogTemp: satis-mod loaded"] })).pass).toBe(true);
    expect(S0.evaluate(ev()).pass).toBe(false);
  });
});

describe("S1 (C2)", () => {
  const good = req(S1.S1_PATH, JSON.stringify(S1.S1_BODY));

  it("passes on the exact path, body and content type", () => {
    expect(S1.evaluate(ev({ echo: [good] }))).toMatchObject({ pass: true });
  });

  it("fails on the wrong path", () => {
    expect(S1.evaluate(ev({ echo: [{ ...good, path: "/other" }] })).pass).toBe(false);
  });

  it("fails on a different body or a missing JSON content type", () => {
    expect(S1.evaluate(ev({ echo: [req(S1.S1_PATH, '{"spike":"S1"}')] })).pass).toBe(false);
    expect(S1.evaluate(ev({ echo: [req(S1.S1_PATH, JSON.stringify(S1.S1_BODY), "text/plain")] })).pass).toBe(false);
  });
});

describe("S1b (C3)", () => {
  const good = "satis-mod s1b good status=200";
  const badErr = "satis-mod s1b bad error=certificate verify failed";

  it("passes only with a 200 on the good host and an error on the bad one", () => {
    expect(S1b.evaluate(ev({ modLines: [good, badErr] })).pass).toBe(true);
    expect(S1b.evaluate(ev({ modLines: [badErr] })).pass).toBe(false);
    expect(S1b.evaluate(ev({ modLines: [good] })).pass).toBe(false);
  });

  it("fails when the wrong hostname got a status (verification is off)", () => {
    expect(S1b.evaluate(ev({ modLines: [good, "satis-mod s1b bad status=200"] })).pass).toBe(false);
  });
});

describe("S2 (C4)", () => {
  const ticks = { baseline: series(30), withMod: series(29) };

  it("passes on posts 1..40 in order and a tick median within 5%", () => {
    expect(S2.evaluate(ev({ echo: s2Posts(oneTo(40)), ticks }))).toMatchObject({ pass: true });
  });

  it("fails on 39 posts, a gap, or a repeat", () => {
    expect(S2.evaluate(ev({ echo: s2Posts(oneTo(39)), ticks })).pass).toBe(false);
    expect(S2.evaluate(ev({ echo: s2Posts([...oneTo(20), ...oneTo(41).slice(21)]), ticks })).pass).toBe(false);
    expect(S2.evaluate(ev({ echo: s2Posts([...oneTo(39), 39]), ticks })).pass).toBe(false);
  });

  it("fails on a tick drop over 5% and passes at 4%", () => {
    expect(S2.evaluate(ev({ echo: s2Posts(oneTo(40)), ticks: { baseline: series(30), withMod: series(28.4) } })).pass).toBe(false);
    expect(S2.evaluate(ev({ echo: s2Posts(oneTo(40)), ticks: { baseline: series(30), withMod: series(28.8) } })).pass).toBe(true);
  });

  it("fails without tick series", () => {
    expect(S2.evaluate(ev({ echo: s2Posts(oneTo(40)) })).pass).toBe(false);
  });
});
