// The command run (spec "Testing" 4, C9): the dashboard sets auto-pause through the mod. A flip applies and is seen by
// the game's own API, a command with a bad parameter fails as unsupported and changes nothing, and a restore puts the
// value back. Each command gets exactly one result post (the ledger), and the mod logs one Applied line per change.
// The bad command is `{ enabled: "yes" }`: the table's CHECK allows any object (1790985600000_agent_commands.sql), and
// the mod refuses a non-boolean (Commands.cpp, CommandFailed("unsupported")).
// apiAfter is GetServerOptions' auto-pause read once the command left pending/sent (null: it didn't answer);
// resultStatuses are the answers to every POST /agent/v1/commands/<id>/result the recorder saw for that command over the
// whole run. Controller ruling (2026-10-20, review of 9b23fd1): exactly one result per command ID is accepted (2xx); a
// correct mod retries a result refused transiently (DriverCommands.cpp), so extra posts refused with 5xx or 429 are
// allowed and counted in the pass reason. Any other refusal, or a second accepted post, fails.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["command", "C9"];
const APPLIED = /Applied set_auto_pause/;

export interface Outcome { id: string; status: string; resultCode: string | null; apiAfter: boolean | null; resultStatuses: number[] }
const isAccepted = (s: number) => s >= 200 && s < 300;
const isTransient = (s: number) => (s >= 500 && s < 600) || s === 429;
export interface Command {
  /** Auto-pause before the first command, from GetServerOptions. */
  before: boolean;
  /** null: the run never got to that command. */
  flipped: Outcome | null;
  unsupported: Outcome | null;
  restored: Outcome | null;
}

function judge(name: string, o: Outcome | null, want: { status: string; resultCode: string | null; autoPause: boolean }): string[] {
  if (!o) return [`the ${name} command never ran`];
  const out: string[] = [];
  if (o.status !== want.status) out.push(`${name}: status ${o.status}, not ${want.status}`);
  if (o.resultCode !== want.resultCode) out.push(`${name}: result code ${o.resultCode ?? "none"}, expected ${want.resultCode ?? "none"}`);
  if (o.apiAfter === null) out.push(`${name}: GetServerOptions didn't answer`);
  else if (o.apiAfter !== want.autoPause) out.push(`${name}: GetServerOptions reads auto-pause ${o.apiAfter}, expected ${want.autoPause}`);
  const accepted = o.resultStatuses.filter(isAccepted).length;
  if (accepted !== 1) out.push(`${name}: ${accepted} result post(s) accepted, not 1`);
  for (const s of new Set(o.resultStatuses.filter((s) => !isAccepted(s) && !isTransient(s)))) out.push(`${name}: a result post was answered ${s}, not 2xx or a transient 5xx/429`);
  return out;
}

export function evaluate(e: CoreEvidence): Verdict {
  const c = e.captures["command.json"] as Command | undefined;
  if (!c) return { pass: false, reasons: ["no command capture"] };
  const flippedTo = !c.before;
  const reasons = [
    ...judge("flip", c.flipped, { status: "succeeded", resultCode: null, autoPause: flippedTo }),
    // A refused command changes nothing: auto-pause stays where the flip left it.
    ...judge("unsupported", c.unsupported, { status: "failed", resultCode: "unsupported", autoPause: flippedTo }),
    ...judge("restore", c.restored, { status: "succeeded", resultCode: null, autoPause: c.before }),
  ];
  const applied = e.lines.filter((l) => APPLIED.test(l)).length;
  if (applied !== 2) reasons.push(`Applied set_auto_pause logged ${applied} times, not 2`);
  const onOff = (v: boolean) => (v ? "on" : "off");
  const all = [c.flipped, c.unsupported, c.restored].flatMap((o) => o?.resultStatuses ?? []);
  const transient = all.filter(isTransient).length;
  return reasons.length
    ? { pass: false, reasons }
    : {
        pass: true,
        reasons: [
          `auto-pause ${onOff(c.before)} -> ${onOff(flippedTo)} -> ${onOff(c.before)} as GetServerOptions read it; { enabled: "yes" } failed as unsupported and changed nothing; ` +
            `1 result accepted per command (3), ${all.length} result post(s) in all, ${transient} refused transiently (5xx/429) and retried; Applied set_auto_pause logged ${applied} times`,
        ],
      };
}
