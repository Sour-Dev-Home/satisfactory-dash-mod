import { describe, expect, it } from "vitest";
import { RunnerConfigSchema } from "../src/config";
import { parseProcTcp, procAddress, procOutbound, sampleContainerOutbound } from "../src/outbound-docker";
import type { Exec } from "../src/server-host-docker";

// /proc/net/tcp rows: sl, local, remote, st, tx:rx, tr:when, retrnsmt, uid, timeout, inode, ...
const HEAD = "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode";
const row = (local: string, remote: string, st: string, inode: number) =>
  `   0: ${local} ${remote} ${st} 00000000:00000000 00:00000000 00000000  1000        0 ${inode} 1 0000000000000000 20 4 30 10 -1`;
// 127.0.0.1:27777 listening (the API), 127.0.0.1:27801 the recorder (the mod's connection), an inbound API connection,
// and socat's own connection, which is not the server's socket.
const TCP = [
  HEAD,
  row("0100007F:6C81", "00000000:0000", "0A", 101),
  row("0100007F:9C40", "0100007F:6CA1", "01", 102),
  row("0100007F:6C81", "0100007F:A000", "01", 103),
  row("0100007F:B000", "0100007F:6CA1", "01", 999),
].join("\n");

describe("procAddress", () => {
  it("reads IPv4 as one little-endian word", () => {
    expect(procAddress("0100007F")).toBe("127.0.0.1");
    expect(procAddress("0101A8C0")).toBe("192.168.1.1");
  });
  it("reads IPv6 as four little-endian words, and an IPv4-mapped one as IPv4", () => {
    expect(procAddress("00000000000000000000000001000000")).toBe("[::1]");
    expect(procAddress("0000000000000000FFFF00000100007F")).toBe("127.0.0.1");
  });
});

describe("parseProcTcp", () => {
  it("parses each row's ends, state and inode, skipping the header", () => {
    const rows = parseProcTcp(TCP);
    expect(rows).toHaveLength(4);
    expect(rows[1]).toEqual({ localAddress: "127.0.0.1", localPort: 40000, remoteAddress: "127.0.0.1", remotePort: 27809, state: "01", inode: 102 });
  });
});

describe("procOutbound", () => {
  it("keeps only the process's sockets, drops listening, inbound and closed rows", () => {
    expect(procOutbound(TCP, new Set([101, 102, 103]))).toEqual(["127.0.0.1:27809"]);
  });
  it("drops TIME_WAIT and CLOSE rows", () => {
    const t = [HEAD, row("0100007F:9C40", "0100007F:6CA1", "06", 102), row("0100007F:9C41", "0100007F:6CA1", "07", 104)].join("\n");
    expect(procOutbound(t, new Set([102, 104]))).toEqual([]);
  });
});

describe("sampleContainerOutbound", () => {
  const cfg = RunnerConfigSchema.parse({
    serverDir: "D:/r", apiBaseUrl: "https://127.0.0.1:27777", buildCommand: ["b"], builtPluginDir: "D:/b/{spike}",
    installed: { engine: "5.6.1", sml: "3.12.0" }, serverHost: "docker",
    docker: { container: "satis-linux-ds", image: "i", volume: "v", publish: ["127.0.0.1:27777:27778/tcp"] },
  });
  const ps = "  PID ARGS\n    1 /bin/bash /srv/satis/entrypoint.sh\n   42 /srv/satis/server/Engine/Binaries/Linux/FactoryServer-Linux-Shipping FactoryGame -log\n";

  it("reads the server process's sockets and connections inside the container", async () => {
    const calls: string[][] = [];
    const exec: Exec = async (_cmd, args) => {
      calls.push(args);
      if (args.includes("ps")) return { code: 0, stdout: ps, stderr: "" };
      return { code: 0, stdout: `socket:[101]\nsocket:[102]\n/dev/null\nsocket:[103]\n---\n${TCP}\n`, stderr: "" };
    };
    expect(await sampleContainerOutbound(exec, cfg)).toEqual({ endpoints: ["127.0.0.1:27809"], names: {}, dnsFailed: false });
    expect(calls[1]!.join(" ")).toContain("/proc/42/");
  });

  it("rejects when the server process isn't found or the read fails: a failed sample, never an empty one", async () => {
    const noServer: Exec = async () => ({ code: 0, stdout: "  PID ARGS\n    1 /bin/bash\n", stderr: "" });
    await expect(sampleContainerOutbound(noServer, cfg)).rejects.toThrow(/server process/);
    const failed: Exec = async (_c, args) => (args.includes("ps") ? { code: 0, stdout: ps, stderr: "" } : { code: 1, stdout: "", stderr: "boom" });
    await expect(sampleContainerOutbound(failed, cfg)).rejects.toThrow(/boom/);
  });
});
