// The parity run's second agent: satisfactory-dash's Node agent (agent/dist/agent.cjs, built from the contract pin), run
// as satis-runner against the same test server and, through the recorder, the same throwaway backend. Its CLI
// (satisfactory-dash agent/src/cli.ts at 81f22aef): `set-tokens [--host] [--api-port] [--frm-port]` asks for the game API
// token and then FRM's at a hidden prompt, which reads one line per prompt from a piped stdin (prompt.ts readHidden); the
// tokens are never accepted as arguments or environment variables (cli.ts:29). Then `check`, `enroll <CODE> --url
// <origin>` (http is allowed for loopback only, backendClient.ts:68-84), and `run` until stopped. The store lives in
// SD_AGENT_HOME (store.ts resolveDataDir) and its secrets are DPAPI-protected for the Windows user that ran set-tokens,
// so every step runs as the same user, in one throwaway folder deleted on stop.
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import type { RunnerConfig } from "../config";
import { LOOPBACK_HOSTS } from "../config";
import type { ServerApi } from "../server-api";
import { killTree as realKillTree } from "../server-control";

export interface AgentChild {
  pid: number;
  exited: Promise<number | null>;
  /** Writes text to the child's stdin and closes it. */
  end(stdin: string): void;
  /** The tail of what it printed (stdout and stderr), for a failure's message only. */
  output(): string;
}
export type AgentSpawner = (node: string, args: string[], env: NodeJS.ProcessEnv) => AgentChild;

export interface NodeAgentOptions {
  agentCjs: string;
  apiPort: number;
  frmPort: number;
  apiToken: string;
  frmToken?: string;
  /** The enrolment code seeded for the "node" server. */
  code: string;
  /** The recorder's URL (an origin on loopback). */
  backendUrl: string;
  /** Where the throwaway SD_AGENT_HOME is made (default: this account's temp folder). */
  tempRoot?: string;
  /** The Node that runs agent.cjs (default: the harness's own). */
  node?: string;
  spawn?: AgentSpawner;
  killTree?: (pid: number) => Promise<void>;
  /** How long set-tokens, check and enroll may take each. */
  stepTimeoutMs?: number;
}
export interface NodeAgent { pid: number; exited: Promise<number | null>; stop(): Promise<void> }

const OUTPUT_KEPT = 16 * 1024;
const CODE = /^[A-Z2-7]{4}-[A-Z2-7]{4}-[A-Z2-7]{4}$/;

export const realAgentSpawner: AgentSpawner = (node, args, env) => {
  const child = spawn(node, args, { env, stdio: ["pipe", "pipe", "pipe"], windowsHide: true });
  let out = "";
  const keep = (c: Buffer) => {
    out = (out + c.toString("utf8")).slice(-OUTPUT_KEPT);
  };
  child.stdout?.on("data", keep);
  child.stderr?.on("data", keep);
  const exited = new Promise<number | null>((resolve) => {
    child.once("exit", (code) => resolve(code));
    child.once("error", () => resolve(null));
  });
  if (child.pid === undefined) throw new Error("could not start the Node agent");
  child.stdin?.on("error", () => undefined); // a child that exits before reading its stdin
  return { pid: child.pid, exited, end: (text) => void child.stdin?.end(text), output: () => out };
};

const checkPort = (n: number, name: string) => {
  if (!Number.isInteger(n) || n < 1 || n > 65535) throw new Error(`${name} must be a port from 1 to 65535`);
};

/** The API port (apiBaseUrl) and FRM's (frmBaseUrl) the agent reads the test server on. */
export function agentPorts(config: Pick<RunnerConfig, "apiBaseUrl" | "frmBaseUrl">): { apiPort: number; frmPort: number } {
  if (!config.frmBaseUrl) throw new Error("the parity run needs frmBaseUrl in the runner config (the Node agent reads power from FRM)");
  const api = new URL(config.apiBaseUrl);
  const frm = new URL(config.frmBaseUrl);
  return { apiPort: Number(api.port || 443), frmPort: Number(frm.port || 80) };
}

/**
 * Sets the agent up and starts `run`. The tokens go only to set-tokens' stdin, one line each; nothing here logs, and a
 * failure's message carries the step, its exit code and the tail of its output with the tokens and the code removed.
 */
export async function startNodeAgent(o: NodeAgentOptions): Promise<NodeAgent> {
  const url = new URL(o.backendUrl);
  if (!/^https?:$/.test(url.protocol) || !LOOPBACK_HOSTS.has(url.hostname) || (url.pathname !== "/" && url.pathname !== "")) {
    throw new Error("the Node agent's backend must be an http(s) origin on loopback (the recorder)");
  }
  if (!CODE.test(o.code)) throw new Error("the Node agent's enrolment code is malformed");
  checkPort(o.apiPort, "the API port");
  checkPort(o.frmPort, "the FRM port");
  const spawnAgent = o.spawn ?? realAgentSpawner;
  const kill = o.killTree ?? realKillTree;
  const node = o.node ?? process.execPath;
  const timeoutMs = o.stepTimeoutMs ?? 120_000;
  const secrets = [o.apiToken, o.frmToken, o.code].filter((s): s is string => !!s);
  const scrub = (text: string) => secrets.reduce((t, s) => t.replaceAll(s, "<redacted>"), text);
  const tail = (text: string) => scrub(text).trim().split(/\r?\n/).slice(-8).join(" | ");

  const home = mkdtempSync(join(o.tempRoot ?? tmpdir(), "sd-agent-"));
  const env: NodeJS.ProcessEnv = { ...process.env, SD_AGENT_HOME: home };
  const removeHome = () => rmSync(home, { recursive: true, force: true });

  const step = async (name: string, args: string[], stdin = "") => {
    const child = spawnAgent(node, [o.agentCjs, name, ...args], env);
    child.end(stdin);
    let timer: ReturnType<typeof setTimeout> | undefined;
    const timedOut = new Promise<"timeout">((r) => (timer = setTimeout(() => r("timeout"), timeoutMs)));
    const code = await Promise.race([child.exited, timedOut]);
    clearTimeout(timer);
    if (code === "timeout") {
      await kill(child.pid);
      throw new Error(`the Node agent's ${name} did not finish within ${timeoutMs} ms`);
    }
    if (code !== 0) {
      const said = tail(child.output());
      throw new Error(`the Node agent's ${name} exited ${code}${said ? `: ${said}` : ""}`);
    }
  };

  try {
    await step("set-tokens", ["--host", "127.0.0.1", "--api-port", String(o.apiPort), "--frm-port", String(o.frmPort)], `${o.apiToken}\n${o.frmToken ?? ""}\n`);
    await step("check", []);
    await step("enroll", [o.code, "--url", url.origin]);
  } catch (e) {
    removeHome();
    throw e;
  }
  let run: AgentChild;
  try {
    run = spawnAgent(node, [o.agentCjs, "run"], env);
    run.end("");
  } catch (e) {
    removeHome();
    throw e;
  }
  let stopped = false;
  return {
    pid: run.pid,
    exited: run.exited,
    async stop() {
      if (stopped) return;
      stopped = true;
      await kill(run.pid);
      await Promise.race([run.exited, new Promise((r) => setTimeout(r, 10_000))]);
      removeHome();
    },
  };
}

const runCommand = (api: ServerApi, command: string) => api.call("RunCommand", { Command: command });

/** The payload's privilege level ("pl"), from the token's base64 JSON part (dedicated-server-api.md E5); null if unreadable. */
function privilege(payload: string): string | null {
  try {
    const json = JSON.parse(Buffer.from(payload.replace(/-/g, "+").replace(/_/g, "/"), "base64").toString("utf8")) as { pl?: unknown };
    return typeof json.pl === "string" ? json.pl : null;
  } catch {
    return null;
  }
}

/**
 * A fresh application token for the Node agent (a made-up one gets 401 even with AllowInsecureLocalAccess, dash session
 * 2026-10-21). `server.GenerateAPIToken` is a console command (dedicated-server-api.md E5); the HTTPS API runs one with
 * RunCommand (Administrator; FGServerManagementController.h:51-52, `Handler_RunCommand(const FString& Command,
 * FString& OutCommandResult)`), which insecure local access allows on loopback, as it allows ApplyServerOptions. Its
 * result is `data.commandResult`, by the API's naming (Out dropped, first letter lowered, as serverGameState and
 * serverOptions). The token is the "<base64 payload>.<hex fingerprint>" in that text whose payload says APIToken.
 * Nothing of the answer is ever put in an error.
 */
export async function generateApiToken(api: ServerApi): Promise<string> {
  const r = await runCommand(api, "server.GenerateAPIToken");
  if (r.status < 200 || r.status > 299) throw new Error(`RunCommand server.GenerateAPIToken answered ${r.status}`);
  const text = (r.body as { data?: { commandResult?: unknown } } | null)?.data?.commandResult;
  if (typeof text !== "string") throw new Error("RunCommand server.GenerateAPIToken returned no commandResult");
  for (const m of text.matchAll(/([A-Za-z0-9+/_-]+={0,2})\.([0-9A-Fa-f]{16,})/g)) {
    if (privilege(m[1]!) === "APIToken") return m[0];
  }
  throw new Error("RunCommand server.GenerateAPIToken returned no application token");
}

/** Prunes the test server's application tokens (`server.InvalidateAPITokens`, E5), so the run's token dies with it. */
export async function invalidateApiTokens(api: ServerApi): Promise<void> {
  const r = await runCommand(api, "server.InvalidateAPITokens");
  if (r.status < 200 || r.status > 299) throw new Error(`RunCommand server.InvalidateAPITokens answered ${r.status}`);
}
