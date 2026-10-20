import { readFileSync } from "node:fs";
import { join } from "node:path";
import { z } from "zod";

/**
 * UE's test-pass summary: FAutomatedTestPassResults (Engine/Source/Developer/AutomationController/Private/
 * AutomationControllerManager.h:110-147), written by FJsonObjectConverter::UStructToJsonObjectString to
 * <ReportExportPath>/index.json (AutomationControllerManager.cpp:670-672). The converter lower-cases the first letter
 * of each property name. Other fields (devices, tests, durations) are ignored.
 */
export const AutomationReportSchema = z.object({
  succeeded: z.number().int().min(0),
  succeededWithWarnings: z.number().int().min(0),
  failed: z.number().int().min(0),
  notRun: z.number().int().min(0),
  inProcess: z.number().int().min(0),
});
export type AutomationReport = z.infer<typeof AutomationReportSchema>;

/** index.json is written by FFileHelper::SaveStringToFile, which picks UTF-16 with a BOM for non-ASCII text. */
export function decodeReport(bytes: Buffer): string {
  if (bytes[0] === 0xff && bytes[1] === 0xfe) return bytes.subarray(2).toString("utf16le");
  if (bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) return bytes.subarray(3).toString("utf8");
  return bytes.toString("utf8");
}

export function readAutomationReport(dir: string): AutomationReport {
  return AutomationReportSchema.parse(JSON.parse(decodeReport(readFileSync(join(dir, "index.json")))));
}

/** A pass needs no failure, nothing left unrun, and at least `minTests` passed (with or without warnings). */
export function judgeAutomationReport(r: AutomationReport, minTests: number): { pass: boolean; reason: string } {
  const passed = r.succeeded + r.succeededWithWarnings;
  if (r.failed > 0) return { pass: false, reason: `${r.failed} failed, ${passed} passed` };
  if (r.notRun > 0 || r.inProcess > 0) return { pass: false, reason: `${r.notRun} not run, ${r.inProcess} unfinished` };
  if (passed < minTests) return { pass: false, reason: `${passed} passed, fewer than the ${minTests} expected` };
  return { pass: true, reason: `${passed} passed` };
}
