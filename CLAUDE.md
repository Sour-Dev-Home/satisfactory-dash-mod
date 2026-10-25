# satisfactory-dash-mod: rules for agents

This repo holds ADR-0039's G2 spikes and the harness that judges them. Its spec is
`docs/specs/2026-10-19-g2-harness-design.md`.

1. **No API from memory.** Before using any SML, FactoryGame or Unreal Engine symbol, include or module, open the header
   in the pinned checkout (`harness/toolchain.json`) and add its row to `api-sources.md`, with `path:line`.
   `check-api` fails the build otherwise.
2. **The header wins.** If the docs and the header disagree, follow the header and record both in the row's Source.
3. **A failed spike stops the work.** Report it with the evidence bundle. Never weaken a pass condition
   (`spikes/S#/pass.ts`, `runs/*/pass.ts`) or work around a failure to make a spike pass.
4. **Evidence is machine-written.** Never hand-edit a `result.json` or a capture.
5. **Loopback and the test server only.** Spikes talk to `127.0.0.1` (the S1b health check is the one exception the
   claims table allows). They never touch the owner's real server or production.
6. Plain PRs to `main`, merged by the owner. No PII or local paths (`C:\Users\...`) in anything committed.
7. **Clean room (ADR-0058).** Never open FicsitRemoteMonitoring's source in any form, and never search the web for it. Native readers follow `docs/clean-room.md`: allowed sources only, our own names, a `PROVENANCE.md` per reader (CI checks it), and the PR checkbox "FRM source was not opened". If you see FRM source by accident, stop and tell the owner.
