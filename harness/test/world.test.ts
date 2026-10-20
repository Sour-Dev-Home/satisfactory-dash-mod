import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { loadWorld, otherServerSettings } from "../src/world";

let dir = "";
afterEach(() => dir && rmSync(dir, { recursive: true, force: true }));

describe("the S3 world", () => {
  it("uploads the save and loads it at once (saves capture E4)", async () => {
    const sent: unknown[] = [];
    const api = { upload: async (fn: string, data: Record<string, unknown>, file: { field: string; name: string; bytes: Buffer }) => (sent.push({ fn, data, field: file.field, name: file.name, size: file.bytes.length }), { status: 204, body: null }) };
    await loadWorld(api, "s3-1", Buffer.alloc(10));
    expect(sent).toEqual([{ fn: "UploadSaveGame", data: { SaveName: "s3-1", LoadSaveGame: true, EnableAdvancedGameSettings: false }, field: "saveGameFile", name: "s3-1.sav", size: 10 }]);
  });

  it("names the server's error code when the upload is refused", async () => {
    const api = { upload: async () => ({ status: 400, body: { errorCode: "unsupported_save_game" } }) };
    await expect(loadWorld(api, "s3-1", Buffer.alloc(1))).rejects.toThrow(/UploadSaveGame returned 400: unsupported_save_game/);
  });

  it("finds another server's settings in the saves folder S3 would write to (the owner's real server, 7777)", () => {
    dir = mkdtempSync(join(tmpdir(), "saves-"));
    writeFileSync(join(dir, "ServerSettings.17777.sav"), "");
    expect(otherServerSettings(dir, 17777)).toEqual([]);
    writeFileSync(join(dir, "ServerSettings.7777.sav"), "");
    expect(otherServerSettings(dir, 17777)).toEqual(["ServerSettings.7777.sav"]);
    expect(otherServerSettings(join(dir, "missing"), 17777)).toEqual([]);
  });
});
