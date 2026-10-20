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

type CallApi = { call(fn: string, data?: Record<string, unknown>): Promise<{ status: number; body: unknown }> };

/** The session the server loads on startup: QueryServerState's autoLoadSessionName ("" when none). S6 clears it so the
 *  server boots with no game, and puts it back afterwards. */
export async function readAutoLoad(api: CallApi): Promise<string> {
  const r = await api.call("QueryServerState");
  if (r.status < 200 || r.status > 299) throw new Error(`QueryServerState returned ${r.status}`);
  const name = (r.body as { data?: { serverGameState?: { autoLoadSessionName?: unknown } } } | null)?.data?.serverGameState?.autoLoadSessionName;
  if (typeof name !== "string") throw new Error("QueryServerState has no autoLoadSessionName");
  return name;
}

/** SetAutoLoadSessionName (satisfactory-dash raw-sources/dedicated-server-api-saves-2026-10-20.md, E1). It doesn't
 *  change the loaded session; the next boot loads this one. */
export async function setAutoLoad(api: CallApi, name: string): Promise<void> {
  const r = await api.call("SetAutoLoadSessionName", { SessionName: name });
  if (r.status < 200 || r.status > 299) {
    const code = (r.body as { errorCode?: unknown } | null)?.errorCode;
    throw new Error(`SetAutoLoadSessionName returned ${r.status}${typeof code === "string" ? `: ${code}` : ""}`);
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
