// S12: where the server's process tree opens TCP connections to. Read by pid (the spawned launcher and its descendants,
// as treeMemory does), never by image name and never the harness's own pid.
import { execFile } from "node:child_process";
import { z } from "zod";

// TcpState as Get-NetTCPConnection's ConvertTo-Json prints it (a number), or its name with -EnumsAsStrings.
const LISTEN = new Set<unknown>([2, "Listen"]);
const TIME_WAIT = new Set<unknown>([11, "TimeWait"]);

const RowSchema = z.object({
  LocalPort: z.number().int(),
  RemoteAddress: z.string(),
  RemotePort: z.number().int(),
  State: z.union([z.number(), z.string()]),
});

/** One row or an array of rows (PowerShell prints a single row without an array); empty or null output is none. */
function rowsOf(json: string): unknown[] {
  if (!json.trim()) return [];
  const v = JSON.parse(json) as unknown;
  if (v === null) return [];
  return Array.isArray(v) ? v : [v];
}

/** An IPv4-mapped IPv6 address (::ffff:a.b.c.d) is the IPv4 address (review M4). */
export const normalizeAddress = (address: string) => address.replace(/^::ffff:(?=\d+\.\d+\.\d+\.\d+$)/i, "");

const endpoint = (raw: string, port: number) => {
  const address = normalizeAddress(raw);
  return address.includes(":") ? `[${address}]:${port}` : `${address}:${port}`;
};

/**
 * The sorted set of `remote:port` the rows connect out to. A row whose local port is one the process tree listens on is
 * inbound and left out, as are Listen and TimeWait rows and rows with no remote end (port 0, as Bound rows have).
 */
export function parseConnections(json: string, listening: number[]): string[] {
  const inbound = new Set(listening);
  const out = new Set<string>();
  for (const raw of rowsOf(json)) {
    const r = RowSchema.parse(raw);
    if (LISTEN.has(r.State) || TIME_WAIT.has(r.State)) continue;
    if (inbound.has(r.LocalPort) || r.RemotePort === 0) continue;
    out.add(endpoint(r.RemoteAddress, r.RemotePort));
  }
  return [...out].sort();
}

/**
 * The script: the process tree from rootPid by ParentProcessId (Win32_Process), the ports it listens on, its TCP
 * connections other than Listen, TimeWait, Bound and Closed, and the DNS client cache's A/AAAA rows (evidence only), as
 * JSON `{ listening, connections, dns, dnsFailed }`. It throws (pwsh exits non-zero) when rootPid is not running or the
 * process or TCP read fails: a failed read is a failed sample, never an empty one (review I1). Only the DNS read may
 * fail without failing the sample, since no pass condition uses it.
 */
export function outboundScript(rootPid: number): string {
  if (!Number.isInteger(rootPid) || rootPid <= 0) throw new Error(`not a pid: ${rootPid}`);
  return [
    "$ErrorActionPreference = 'Stop'",
    `$root = ${rootPid}`,
    "$all = @(Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId)",
    "$tree = @($all | Where-Object { $_.ProcessId -eq $root } | ForEach-Object { [int]$_.ProcessId })",
    "if ($tree.Count -eq 0) { throw \"process $root is not running\" }",
    "for ($i = 0; $i -lt $tree.Count; $i++) { $p = $tree[$i]; $tree += @($all | Where-Object { $_.ParentProcessId -eq $p -and $_.ProcessId -ne $p -and $tree -notcontains [int]$_.ProcessId } | ForEach-Object { [int]$_.ProcessId }) }",
    "$tcp = @(Get-NetTCPConnection | Where-Object { $tree -contains [int]$_.OwningProcess })",
    "$listening = @($tcp | Where-Object { [string]$_.State -eq 'Listen' } | ForEach-Object { [int]$_.LocalPort } | Sort-Object -Unique)",
    "$connections = @($tcp | Where-Object { @('Listen','TimeWait','Bound','Closed') -notcontains [string]$_.State } | Select-Object LocalAddress,LocalPort,RemoteAddress,RemotePort,State,OwningProcess)",
    "$dnsFailed = $false",
    "try { $dns = @(Get-DnsClientCache | Where-Object { ($_.Type -eq 1 -or $_.Type -eq 28) -and $_.Data } | Select-Object Entry,Data) } catch { $dns = @(); $dnsFailed = $true }",
    "[pscustomobject]@{ listening = $listening; connections = $connections; dns = $dns; dnsFailed = $dnsFailed } | ConvertTo-Json -Depth 4 -Compress -EnumsAsStrings",
  ].join("\n");
}

export type PwshExec = (args: string[]) => Promise<string>;

const realExec: PwshExec = (args) =>
  new Promise((resolve, reject) =>
    execFile("pwsh", args, { maxBuffer: 16 * 1024 * 1024, windowsHide: true, timeout: 60_000 }, (err, stdout, stderr) =>
      err ? reject(new Error(`sampling outbound connections failed: ${err.message} ${String(stderr).trim().slice(0, 300)}`)) : resolve(stdout),
    ));

const ListeningSchema = z.union([z.array(z.number().int()), z.number().int(), z.null()]);
const DnsRowSchema = z.object({ Entry: z.string(), Data: z.string().nullable() });

export interface OutboundSample {
  /** Sorted `remote:port` (parseConnections). */
  endpoints: string[];
  /** For each endpoint address the DNS cache names, the names that resolve to it, sorted (evidence only). */
  names: Record<string, string[]>;
  /** The DNS cache couldn't be read; the endpoints are still good. */
  dnsFailed: boolean;
}

/** The outbound endpoints of rootPid's process tree now, with DNS names for evidence. Rejects when they can't be read. */
export async function sampleOutbound(rootPid: number, exec: PwshExec = realExec): Promise<OutboundSample> {
  const stdout = await exec(["-NoProfile", "-NonInteractive", "-Command", outboundScript(rootPid)]);
  const v = z
    .object({ listening: ListeningSchema, connections: z.unknown(), dns: z.unknown().optional(), dnsFailed: z.boolean().optional() })
    .parse(JSON.parse(stdout));
  const listening = v.listening === null ? [] : Array.isArray(v.listening) ? v.listening : [v.listening];
  const endpoints = parseConnections(JSON.stringify(v.connections ?? null), listening);
  const addresses = new Set(endpoints.map((e) => e.replace(/:\d+$/, "").replace(/^\[(.*)\]$/, "$1")));
  const byAddress = new Map<string, Set<string>>();
  for (const raw of rowsOf(JSON.stringify(v.dns ?? null))) {
    const r = DnsRowSchema.parse(raw);
    if (r.Data === null) continue;
    const address = normalizeAddress(r.Data);
    if (!addresses.has(address)) continue;
    if (!byAddress.has(address)) byAddress.set(address, new Set());
    byAddress.get(address)!.add(r.Entry);
  }
  const names: Record<string, string[]> = {};
  for (const a of [...byAddress.keys()].sort()) names[a] = [...byAddress.get(a)!].sort();
  return { endpoints, names, dnsFailed: v.dnsFailed ?? false };
}
