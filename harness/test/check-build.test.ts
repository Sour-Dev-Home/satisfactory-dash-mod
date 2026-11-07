import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { checkBuild } from "../src/check-build";
import { main } from "../src/cli";

const dirs: string[] = [];
const root = () => {
  const d = mkdtempSync(join(tmpdir(), "build-"));
  dirs.push(d);
  return d;
};
afterEach(() => dirs.splice(0).forEach((d) => rmSync(d, { recursive: true, force: true })));

const put = (base: string, path: string, text = "x") => {
  mkdirSync(dirname(join(base, path)), { recursive: true });
  writeFileSync(join(base, path), text);
};
const uplugin = (version: string) => JSON.stringify({ SemVersion: version });

/** Both platforms' trees, as build-mod.ps1 -Platforms Win64+Linux unpacks them. */
const good = (version = "0.2.0", linuxVersion = version) => {
  const r = root();
  put(r, "WindowsServer/SatisDashAgent.uplugin", uplugin(version));
  put(r, "WindowsServer/Binaries/Win64/FactoryServer-SatisDashCore-Win64-Shipping.dll");
  put(r, "WindowsServer/Binaries/Win64/FactoryServer-SatisDashGame-Win64-Shipping.dll");
  put(r, "LinuxServer/SatisDashAgent.uplugin", uplugin(linuxVersion));
  put(r, "LinuxServer/Binaries/Linux/libFactoryServer-SatisDashCore-Linux-Shipping.so");
  put(r, "LinuxServer/Binaries/Linux/libFactoryServer-SatisDashGame-Linux-Shipping.so");
  return r;
};

describe("checkBuild", () => {
  it("passes both platforms' trees with each module's library and one version", () => {
    expect(checkBuild(good())).toEqual([]);
  });

  it("fails a Linux tree without SatisDashGame's library", () => {
    const r = good();
    rmSync(join(r, "LinuxServer/Binaries/Linux/libFactoryServer-SatisDashGame-Linux-Shipping.so"));
    expect(checkBuild(r)).toEqual(["LinuxServer: no SatisDashGame library (.so)"]);
  });

  it("fails a Windows tree without SatisDashCore's library", () => {
    const r = good();
    rmSync(join(r, "WindowsServer/Binaries/Win64/FactoryServer-SatisDashCore-Win64-Shipping.dll"));
    expect(checkBuild(r)).toEqual(["WindowsServer: no SatisDashCore library (.dll)"]);
  });

  it("fails a missing platform tree", () => {
    const r = good();
    rmSync(join(r, "LinuxServer"), { recursive: true });
    expect(checkBuild(r)).toEqual(["LinuxServer: no SatisDashAgent.uplugin"]);
  });

  it("fails test code in either tree, naming the file", () => {
    const r = good();
    put(r, "LinuxServer/Binaries/Linux/libFactoryServer-SatisDashCoreTests-Linux-Shipping.so");
    expect(checkBuild(r)).toEqual(["LinuxServer: test code shipped: Binaries/Linux/libFactoryServer-SatisDashCoreTests-Linux-Shipping.so"]);
  });

  it("fails trees whose versions differ", () => {
    expect(checkBuild(good("0.2.0", "0.1.0"))).toEqual(["the platforms' versions differ: WindowsServer 0.2.0, LinuxServer 0.1.0"]);
  });
});

describe("harness check-build", () => {
  const io = () => {
    const out = { log: [] as string[], error: [] as string[] };
    return { out, io: { log: (s: string) => out.log.push(s), error: (s: string) => out.error.push(s) } };
  };

  it("exits 0 on a good build and 1 listing each problem", async () => {
    const ok = io();
    expect(await main(["check-build", good()], ok.io)).toBe(0);
    expect(ok.out.log).toEqual(["check-build: 0 problems"]);
    const bad = io();
    const r = good();
    rmSync(join(r, "LinuxServer"), { recursive: true });
    expect(await main(["check-build", r], bad.io)).toBe(1);
    expect(bad.out.error).toEqual(["LinuxServer: no SatisDashAgent.uplugin"]);
  });

  it("exits 2 without a folder", async () => {
    expect(await main(["check-build"], io().io)).toBe(2);
  });
});
