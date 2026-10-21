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

const endpoint = (address: string, port: number) => (address.includes(":") ? `[${address}]:${port}` : `${address}:${port}`);

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
 * The script: the process tree from rootPid by ParentProcessId (Win32_Process), the ports it listens on, and its TCP
 * connections other than Listen, TimeWait, Bound and Closed, as JSON `{ listening, connections }`. It throws (pwsh
 * exits non-zero) when rootPid is not running.
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
    "$tcp = @(Get-NetTCPConnection -ErrorAction SilentlyContinue | Where-Object { $tree -contains [int]$_.OwningProcess })",
    "$listening = @($tcp | Where-Object { [string]$_.State -eq 'Listen' } | ForEach-Object { [int]$_.LocalPort } | Sort-Object -Unique)",
    "$connections = @($tcp | Where-Object { @('Listen','TimeWait','Bound','Closed') -notcontains [string]$_.State } | Select-Object LocalAddress,LocalPort,RemoteAddress,RemotePort,State,OwningProcess)",
    "[pscustomobject]@{ listening = $listening; connections = $connections } | ConvertTo-Json -Depth 4 -Compress -EnumsAsStrings",
  ].join("\n");
}

export type PwshExec = (args: string[]) => Promise<string>;

const realExec: PwshExec = (args) =>
  new Promise((resolve, reject) =>
    execFile("pwsh", args, { maxBuffer: 16 * 1024 * 1024, windowsHide: true, timeout: 60_000 }, (err, stdout, stderr) =>
      err ? reject(new Error(`sampling outbound connections failed: ${err.message} ${String(stderr).trim().slice(0, 300)}`)) : resolve(stdout),
    ));

const ListeningSchema = z.union([z.array(z.number().int()), z.number().int(), z.null()]);

/** The outbound endpoints of rootPid's process tree now. Rejects when they can't be read. */
export async function sampleOutbound(rootPid: number, exec: PwshExec = realExec): Promise<string[]> {
  const stdout = await exec(["-NoProfile", "-NonInteractive", "-Command", outboundScript(rootPid)]);
  const v = z.object({ listening: ListeningSchema, connections: z.unknown() }).parse(JSON.parse(stdout));
  const listening = v.listening === null ? [] : Array.isArray(v.listening) ? v.listening : [v.listening];
  return parseConnections(JSON.stringify(v.connections ?? null), listening);
}
