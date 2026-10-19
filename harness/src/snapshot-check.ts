import { SnapshotRequestSchema } from "../contract/src/agent";

/** Whether a body the mod posted is a valid agent snapshot, by satisfactory-dash's own schema (vendored, pinned). */
export function checkSnapshot(value: unknown): { ok: true } | { ok: false; issues: string[] } {
  const r = SnapshotRequestSchema.safeParse(value);
  if (r.success) return { ok: true };
  return { ok: false, issues: r.error.issues.map((i) => `${i.path.join(".") || "(root)"}: ${i.message}`) };
}
