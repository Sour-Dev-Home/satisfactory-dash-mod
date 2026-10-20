import { connect } from "node:net";
import { afterEach, describe, expect, it } from "vitest";
import { delimiter, join } from "node:path";
import { FAKE_SECRET, findOpenssl, parseClientHello, startHelloCapture, startProbeStub, type ProbeStub } from "../src/probe-stub";

/** A minimal ClientHello record: legacy version, empty session id, one suite, null compression, optional supported_versions. */
function hello(legacy: number, versions?: number[]): Buffer {
  const ext = versions
    ? Buffer.concat([Buffer.from([0x00, 0x2b]), u16(1 + versions.length * 2), Buffer.from([versions.length * 2]), ...versions.map(u16)])
    : Buffer.alloc(0);
  const body = Buffer.concat([u16(legacy), Buffer.alloc(32), Buffer.from([0]), u16(2), u16(0x1301), Buffer.from([1, 0]), u16(ext.length), ext]);
  const handshake = Buffer.concat([Buffer.from([0x01]), u24(body.length), body]);
  return Buffer.concat([Buffer.from([0x16, 0x03, 0x01]), u16(handshake.length), handshake]);
}
const u16 = (n: number) => Buffer.from([(n >> 8) & 0xff, n & 0xff]);
const u24 = (n: number) => Buffer.from([(n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff]);

describe("findOpenssl", () => {
  const win = process.platform === "win32";
  const bin = win ? "openssl.exe" : "openssl";
  const GIT_USR = "C:\\Program Files\\Git\\usr\\bin\\openssl.exe";
  const GIT_MINGW = "C:\\Program Files\\Git\\mingw64\\bin\\openssl.exe";
  const only = (...present: string[]) => (p: string) => present.includes(p);

  it("prefers SATIS_OPENSSL when the file exists, and skips it when it doesn't", () => {
    const env = { SATIS_OPENSSL: "/opt/o/openssl", PATH: "/usr/bin" };
    expect(findOpenssl(env, only("/opt/o/openssl", join("/usr/bin", bin)))).toBe("/opt/o/openssl");
    expect(findOpenssl(env, only(join("/usr/bin", bin)))).toBe(join("/usr/bin", bin));
  });
  it("takes the first match on PATH", () => {
    const env = { PATH: ["/a", "/b", "/c"].join(delimiter) };
    expect(findOpenssl(env, only(join("/b", bin), join("/c", bin)))).toBe(join("/b", bin));
  });
  it.runIf(win)("falls back to Git for Windows' folders, usr/bin first", () => {
    expect(findOpenssl({ PATH: "" }, only(GIT_USR, GIT_MINGW))).toBe(GIT_USR);
    expect(findOpenssl({ PATH: "" }, only(GIT_MINGW))).toBe(GIT_MINGW);
  });
  it("is null when nothing exists", () => {
    expect(findOpenssl({ PATH: "/a" }, () => false)).toBeNull();
    expect(findOpenssl({}, () => false)).toBeNull();
  });
});

describe("parseClientHello", () => {
  it("reads supported_versions and drops GREASE values", () => {
    expect(parseClientHello(hello(0x0303, [0x0a0a, 0x0304, 0x0303]))).toEqual({ legacyVersion: 0x0303, supportedVersions: [0x0304, 0x0303] });
  });
  it("says the versions are unknown without the extension: the legacy version is the maximum, not the floor", () => {
    expect(parseClientHello(hello(0x0303))).toEqual({ legacyVersion: 0x0303, supportedVersions: null });
  });
  it("refuses what isn't a ClientHello", () => {
    expect(parseClientHello(Buffer.from("GET / HTTP/1.1\r\n\r\n"))).toBeNull();
    expect(parseClientHello(hello(0x0303, [0x0304]).subarray(0, 20))).toBeNull();
  });
});

describe("startProbeStub", () => {
  let stub: ProbeStub | undefined;
  afterEach(async () => void (await stub?.close()));

  it("enrols, answers snapshots, then redirects them to the other port, recording only what's safe", async () => {
    stub = await startProbeStub();
    const enrol = await fetch(`${stub.backendUrl}/agent/v1/enroll`, { method: "POST", headers: { "user-agent": "satisfactory-dash-mod/0.1.0" }, body: "{}" });
    expect(enrol.status).toBe(201);
    expect(((await enrol.json()) as { agentSecret: string }).agentSecret).toBe(FAKE_SECRET);
    const ok = await fetch(`${stub.backendUrl}/agent/v1/snapshots`, { method: "POST", headers: { authorization: `Bearer ${FAKE_SECRET}` }, body: '{"observedAt":"2026-10-20T00:00:00.000Z"}' });
    expect(ok.status).toBe(200);
    stub.startRedirecting();
    const moved = await fetch(`${stub.backendUrl}/agent/v1/snapshots`, { method: "POST", body: "{}", redirect: "manual" });
    expect(moved.status).toBe(302);
    expect(moved.headers.get("location")).toBe(`${stub.elsewhereUrl}/agent/v1/snapshots`);
    await fetch(`${stub.elsewhereUrl}/agent/v1/snapshots`, { method: "POST", headers: { authorization: "Bearer x" }, body: "{}" });
    expect(stub.requests.map((r) => [r.port, r.path, r.authorization])).toEqual([
      ["backend", "/agent/v1/enroll", false],
      ["backend", "/agent/v1/snapshots", true],
      ["backend", "/agent/v1/snapshots", false],
      ["elsewhere", "/agent/v1/snapshots", true],
    ]);
    expect(stub.requests[1]!.observedAt).toBe("2026-10-20T00:00:00.000Z");
    expect(JSON.stringify(stub.requests)).not.toContain(FAKE_SECRET);
  });
});

describe("startHelloCapture", () => {
  it("records each ClientHello and closes the connection", async () => {
    const capture = await startHelloCapture();
    const socket = connect(Number(new URL(capture.url).port), "127.0.0.1");
    await new Promise<void>((r) => socket.on("connect", () => r()));
    socket.write(hello(0x0303, [0x0304, 0x0303]));
    await new Promise<void>((r) => socket.on("close", () => r()));
    expect(capture.hellos.map((h) => h.supportedVersions)).toEqual([[0x0304, 0x0303]]);
    await capture.close();
  });
});
