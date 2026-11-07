// build-mod.ps1 -DryRun prints the PackagePlugin command line and builds nothing (plan 2026-11-06-linux-server-build, Task 2).
import { execFileSync } from "node:child_process";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { REPO_ROOT } from "../src/cli";

const dryRun = (...extra: string[]) =>
  execFileSync("pwsh", ["-NoProfile", "-File", join(REPO_ROOT, "scripts", "build-mod.ps1"), "-Project", "P", "-Engine", "E", "-Out", "O", "-DryRun", ...extra], { encoding: "utf8", stdio: "pipe" });

describe("build-mod.ps1 -DryRun", () => {
  it("packages the Windows server only by default, as before", () => {
    const line = dryRun();
    expect(line).toContain("PackagePlugin");
    expect(line).toContain("-serverplatform=Win64 ");
    expect(line).toContain("-noclient");
    expect(line).not.toContain("-platform=");
  });

  it("packages the Windows and Linux servers with -Platforms Win64+Linux", () => {
    const line = dryRun("-Platforms", "Win64+Linux");
    expect(line).toContain("-serverplatform=Win64+Linux ");
    expect(line).toContain("-noclient");
    expect(line).not.toContain("-platform=");
  });

  it("refuses any other platform set", () => {
    expect(() => dryRun("-Platforms", "Linux")).toThrow();
  });
});
