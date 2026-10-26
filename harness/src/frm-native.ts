// FRM's HTTP output as the native-parity run's oracle (ADR-0058 Decision 3): what the harness reads from the test server's
// FRM, and the forms the judge compares. No FRM source is read; the shapes are the bridge's own fixtures and captures.
import { z } from "zod";
import { sessionHash } from "./status";

const LIMIT_METRES = 1_000_000;
const MAX_NODES = 5000;

/** A resourceNodes item as the contract has it (map.ts ResourceNodeSchema). */
export interface CanonNode { type: string; purity: string; nodeType: string; x: number; y: number; exploited: boolean; className?: string | undefined }

const FrmNodeSchema = z.object({
  Name: z.string(),
  ClassName: z.string().optional(),
  Purity: z.string(),
  NodeType: z.string(),
  Exploited: z.boolean(),
  location: z.object({ x: z.number(), y: z.number() }),
});

/** Math.round(units / 100), with -0 written 0 as JSON.stringify would. */
const metres = (units: number): number => {
  const m = Math.round(units / 100);
  return m === 0 ? 0 : m;
};
const cut = (s: string, n: number) => (s.length > n ? s.slice(0, n) : s);

/** nodeType.ts mapNodeType: the three known kinds by name, anything else camel-cased on JavaScript's \s. */
export function mapNodeType(raw: string): string {
  if (raw === "Node") return "node";
  if (raw === "Fracking Satellite") return "frackingSatellite";
  if (raw === "Geyser") return "geyser";
  return raw
    .split(/\s+/)
    .filter((w) => w.length > 0)
    .map((w, i) => (i === 0 ? w.toLowerCase() : w[0]!.toUpperCase() + w.slice(1).toLowerCase()))
    .join("");
}

/** FRM's Purity, lowercased, with the game's own spelling slip corrected: the contract (map.ts) says its 'Inpure' typo becomes 'impure'. */
export function mapPurity(purity: string): string {
  const p = purity.toLowerCase();
  return p === "inpure" ? "impure" : p;
}

/** getResourceNode's body to the contract's items (the mapper the bridge's golden pins), in FRM's order. Throws on an invalid record. */
export function mapFrmResourceNodes(body: unknown): CanonNode[] {
  const out: CanonNode[] = [];
  for (const r of z.array(FrmNodeSchema).parse(body)) {
    const x = metres(r.location.x);
    const y = metres(r.location.y);
    if (Math.abs(x) > LIMIT_METRES || Math.abs(y) > LIMIT_METRES) continue;
    const node: CanonNode = { type: cut(r.Name, 200), purity: cut(mapPurity(r.Purity), 40), nodeType: cut(mapNodeType(r.NodeType), 40), x, y, exploited: r.Exploited };
    // The contract's className is optional: FRM's value as it is, and no key when FRM has none (or an empty one).
    if (r.ClassName) node.className = cut(r.ClassName, 200);
    out.push(node);
  }
  return out.slice(0, MAX_NODES);
}

/** One string per node, sorted: two layers are equal when these lists are. */
export function canonNodes(nodes: CanonNode[]): string[] {
  return nodes.map((n) => JSON.stringify([n.type, n.purity, n.nodeType, n.x, n.y, n.exploited, n.className ?? ""])).sort();
}

const FrmPlayerSchema = z.object({ Name: z.string(), Online: z.boolean() });
/** getPlayer's body to "<hash of name>:<online>", sorted. Names are personal data (ADR-0029): only the hash leaves this function. */
export function playerKeys(body: unknown): string[] {
  return z.array(FrmPlayerSchema).parse(body).map((p) => `${sessionHash(p.Name)}:${p.Online}`).sort();
}

const GroupsSchema = z.array(z.object({ AssociatedCircuits: z.array(z.number()) }));
/** How many of getPower's groups hold more than one circuit (evidence on whether the world exercises the group merge). */
export function countMergedGroups(body: unknown): number {
  return GroupsSchema.parse(body).filter((g) => g.AssociatedCircuits.length > 1).length;
}

/** One successful read of an FRM endpoint and when it ran (ms since the epoch; the harness and the server share a clock). */
export interface Sample<T> { startedAt: number; endedAt: number; value: T }

/**
 * Reads on a fixed pause until stopped; take() reads once now (a failed read counts and is not kept). counts() has the same
 * field names as startFrmPoller's (reads, failed).
 */
export function startSampler<T>(
  read: () => Promise<T>,
  opts: { intervalMs: number; now: () => number; sleep: (ms: number) => Promise<void> },
): { samples: Sample<T>[]; counts(): { reads: number; failed: number }; take(): Promise<void>; stop(): void } {
  const samples: Sample<T>[] = [];
  let running = true;
  let reads = 0;
  let failed = 0;
  const take = async () => {
    const startedAt = opts.now();
    try {
      const value = await read();
      samples.push({ startedAt, endedAt: opts.now(), value });
      reads++;
    } catch {
      failed++;
    }
  };
  void (async () => {
    while (running) {
      await take();
      await opts.sleep(opts.intervalMs);
    }
  })();
  return { samples, counts: () => ({ reads, failed }), take, stop: () => void (running = false) };
}
