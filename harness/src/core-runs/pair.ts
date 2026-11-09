import * as Pass from "../../../runs/pair/pass";
import type { CoreRunSpec } from "../core-run";
import { satisDashLines } from "../core-run";
import { publicEntries, startRecorder } from "../recorder";
import { accepted } from "./enrol";

const MIN = 60_000;
const CODE_LINE = /Pairing code ([A-Z2-7]{4}-[A-Z2-7]{4}-[A-Z2-7]{4}):/;

/**
 * The pair run (spec 2026-11-09 §4): the mod boots with the loopback cfg and no code, so it pairs on its own against the
 * recorder; the drive reads the code from the log, approves it as the operator, and waits for an accepted snapshot.
 */
export const pair: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: false,
  emptyBoot: false,
  evaluate: Pass.evaluate,
  async drive(ctx) {
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl, { keepPairingSecrets: true });
    const log = () => satisDashLines(ctx.deps.readLogSince(ctx.offset));
    const capture: Pass.Pair = {
      bootAt: "", codeLineAt: null, userCode: null, codeCount: 0, approvedAt: null, approveStatus: null, pairedLine: null,
      secretsChecked: 0, credentialWritten: false, calls: [], secretSeen: { inLog: false, inUrl: false, inHeader: false },
    };
    try {
      capture.bootAt = new Date().toISOString();
      ctx.writeConfig({ backendUrl: recorder.url, enrollmentCode: "" });
      const code = await ctx.waitFor(
        "a pairing code in the log",
        () => {
          const m = log().map((l) => CODE_LINE.exec(l)).find((x) => x !== null);
          // When the harness saw the line, up to one waitFor interval (1 s) after it was logged, so the 30 s rule is conservative.
          if (m) capture.codeLineAt = new Date().toISOString();
          return m ? m[1] : undefined;
        },
        90_000,
      );
      capture.userCode = code;
      capture.approvedAt = new Date().toISOString();
      capture.approveStatus = (await host.approvePairing("pair", code)).status;
      await ctx.waitFor("an accepted snapshot", () => (accepted(recorder).length ? true : undefined), 3 * MIN);
      ctx.noteSecret(); // the new credential's secret is searched for in the log too
      // A little longer, so a pairing call that should not happen after the first snapshot would be recorded.
      await ctx.deps.sleep(15_000);
      // Read only on the success path: after a timeout these stay null/false, and the run fails anyway.
      capture.pairedLine = log().find((l) => l.includes("Paired with the dashboard")) ?? null;
      capture.credentialWritten = ctx.credentialExists();
    } finally {
      const lines = log();
      capture.codeCount = new Set(lines.map((l) => CODE_LINE.exec(l)?.[1]).filter((c) => c !== undefined)).size;
      // The poll secrets live in memory only. Only booleans reach the capture, never a secret.
      const secrets = [...new Set(recorder.entries.map((e) => e.pairingSecret).filter((s): s is string => !!s))];
      capture.secretsChecked = secrets.length;
      const text = ctx.deps.readLogSince(ctx.offset);
      capture.secretSeen = {
        inLog: secrets.some((s) => text.includes(s)),
        inUrl: recorder.entries.some((e) => secrets.some((s) => (e.rawUrl ?? "").includes(s))),
        inHeader: recorder.entries.some((e) => secrets.some((s) => (e.headerValues ?? "").includes(s))),
      };
      capture.calls = recorder.entries
        .filter((e) => e.agent === "mod")
        .map(({ path, status, at, doneAt }) => ({ path, status, at, doneAt }));
      ctx.captures["pair.json"] = capture;
      ctx.captures["recorder.json"] = publicEntries(recorder.entries); // no secrets, raw URLs or header values
      await recorder.close();
    }
  },
};
