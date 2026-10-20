import { existsSync, readdirSync } from "node:fs";
import type { UploadingServerApi } from "./server-api";

/** UploadSaveGame with LoadSaveGame (satisfactory-dash raw-sources/dedicated-server-api-saves-2026-10-20.md, E4). The
 *  test server runs with insecure local access, so no token is needed. The API is down while the save loads. */
export async function loadWorld(api: Pick<UploadingServerApi, "upload">, saveName: string, bytes: Buffer): Promise<void> {
  const r = await api.upload(
    "UploadSaveGame",
    { SaveName: saveName, LoadSaveGame: true, EnableAdvancedGameSettings: false },
    { field: "saveGameFile", name: `${saveName}.sav`, bytes },
  );
  if (r.status < 200 || r.status > 299) {
    const code = (r.body as { errorCode?: unknown } | null)?.errorCode;
    throw new Error(`UploadSaveGame returned ${r.status}${typeof code === "string" ? `: ${code}` : ""}`);
  }
}

/** The server writes the uploaded save and its autosaves into the running account's SaveGames folder, named after the
 *  save's session. If another dedicated server keeps its settings there too (the owner's real server, as the owner's
 *  account), those autosaves would land in its rotation: S3 refuses, and runs only as satis-runner. */
export function otherServerSettings(savesDir: string, testPort: number): string[] {
  if (!existsSync(savesDir)) return [];
  return readdirSync(savesDir).filter((f) => {
    const m = /^ServerSettings\.(\d+)\.sav$/.exec(f);
    return m !== null && Number(m[1]) !== testPort;
  });
}
