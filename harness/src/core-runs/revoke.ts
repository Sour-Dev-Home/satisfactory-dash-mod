import * as Pass from "../../../runs/revoke/pass";
import type { CoreRunSpec } from "../core-run";
import { satisDashLines } from "../core-run";
import { randomCode } from "../backend";
import { startRecorder, type RecordedCall } from "../recorder";
import { accepted, enrolPhase } from "./enrol";

const MIN = 60_000;
const ENROLL = "/agent/v1/enroll";
const bodyless = ({ snapshot: _, ...rest }: RecordedCall): RecordedCall => rest;

/**
 * The revoke run (no world): enrol, revoke, watch the halt and the silence after it, then enrol again on the same server
 * with a code issued after the revoke (revoking deletes any open code; runs/revoke/pass.ts header).
 */
export const revoke: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: false,
  emptyBoot: true,
  evaluate: Pass.evaluate,
  async drive(ctx) {
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl);
    const capture: Pass.Revoke = {
      revokedAt: "", halts: [], reseededAt: null, afterRevoke: [], credentialAfterHalt: null, reenrolled: { enrolStatus: null, acceptedAfter: 0 },
    };
    // Each halt line gets the time the harness first saw it (the log is read every second).
    const scanHalts = () => {
      const n = satisDashLines(ctx.deps.readLogSince(ctx.offset)).filter((l) => l.includes(Pass.HALT_LINE)).length;
      while (capture.halts.length < n) capture.halts.push(new Date().toISOString());
    };
    try {
      await enrolPhase(ctx, host, recorder);
      await ctx.waitFor("3 accepted snapshots", () => (accepted(recorder).length >= 3 ? true : undefined), 2 * MIN);
      await ctx.sampleMemory("withMod");
      // Noted before the call, so the halt's delay is never counted short.
      capture.revokedAt = new Date().toISOString();
      await host.revoke("mod");
      await ctx.waitFor("the halt line", () => (scanHalts(), capture.halts.length ? true : undefined), 30_000);
      // Two more minutes: whatever reaches the recorder now is judged (nothing may, after the first 401).
      const quietUntil = Date.now() + 2 * MIN;
      while (Date.now() < quietUntil) {
        scanHalts();
        await ctx.deps.sleep(1000);
      }
      scanHalts();
      capture.credentialAfterHalt = ctx.credentialExists();

      // A code issued after the revoke (the revoke deleted any open one), for the same server.
      const code = randomCode();
      capture.reseededAt = new Date().toISOString();
      await host.seedCode("mod", code);
      ctx.writeConfig({ backendUrl: recorder.url, enrollmentCode: code });
      const since = capture.reseededAt;
      await ctx.waitFor(
        "the re-enrolment and 3 accepted snapshots after it",
        () => {
          const enrol = recorder.entries.find((e) => e.agent === "mod" && e.path === ENROLL && e.at >= since);
          return enrol && accepted(recorder).filter((e) => e.at > enrol.at).length >= 3 ? true : undefined;
        },
        3 * MIN,
      );
      ctx.noteSecret(); // the new secret is searched for in the log too
      await ctx.sampleMemory("withModAfterRounds");
    } finally {
      scanHalts();
      if (capture.revokedAt) {
        const from = capture.revokedAt;
        const until = capture.reseededAt;
        // By when each was answered: a request that started just before the revoke can carry the halting 401.
        capture.afterRevoke = recorder.entries.filter((e) => e.doneAt >= from && (until === null || e.at < until)).map(bodyless);
      }
      if (capture.reseededAt) {
        const since = capture.reseededAt;
        const enrol = recorder.entries.find((e) => e.agent === "mod" && e.path === ENROLL && e.at >= since);
        capture.reenrolled = {
          enrolStatus: enrol ? enrol.status : null,
          acceptedAfter: enrol ? accepted(recorder).filter((e) => e.at > enrol.at).length : 0,
        };
      }
      if (capture.revokedAt) ctx.captures["revoke.json"] = capture;
      ctx.captures["recorder.json"] = recorder.entries.map(bodyless);
      await recorder.close();
    }
  },
};
