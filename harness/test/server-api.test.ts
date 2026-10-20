import { describe, expect, it } from "vitest";
import { createServerApi, type Transport } from "../src/server-api";

function recording(status = 200, text = '{"data":{}}') {
  const calls: Array<{ url: string; body: string; contentType?: string }> = [];
  const transport: Transport = async (url, body, contentType) => {
    calls.push({ url: url.href, body: body.toString("latin1"), ...(contentType ? { contentType } : {}) });
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

  it("adds {data} when given, as ApplyServerOptions needs (satisfactory-dash serverOptionsAdapter.ts:133-137)", async () => {
    const { calls, transport } = recording();
    await createServerApi("https://127.0.0.1:17777", transport).call("ApplyServerOptions", { UpdatedServerOptions: { "FG.DSAutoPause": "False" } });
    expect(calls[0]!.body).toBe('{"function":"ApplyServerOptions","data":{"UpdatedServerOptions":{"FG.DSAutoPause":"False"}}}');
  });

  it("uploads a file as multipart: a data part with the envelope and a saveGameFile part (saves capture E4)", async () => {
    const { calls, transport } = recording(204, "");
    const bytes = Buffer.from([0x00, 0xff, 0x41]);
    const r = await createServerApi("https://127.0.0.1:17777", transport).upload("UploadSaveGame", { SaveName: "s3", LoadSaveGame: true }, { field: "saveGameFile", name: "s3.sav", bytes });
    expect(r.status).toBe(204);
    const { body, contentType } = calls[0]!;
    const boundary = /^multipart\/form-data; boundary=(.+)$/.exec(contentType ?? "")?.[1];
    expect(boundary).toBeTruthy();
    expect(body).toContain(`--${boundary}\r\nContent-Disposition: form-data; name="data"\r\nContent-Type: application/json\r\n\r\n{"function":"UploadSaveGame","data":{"SaveName":"s3","LoadSaveGame":true}}\r\n`);
    expect(body).toContain(`Content-Disposition: form-data; name="saveGameFile"; filename="s3.sav"\r\nContent-Type: application/octet-stream\r\n\r\n${bytes.toString("latin1")}\r\n--${boundary}--\r\n`);
  });

  it("returns parsed JSON, raw text when not JSON, and null when empty", async () => {
    expect((await createServerApi("https://localhost:1", recording(200, '{"a":1}').transport).call("X")).body).toEqual({ a: 1 });
    expect((await createServerApi("https://localhost:1", recording(500, "oops").transport).call("X")).body).toBe("oops");
    expect((await createServerApi("https://localhost:1", recording(204, "").transport).call("X")).body).toBeNull();
  });
});
