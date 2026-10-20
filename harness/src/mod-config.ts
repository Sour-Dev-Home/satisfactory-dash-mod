import { createHash, randomBytes } from "node:crypto";
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";

/** SML's path: FPaths::ProjectDir() + "Configs/" + "<ModReference>.cfg" with no category (ConfigManager.cpp:309-318). */
export function modConfigPath(serverDir: string, modReference: string): string {
  return join(serverDir, "FactoryGame", "Configs", `${modReference}.cfg`);
}

/** A root section serialises as a JSON object keyed by property name (ConfigPropertySection.cpp:18-24). */
export function writeModConfig(serverDir: string, modReference: string, values: Record<string, string>): string {
  const path = modConfigPath(serverDir, modReference);
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, `${JSON.stringify(values, null, 2)}\n`);
  return path;
}

export function removeModConfig(serverDir: string, modReference: string): void {
  rmSync(modConfigPath(serverDir, modReference), { force: true });
}

const BASE32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
/** 32 base32 characters (160 bits); made fresh for every run and never reused. */
export function randomCredential(): string {
  return [...randomBytes(32)].map((b) => BASE32[b % 32]).join("");
}

/** The mod logs FSHAHash::ToString() lowercased and cut to 12 characters (SecureHash.h:242, :375). */
export const sha1Prefix = (value: string): string => createHash("sha1").update(value, "utf8").digest("hex").slice(0, 12);
