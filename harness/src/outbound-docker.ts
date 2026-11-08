// S12 on the docker host (the Linux test server): where the server process opens TCP connections to, read inside its
// container. outbound.ts reads the Windows process tree with Get-NetTCPConnection; here the server's own sockets come from
// /proc/<pid>/fd and their rows from /proc/<pid>/net/tcp and tcp6, so the container's socat forwards (API_RELAY,
// FORWARD_PORTS) never count as the server's. The mod's connection to the recorder is 127.0.0.1:<forward port>, as the
// judge expects. A Linux container has no DNS client cache, so the image runs dnsmasq as its local resolver (root-entry.sh)
// with --log-queries to /var/log/satis-dns.log; the same read parses that log into `names`, as Windows' cache does. A
// missing log (an image without dnsmasq) sets dnsFailed, which S12 fails on.
import type { RunnerConfig } from "./config";
import { normalizeAddress, type OutboundSample } from "./outbound";
import { type Exec, findServerPid } from "./server-host-docker";

// /proc/net/tcp states (include/net/tcp_states.h): 06 TIME_WAIT, 07 CLOSE, 0A LISTEN. Windows leaves out Listen, TimeWait,
// Bound and Closed (outbound.ts); a Linux socket that is only bound has no row here.
const LISTEN = "0A";
const LEFT_OUT = new Set(["06", "07", LISTEN]);

/** /proc/net/tcp's hex address: IPv4 as one little-endian 32-bit word, IPv6 as four. An IPv4-mapped IPv6 address is IPv4. */
export function procAddress(hex: string): string {
  const bytes = (word: string) => [6, 4, 2, 0].map((i) => Number.parseInt(word.slice(i, i + 2), 16));
  if (hex.length === 8) return bytes(hex).join(".");
  const b = [0, 8, 16, 24].flatMap((i) => bytes(hex.slice(i, i + 8)));
  const groups = Array.from({ length: 8 }, (_, g) => ((b[2 * g]! << 8) | b[2 * g + 1]!).toString(16));
  const v4 = canonicalV6(groups.join(":"));
  return v4.includes(":") ? `[${v4}]` : v4;
}

/** An IPv6 address in the compressed form the URL parser gives, with an IPv4-mapped one turned into plain IPv4. */
function canonicalV6(text: string): string {
  const host = new URL(`http://[${text}]/`).hostname;
  return normalizeAddress(host.slice(1, -1).replace(/^::ffff:([0-9a-f]+):([0-9a-f]+)$/i, (_, hi: string, lo: string) => {
    const h = Number.parseInt(hi, 16);
    const l = Number.parseInt(lo, 16);
    return `::ffff:${h >> 8}.${h & 255}.${l >> 8}.${l & 255}`;
  }));
}

export interface ProcTcpRow { localAddress: string; localPort: number; remoteAddress: string; remotePort: number; state: string; inode: number }

/** The rows of /proc/net/tcp or tcp6 text (header lines skipped). */
export function parseProcTcp(text: string): ProcTcpRow[] {
  const out: ProcTcpRow[] = [];
  for (const line of text.split(/\r?\n/)) {
    const f = line.trim().split(/\s+/);
    if (f.length < 10 || !/^\d+:$/.test(f[0]!)) continue;
    const [la, lp] = f[1]!.split(":");
    const [ra, rp] = f[2]!.split(":");
    out.push({
      localAddress: procAddress(la!), localPort: Number.parseInt(lp!, 16),
      remoteAddress: procAddress(ra!), remotePort: Number.parseInt(rp!, 16),
      state: f[3]!.toUpperCase(), inode: Number(f[9]),
    });
  }
  return out;
}

/**
 * The sorted `remote:port` the process's sockets connect out to: its own rows only (by inode), leaving out LISTEN,
 * TIME_WAIT and CLOSE rows, rows with no remote end, and rows whose local port is one the process listens on (inbound).
 */
export function procOutbound(tcpText: string, socketInodes: ReadonlySet<number>): string[] {
  const own = parseProcTcp(tcpText).filter((r) => socketInodes.has(r.inode));
  const listening = new Set(own.filter((r) => r.state === LISTEN).map((r) => r.localPort));
  const out = new Set<string>();
  for (const r of own) {
    if (LEFT_OUT.has(r.state) || r.remotePort === 0 || listening.has(r.localPort)) continue;
    out.add(`${r.remoteAddress}:${r.remotePort}`);
  }
  return [...out].sort();
}

export const DNS_LOG = "/var/log/satis-dns.log";
const NO_DNS = "SATIS_NO_DNS_LOG";

/** An answer's address as outbound.ts keys `names` (no brackets, IPv4-mapped IPv6 as IPv4), or null when it isn't one. */
function answerAddress(text: string): string | null {
  if (/^\d{1,3}(\.\d{1,3}){3}$/.test(text)) return normalizeAddress(text);
  if (!text.includes(":")) return null;
  try {
    return canonicalV6(text);
  } catch {
    return null;
  }
}

/**
 * The names behind each address in a dnsmasq query log (--log-queries): every `reply`/`cached` answer that is an IP maps its
 * address to the name on its line and to the most recent `query[...]` name (so a CNAME chain maps to the name the program
 * asked for, as Windows' cache `Entry` does). NXDOMAIN, NODATA and <CNAME> answers are ignored. Keys and names sorted.
 */
export function parseDnsmasqLog(text: string): Record<string, string[]> {
  const byAddress = new Map<string, Set<string>>();
  let asked: string | undefined;
  for (const line of text.split(/\r?\n/)) {
    const m = /dnsmasq\[\d+\]: (.*)$/.exec(line);
    if (!m) continue;
    const q = /^query\[[^\]]*\] (\S+) from /.exec(m[1]!);
    if (q) { asked = q[1]; continue; }
    const a = /^(?:reply|cached) (\S+) is (\S+)$/.exec(m[1]!);
    if (!a) continue;
    const address = answerAddress(a[2]!);
    if (address === null) continue;
    const set = byAddress.get(address) ?? new Set<string>();
    byAddress.set(address, set);
    set.add(a[1]!);
    if (asked !== undefined) set.add(asked);
  }
  const out: Record<string, string[]> = {};
  for (const k of [...byAddress.keys()].sort()) out[k] = [...byAddress.get(k)!].sort();
  return out;
}

/** One S12 sample of the Linux server, shaped as outbound.ts's. Rejects when it can't be read (a failed sample, never empty). */
export async function sampleContainerOutbound(exec: Exec, cfg: RunnerConfig): Promise<OutboundSample> {
  const name = cfg.docker?.container;
  if (!name) throw new Error('serverHost "docker" needs a docker block in the runner config');
  const ps = await exec("docker", ["exec", name, "ps", "-eo", "pid,args"]);
  if (ps.code !== 0) throw new Error(`sampling outbound connections failed: ps: ${ps.stderr.trim().slice(0, 300)}`);
  const pid = findServerPid(ps.stdout);
  if (pid === null) throw new Error("sampling outbound connections failed: the server process is not running in the container");
  // The dnsmasq log is the last section; a missing file (an image without dnsmasq) prints NO_DNS instead of failing the read.
  const script = `for f in /proc/${pid}/fd/*; do readlink "$f"; done; echo ---; cat /proc/${pid}/net/tcp /proc/${pid}/net/tcp6; echo ---; cat ${DNS_LOG} 2>/dev/null || echo ${NO_DNS}`;
  const read = await exec("docker", ["exec", name, "sh", "-c", script]);
  if (read.code !== 0) throw new Error(`sampling outbound connections failed: ${read.stderr.trim().slice(0, 300)}`);
  const [fds, tcp, dns] = read.stdout.split(/^---$/m);
  if (tcp === undefined) throw new Error("sampling outbound connections failed: no TCP table read");
  const inodes = new Set([...fds!.matchAll(/socket:\[(\d+)\]/g)].map((m) => Number(m[1])));
  const endpoints = procOutbound(tcp, inodes);
  const log = dns?.trim() ?? "";
  // Missing log, or an empty one while the server has connections: names can't be known, so the DNS read failed.
  const dnsFailed = log.includes(NO_DNS) || (log === "" && endpoints.length > 0);
  const addresses = new Set(endpoints.map((e) => e.replace(/:\d+$/, "").replace(/^\[(.*)\]$/, "$1")));
  const parsed = dnsFailed ? {} : parseDnsmasqLog(log);
  const names: Record<string, string[]> = {};
  for (const a of Object.keys(parsed).sort()) if (addresses.has(a)) names[a] = parsed[a]!;
  return { endpoints, names, dnsFailed };
}
