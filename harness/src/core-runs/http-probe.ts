import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import * as Pass from "../../../runs/http-probe/pass";
import { randomCode } from "../backend";
import type { CoreRunSpec } from "../core-run";
import { startHelloCapture, startProbeStub, startUntrustedTls } from "../probe-stub";

const MIN = 60_000;

export const httpProbe: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: false,
  world: false,
  evaluate: Pass.evaluate,
  async drive(ctx) {
    const certDir = mkdtempSync(join(tmpdir(), "satis-probe-"));
    const stub = await startProbeStub();
    const hello = await startHelloCapture();
    try {
      const untrusted = await startUntrustedTls(certDir);
      try {
        // 1. Normal: enrol, then three snapshots answered 200 by the URL asked.
        ctx.writeConfig({ backendUrl: stub.backendUrl, enrollmentCode: randomCode() });
        await ctx.waitFor("an enrol and three snapshots", () =>
          stub.requests.filter((r) => r.path === "/agent/v1/snapshots").length >= 3 ? true : undefined, 3 * MIN);
        // S8: the secret is searched for in the log whatever the agent later does with its file.
        ctx.noteSecret();
        // 2. S3: snapshots now get 302 to the other port; the mod retries with backoff (1, 2, 4, 8, 16 s ...).
        stub.startRedirecting();
        await ctx.deps.sleep(MIN);
        // 3. S2: a new backend URL and code make the mod enrol there at the next minute's config check.
        ctx.writeConfig({ backendUrl: hello.url, enrollmentCode: randomCode() });
        await ctx.waitFor("a ClientHello", () => (hello.hellos.length ? true : undefined), 2 * MIN);
        // 4. S1 and S9: a certificate nobody trusts, for another name.
        ctx.writeConfig({ backendUrl: untrusted.url, enrollmentCode: randomCode() });
        await ctx.waitFor("a refused handshake", () => (untrusted.handshakeFailures ? true : undefined), 2 * MIN);
        await ctx.deps.sleep(10_000);
      } finally {
        ctx.captures["probe.json"] = {
          redirectingSince: stub.redirectingSince(),
          requests: stub.requests,
          hellos: hello.hellos,
          untrusted: { url: untrusted.url, handshakeFailures: untrusted.handshakeFailures, requests: untrusted.requests },
        };
        await untrusted.close();
      }
    } finally {
      await Promise.all([stub.close(), hello.close()]);
      rmSync(certDir, { recursive: true, force: true });
    }
  },
};
