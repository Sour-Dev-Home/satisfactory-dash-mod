import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { parse } from "yaml";
import { CORE_RUNS } from "../src/core-runs";
import { LOCAL_ONLY, RUNNABLE } from "../src/run";

const WORKFLOWS = join(import.meta.dirname, "..", "..", ".github", "workflows");
type Job = { "runs-on"?: string | string[]; environment?: string | { name: string }; steps?: Array<{ uses?: string; run?: string }> };
type Workflow = { on: Record<string, unknown>; permissions?: unknown; jobs: Record<string, Job> };
const load = (file: string) => parse(readFileSync(join(WORKFLOWS, file), "utf8")) as Workflow;
const runsOn = (job: Job) => [job["runs-on"] ?? []].flat();

describe("mod-test.yml (the self-hosted runner's second door)", () => {
  const wf = load("mod-test.yml");

  it("holds the UE test-count floor at 186 (the suite has 186 tests after R7 step 3 deleted the FRM role; owner 2026-11-03)", () => {
    const steps = Object.values(wf.jobs).flatMap((j) => (j as unknown as { steps: Array<{ run?: string }> }).steps);
    expect(steps.some((s) => /test-mod .*--min 186\b/.test(s.run ?? ""))).toBe(true);
  });

  it("triggers only on workflow_dispatch and pushes to main, never on a pull request", () => {
    expect(Object.keys(wf.on).sort()).toEqual(["push", "workflow_dispatch"]);
    expect((wf.on.push as { branches: string[] }).branches).toEqual(["main"]);
  });

  it("reads contents only, on the ue runner behind the ue-runner environment", () => {
    expect(wf.permissions).toEqual({ contents: "read" });
    for (const job of Object.values(wf.jobs)) {
      expect(runsOn(job)).toEqual(expect.arrayContaining(["self-hosted", "windows", "ue"]));
      expect(typeof job.environment === "string" ? job.environment : job.environment?.name).toBe("ue-runner");
    }
  });
});

describe("linux-build.yml (the Linux server build on the self-hosted runner; plan 2026-11-06-linux-server-build, owner 2026-11-06)", () => {
  const wf = load("linux-build.yml");

  it("triggers only on workflow_dispatch and pushes to main, never on a pull request", () => {
    expect(Object.keys(wf.on).sort()).toEqual(["push", "workflow_dispatch"]);
    expect((wf.on.push as { branches: string[] }).branches).toEqual(["main"]);
  });

  it("reads contents only, on the ue runner behind the ue-runner environment", () => {
    expect(wf.permissions).toEqual({ contents: "read" });
    for (const job of Object.values(wf.jobs)) {
      expect(runsOn(job)).toEqual(expect.arrayContaining(["self-hosted", "windows", "ue"]));
      expect(typeof job.environment === "string" ? job.environment : job.environment?.name).toBe("ue-runner");
    }
  });

  it("builds both server platforms and checks them", () => {
    const steps = Object.values(wf.jobs).flatMap((j) => j.steps ?? []);
    expect(steps.some((s) => /build-mod .*--platforms Win64\+Linux\b/.test(s.run ?? ""))).toBe(true);
  });
  it("uploads the release zips (the merged multi-target zip and both per-platform zips)", () => {
    const steps = Object.values(wf.jobs).flatMap((j) => j.steps ?? []) as { uses?: string; with?: { path?: string } }[];
    expect(steps.some((s) => s.uses?.startsWith("actions/upload-artifact@") && s.with?.path?.endsWith("/linux-build/zips"))).toBe(true);
  });
});

describe("spike.yml (the self-hosted runner's only door)", () => {
  const wf = load("spike.yml");

  it("triggers only on workflow_dispatch and pushes to main", () => {
    expect(Object.keys(wf.on).sort()).toEqual(["push", "workflow_dispatch"]);
    expect((wf.on.push as { branches: string[] }).branches).toEqual(["main"]);
  });

  it("offers exactly the spikes this harness runs, except the local-only ones (S5)", () => {
    const input = (wf.on.workflow_dispatch as { inputs: { spike: { options: string[] } } }).inputs.spike;
    expect(input.options).toEqual(RUNNABLE.filter((s) => !LOCAL_ONLY.includes(s)));
  });

  it("reads contents only", () => {
    expect(wf.permissions).toEqual({ contents: "read" });
  });

  it("runs every job on the ue runner behind the ue-runner environment", () => {
    for (const job of Object.values(wf.jobs)) {
      expect(runsOn(job)).toEqual(expect.arrayContaining(["self-hosted", "windows", "ue"]));
      expect(typeof job.environment === "string" ? job.environment : job.environment?.name).toBe("ue-runner");
    }
  });

  it("never expands an input or event value inside a run script", () => {
    for (const job of Object.values(wf.jobs)) {
      for (const step of job.steps ?? []) expect(step.run ?? "").not.toMatch(/\$\{\{\s*(inputs|github\.event)/);
    }
  });
});

describe("core-run.yml (the core mod's runs on the self-hosted runner)", () => {
  const wf = load("core-run.yml");

  it("triggers only on workflow_dispatch: never on a pull request or a push", () => {
    expect(Object.keys(wf.on)).toEqual(["workflow_dispatch"]);
  });

  it("offers exactly the core runs this harness drives", () => {
    const input = (wf.on.workflow_dispatch as { inputs: { run: { options: string[] } } }).inputs.run;
    expect(input.options).toEqual(Object.keys(CORE_RUNS));
  });

  it("reads contents only, on the ue runner behind the ue-runner environment", () => {
    expect(wf.permissions).toEqual({ contents: "read" });
    for (const job of Object.values(wf.jobs)) {
      expect(runsOn(job)).toEqual(expect.arrayContaining(["self-hosted", "windows", "ue"]));
      expect(typeof job.environment === "string" ? job.environment : job.environment?.name).toBe("ue-runner");
    }
  });

  it("never expands an input or event value inside a run script", () => {
    for (const job of Object.values(wf.jobs)) {
      for (const step of job.steps ?? []) expect(step.run ?? "").not.toMatch(/\$\{\{\s*(inputs|github\.event)/);
    }
  });

  it("offers the host as a fixed choice, windows by default (Linux plan Task 7)", () => {
    const host = (wf.on.workflow_dispatch as { inputs: { host: { type: string; options: string[]; default: string } } }).inputs.host;
    expect(host).toMatchObject({ type: "choice", options: ["windows", "linux"], default: "windows" });
  });

  it("takes the Linux host's config only from the runner's SATIS_HARNESS_CONFIG_LINUX, and refuses without it", () => {
    const run = Object.values(wf.jobs).flatMap((j) => j.steps ?? []).map((s) => s.run ?? "").find((r) => r.includes("core-run")) ?? "";
    expect(run).toMatch(/\$env:HOST_KIND -eq 'linux'/);
    expect(run).toMatch(/\$env:SATIS_HARNESS_CONFIG = \$env:SATIS_HARNESS_CONFIG_LINUX/);
    expect(run).toMatch(/throw/);
  });
});

describe("every workflow", () => {
  const files = readdirSync(WORKFLOWS).filter((f) => /\.ya?ml$/.test(f));

  it("pins every action to a full commit SHA", () => {
    for (const f of files) {
      for (const job of Object.values(load(f).jobs)) {
        for (const step of job.steps ?? []) {
          if (step.uses) expect(step.uses, `${f}: ${step.uses}`).toMatch(/@[0-9a-f]{40}$/);
        }
      }
    }
  });

  it("keeps self-hosted runners out of every workflow but spike.yml, mod-test.yml, core-run.yml and linux-build.yml", () => {
    for (const f of files.filter((x) => !["spike.yml", "mod-test.yml", "core-run.yml", "linux-build.yml"].includes(x))) {
      for (const job of Object.values(load(f).jobs)) expect(runsOn(job), f).not.toContain("self-hosted");
    }
  });
});
