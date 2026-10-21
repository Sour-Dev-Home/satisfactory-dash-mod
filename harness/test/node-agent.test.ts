import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { RunnerConfigSchema } from "../src/config";
import { type AgentChild, type AgentSpawner, agentPorts, generateApiToken, invalidateApiTokens, startNodeAgent } from "../src/core-runs/node-agent";
import type { ServerApi } from "../src/server-api";

// Token format: base64 JSON payload "." hex fingerprint (dedicated-server-api.md E5); {"pl":"APIToken"}.
const TOKEN = `${Buffer.from(JSON.stringify({ pl: "APIToken" })).toString("base64")}.0A1B2C3D4E5F60718293A4B5C6D7E8F9`;
const FRM_TOKEN = "frm-secret-123";
const CODE = "AB3D-7XQ2-K5MN";

let roots: string[] = [];
afterEach(() => {
  for (const r of roots) rmSync(r, { recursive: true, force: true });
  roots = [];
  vi.restoreAllMocks();
});
/** The error a promise rejects with; resolving fails the test. */
const failure = (p: Promise<unknown>): Promise<Error> =>
  p.then(
    () => {
      throw new Error("expected a failure");
    },
    (e: unknown) => e as Error,
  );
const tempRoot = () => {
  const r = mkdtempSync(join(tmpdir(), "node-agent-test-"));
  roots.push(r);
  return r;
};

interface Spawned { node: string; args: string[]; env: NodeJS.ProcessEnv; stdin: string | undefined; pid: number; homeExisted: boolean; exit(code: number | null): void }

/** Each step exits with codes[step] once its stdin is closed (undefined: never); `run` lives until killed. */
function fakeSpawner(codes: Partial<Record<string, number | undefined>> = {}, output = "") {
  const spawned: Spawned[] = [];
  const spawn: AgentSpawner = (node, args, env) => {
    let exit: (code: number | null) => void = () => undefined;
    const exited = new Promise<number | null>((r) => (exit = r));
    const step = args[1]!;
    const s: Spawned = { node, args, env, stdin: undefined, pid: 4000 + spawned.length, homeExisted: existsSync(String(env.SD_AGENT_HOME)), exit };
    spawned.push(s);
    const child: AgentChild = {
      pid: s.pid,
      exited,
      end: (text) => {
        s.stdin = text;
        if (step === "run") return;
        const code = step in codes ? codes[step] : 0;
        if (code !== undefined) setTimeout(() => exit(code), 1);
      },
      output: () => output,
    };
    return child;
  };
  const killed: number[] = [];
  const killTree = async (pid: number) => {
    killed.push(pid);
    spawned.find((s) => s.pid === pid)?.exit(1);
  };
  return { spawn, spawned, killed, killTree };
}

const base = (f: ReturnType<typeof fakeSpawner>, over: Record<string, unknown> = {}) => ({
  agentCjs: "D:/agent/agent.cjs",
  node: "node.exe",
  apiPort: 17777,
  frmPort: 8080,
  apiToken: TOKEN,
  frmToken: FRM_TOKEN,
  code: CODE,
  backendUrl: "http://127.0.0.1:50123",
  tempRoot: tempRoot(),
  spawn: f.spawn,
  killTree: f.killTree,
  stepTimeoutMs: 2000,
  ...over,
});

describe("startNodeAgent (the parity run's satisfactory-dash agent)", () => {
  it("runs set-tokens, check, enroll and run in that order, all in one fresh SD_AGENT_HOME", async () => {
    const f = fakeSpawner();
    const opts = base(f);
    const agent = await startNodeAgent(opts);
    expect(f.spawned.map((s) => [s.node, ...s.args])).toEqual([
      ["node.exe", "D:/agent/agent.cjs", "set-tokens", "--host", "127.0.0.1", "--api-port", "17777", "--frm-port", "8080"],
      ["node.exe", "D:/agent/agent.cjs", "check"],
      ["node.exe", "D:/agent/agent.cjs", "enroll", CODE, "--url", "http://127.0.0.1:50123"],
      ["node.exe", "D:/agent/agent.cjs", "run"],
    ]);
    const homes = new Set(f.spawned.map((s) => s.env.SD_AGENT_HOME));
    expect(homes.size).toBe(1);
    const home = [...homes][0]!;
    expect(dirname(home)).toBe(opts.tempRoot);
    expect(f.spawned.every((s) => s.homeExisted)).toBe(true);
    expect(agent.pid).toBe(f.spawned[3]!.pid);
    expect(existsSync(home)).toBe(true);
    await agent.stop();
    expect(f.killed).toEqual([f.spawned[3]!.pid]); // the run's tree, by pid
    expect(existsSync(home)).toBe(false);
    await agent.stop(); // twice is harmless
    expect(f.killed).toHaveLength(1);
  });

  it("gives the tokens on set-tokens' stdin only, never in an argument, the environment or a log", async () => {
    const log = vi.spyOn(console, "log");
    const error = vi.spyOn(console, "error");
    const warn = vi.spyOn(console, "warn");
    const f = fakeSpawner();
    const agent = await startNodeAgent(base(f));
    expect(f.spawned[0]!.stdin).toBe(`${TOKEN}\n${FRM_TOKEN}\n`);
    expect(f.spawned.slice(1).map((s) => s.stdin)).toEqual(["", "", ""]);
    for (const s of f.spawned) {
      expect(s.args.join(" ")).not.toContain(TOKEN);
      expect(s.args.join(" ")).not.toContain(FRM_TOKEN);
      expect(Object.values(s.env).join("\n")).not.toContain(TOKEN);
    }
    expect([...log.mock.calls, ...error.mock.calls, ...warn.mock.calls]).toEqual([]);
    await agent.stop();
  });

  it("answers FRM's prompt with an empty line when there is no FRM token", async () => {
    const f = fakeSpawner();
    const agent = await startNodeAgent(base(f, { frmToken: undefined }));
    expect(f.spawned[0]!.stdin).toBe(`${TOKEN}\n\n`);
    await agent.stop();
  });

  it("a step that fails stops there, names the step and its exit code, redacts the tokens and code, and deletes the store", async () => {
    const f = fakeSpawner({ check: 1 }, `Reading the game ... FAILED ${TOKEN} ${FRM_TOKEN} ${CODE}\n`);
    const err = await failure(startNodeAgent(base(f)));
    expect(err).toBeInstanceOf(Error);
    expect(err.message).toMatch(/^the Node agent's check exited 1/);
    expect(err.message).not.toContain(TOKEN);
    expect(err.message).not.toContain(FRM_TOKEN);
    expect(err.message).not.toContain(CODE);
    expect(f.spawned.map((s) => s.args[1])).toEqual(["set-tokens", "check"]);
    expect(existsSync(String(f.spawned[0]!.env.SD_AGENT_HOME))).toBe(false);
  });

  it("a step that hangs is killed after its timeout, and the store is deleted", async () => {
    const f = fakeSpawner({ enroll: undefined });
    const err = await failure(startNodeAgent(base(f, { stepTimeoutMs: 50 })));
    expect(err.message).toMatch(/the Node agent's enroll did not finish within 50 ms/);
    expect(f.killed).toEqual([f.spawned[2]!.pid]);
    expect(existsSync(String(f.spawned[0]!.env.SD_AGENT_HOME))).toBe(false);
  });

  it("refuses a backend that isn't http(s) on loopback, a malformed code or port, before spawning anything", async () => {
    for (const over of [{ backendUrl: "http://10.0.0.5:3000" }, { backendUrl: "http://127.0.0.1:1/path" }, { code: "ab3d-7xq2" }, { apiPort: 0 }, { frmPort: 70000 }]) {
      const f = fakeSpawner();
      await expect(startNodeAgent(base(f, over))).rejects.toThrow();
      expect(f.spawned).toEqual([]);
    }
  });
});

describe("agentPorts", () => {
  const cfg = (over: Record<string, unknown> = {}) =>
    RunnerConfigSchema.parse({ serverDir: "D:/s", apiBaseUrl: "https://127.0.0.1:17777", buildCommand: ["b"], builtPluginDir: "D:/{spike}", installed: { engine: "5.6.1", sml: "3.12.0" }, ...over });

  it("takes the API port from apiBaseUrl and FRM's from frmBaseUrl", () => {
    expect(agentPorts(cfg({ frmBaseUrl: "http://127.0.0.1:8080" }))).toEqual({ apiPort: 17777, frmPort: 8080 });
  });

  it("needs frmBaseUrl", () => {
    expect(() => agentPorts(cfg())).toThrow(/frmBaseUrl/);
  });
});

describe("the game API token (server.GenerateAPIToken through RunCommand)", () => {
  const api = (answer: { status: number; body: unknown }) => {
    const calls: Array<{ fn: string; data?: Record<string, unknown> }> = [];
    const a: ServerApi = { call: async (fn, data) => (calls.push({ fn, data }), answer) };
    return { a, calls };
  };

  it("runs server.GenerateAPIToken and takes the token out of the command's result", async () => {
    const { a, calls } = api({ status: 200, body: { data: { commandResult: `Generated API Token: ${TOKEN}\n`, returnValue: true } } });
    expect(await generateApiToken(a)).toBe(TOKEN);
    expect(calls).toEqual([{ fn: "RunCommand", data: { Command: "server.GenerateAPIToken" } }]);
  });

  it("fails without echoing the answer when the call fails, the result has no token, or the token isn't an application token", async () => {
    const client = Buffer.from(JSON.stringify({ pl: "Client" })).toString("base64");
    for (const answer of [
      { status: 403, body: { errorCode: "insufficient_scope", note: TOKEN } },
      { status: 200, body: { data: { commandResult: "Unknown command" } } },
      { status: 200, body: { data: {} } },
      { status: 200, body: null },
      { status: 200, body: { data: { commandResult: `${client}.ABCDEF0123456789` } } },
    ]) {
      const err = await failure(generateApiToken(api(answer).a));
      expect(err).toBeInstanceOf(Error);
      expect(err.message).not.toContain(TOKEN);
      expect(err.message).not.toContain("Unknown command");
    }
  });

  it("invalidates the application tokens at the end (server.InvalidateAPITokens), and says when that failed", async () => {
    const ok = api({ status: 200, body: { data: { commandResult: "" } } });
    await invalidateApiTokens(ok.a);
    expect(ok.calls).toEqual([{ fn: "RunCommand", data: { Command: "server.InvalidateAPITokens" } }]);
    await expect(invalidateApiTokens(api({ status: 500, body: null }).a)).rejects.toThrow(/InvalidateAPITokens.*500/);
  });
});
