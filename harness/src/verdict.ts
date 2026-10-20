import type { CapturedRequest } from "./echo";
import type { TickSeries } from "./ticks";

export const SPIKE_IDS = ["S0", "S1", "S1b", "S2", "S3", "S4", "S5"] as const;
export type SpikeId = (typeof SPIKE_IDS)[number];

/** S4: what the harness wrote, and whether the full value showed up in the server log. */
export interface CredentialEvidence { length: number; sha1Prefix: string; inLog: boolean }
/** S5: what the throwaway database says after the run. */
export interface BackendEvidence { codeConsumed: boolean; agentVersion: string | null; lastSeen: boolean; codeInLog: boolean; bearerInLog: boolean }
/** S3: one mod reading paired with FRM's reading of the same circuit, taken right after it. */
export interface PowerReading { production: number; consumption: number; capacity: number }
export interface PowerPair { at: string; circuitId: number; mod: PowerReading; frm: PowerReading | null; group?: number[] }
/** S6: one round of status, as the mod read it natively and as QueryServerState plus HealthCheck reported it. The
 *  session is the first 12 hex characters of the name's SHA-1 over UTF-8: bundles are public. */
export interface StatusReading { running: boolean; paused: boolean; players: number; limit: number; duration: number; tickRate: number; health: string; session: string }
export interface StatusPair { at: string; mod: StatusReading; api: StatusReading | null }

export interface Evidence {
  modLines: string[];
  echo: CapturedRequest[];
  ticks?: { baseline: TickSeries; withMod: TickSeries };
  credential?: CredentialEvidence;
  backend?: BackendEvidence;
  power?: { pairs: PowerPair[]; frmCircuits: number[]; modCircuits: number[] };
  status?: { pairs: StatusPair[] };
}
export interface Verdict { pass: boolean; reasons: string[] }
