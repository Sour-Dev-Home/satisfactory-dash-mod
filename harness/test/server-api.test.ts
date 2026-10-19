import { describe, expect, it } from "vitest";
import { createServerApi, type Transport } from "../src/server-api";

function recording(status = 200, text = '{"data":{}}') {
  const calls: Array<{ url: string; body: string }> = [];
  const transport: Transport = async (url, body) => {
    calls.push({ url: url.href, body });
    return { status, text };
  };
  return { calls, transport };
}

describe("createServerApi", () => {
  it("refuses plain http and non-loopback hosts", () => {
    expect(() => createServerApi("http://127.0.0.1:7777")).toThrow(/loopback/);
    expect(() => createServerApi("https://10.0.0.2:7777")).toThrow(/loopback/);
  });

  it("POSTs {function} to /api/v1, the captured request shape", async () => {
    const { calls, transport } = recording();
    await createServerApi("https://127.0.0.1:17777", transport).call("HealthCheck");
    expect(calls).toEqual([{ url: "https://127.0.0.1:17777/api/v1", body: '{"function":"HealthCheck"}' }]);
  });

  it("returns parsed JSON, raw text when not JSON, and null when empty", async () => {
    expect((await createServerApi("https://localhost:1", recording(200, '{"a":1}').transport).call("X")).body).toEqual({ a: 1 });
    expect((await createServerApi("https://localhost:1", recording(500, "oops").transport).call("X")).body).toBe("oops");
    expect((await createServerApi("https://localhost:1", recording(204, "").transport).call("X")).body).toBeNull();
  });
});
