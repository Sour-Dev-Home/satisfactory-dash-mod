import { describe, expect, it } from "vitest";
import { outboundScript, parseConnections, sampleOutbound } from "../src/outbound";

// What `Get-NetTCPConnection | Select-Object LocalAddress,LocalPort,RemoteAddress,RemotePort,State,OwningProcess |
// ConvertTo-Json` prints: State is a number (TcpState: Listen 2, SynSent 3, Established 5, TimeWait 11, Bound 100)
// unless -EnumsAsStrings is given.
const row = (over: Record<string, unknown>) => ({
  LocalAddress: "192.168.1.10", LocalPort: 50000, RemoteAddress: "203.0.113.7", RemotePort: 443, State: 5, OwningProcess: 4242, ...over,
});

describe("parseConnections", () => {
  it("returns the sorted set of remote:port of outbound connections", () => {
    const json = JSON.stringify([
      row({ RemoteAddress: "203.0.113.7", RemotePort: 443 }),
      row({ LocalPort: 50001, RemoteAddress: "127.0.0.1", RemotePort: 13105 }),
      row({ LocalPort: 50002, RemoteAddress: "203.0.113.7", RemotePort: 443 }),
      row({ LocalPort: 50003, RemoteAddress: "198.51.100.2", RemotePort: 80, State: 3 }),
    ]);
    expect(parseConnections(json, [])).toEqual(["127.0.0.1:13105", "198.51.100.2:80", "203.0.113.7:443"]);
  });

  it("leaves out connections on a port the process listens on: they are inbound", () => {
    const json = JSON.stringify([
      row({ LocalPort: 7777, RemoteAddress: "127.0.0.1", RemotePort: 60000 }),
      row({ LocalPort: 50000, RemoteAddress: "203.0.113.7", RemotePort: 443 }),
    ]);
    expect(parseConnections(json, [7777])).toEqual(["203.0.113.7:443"]);
  });

  it("leaves out Listen and TimeWait rows, numeric or named", () => {
    const json = JSON.stringify([
      row({ State: 2, LocalPort: 8080, RemoteAddress: "0.0.0.0", RemotePort: 0 }),
      row({ State: "Listen", LocalPort: 8081, RemoteAddress: "::", RemotePort: 0 }),
      row({ State: 11, RemoteAddress: "198.51.100.9", RemotePort: 443 }),
      row({ State: "TimeWait", RemoteAddress: "198.51.100.10", RemotePort: 443 }),
      row({ State: "Established", RemoteAddress: "198.51.100.11", RemotePort: 443 }),
    ]);
    expect(parseConnections(json, [])).toEqual(["198.51.100.11:443"]);
  });

  it("leaves out rows with no remote end (Bound, port 0)", () => {
    const json = JSON.stringify([row({ State: 100, RemoteAddress: "::", RemotePort: 0 }), row({ State: 5, RemoteAddress: "0.0.0.0", RemotePort: 0 })]);
    expect(parseConnections(json, [])).toEqual([]);
  });

  it("parses the single object PowerShell prints for one row", () => {
    expect(parseConnections(JSON.stringify(row({})), [])).toEqual(["203.0.113.7:443"]);
  });

  it("brackets an IPv6 remote address", () => {
    expect(parseConnections(JSON.stringify(row({ RemoteAddress: "::1", RemotePort: 13105 })), [])).toEqual(["[::1]:13105"]);
  });

  it("reads no output as no connections, and throws on output that is not rows", () => {
    expect(parseConnections("", [])).toEqual([]);
    expect(parseConnections("null", [])).toEqual([]);
    expect(() => parseConnections("{ not json", [])).toThrow();
    expect(() => parseConnections(JSON.stringify([{ LocalPort: "x" }]), [])).toThrow();
  });
});

describe("sampleOutbound", () => {
  it("runs pwsh with the root pid in the script and parses what it prints", async () => {
    const seen: string[][] = [];
    const exec = async (args: string[]) => {
      seen.push(args);
      return JSON.stringify({ listening: [7777], connections: [row({ LocalPort: 7777 }), row({ RemoteAddress: "127.0.0.1", RemotePort: 13105 })] });
    };
    expect(await sampleOutbound(4242, exec)).toEqual(["127.0.0.1:13105"]);
    expect(seen[0]!.slice(0, 3)).toEqual(["-NoProfile", "-NonInteractive", "-Command"]);
    expect(seen[0]![3]).toBe(outboundScript(4242));
  });

  it("accepts a single listening port printed as a number, and none printed as null", async () => {
    expect(await sampleOutbound(1, async () => JSON.stringify({ listening: 7777, connections: row({ LocalPort: 7777 }) }))).toEqual([]);
    expect(await sampleOutbound(1, async () => JSON.stringify({ listening: null, connections: null }))).toEqual([]);
  });

  it("the script walks the tree by ParentProcessId from the root pid and collects TCP rows by OwningProcess", () => {
    const s = outboundScript(4242);
    expect(s).toContain("$root = 4242");
    expect(s).toContain("Win32_Process");
    expect(s).toContain("ParentProcessId");
    expect(s).toContain("Get-NetTCPConnection");
    expect(s).toContain("OwningProcess");
    expect(() => outboundScript(Number.NaN)).toThrow();
    expect(() => outboundScript(-1)).toThrow();
  });

  it("rejects when pwsh fails (the process is gone)", async () => {
    await expect(sampleOutbound(1, async () => Promise.reject(new Error("process 1 is not running")))).rejects.toThrow("not running");
  });
});
