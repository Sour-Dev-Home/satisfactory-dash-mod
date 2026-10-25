# Clean room: native readers

The mod reads the game itself instead of calling FicsitRemoteMonitoring (FRM). FRM states no licence, so nobody who
writes a native reader reads FRM's code (ADR-0058 in the satisfactory-dash repository, Decision 6, with the owner's
answer 4 of 2026-10-25: there is no similarity-check job; these rules and the provenance file are the control).

## The rules (verbatim from ADR-0058, Decision 6)

1. **Never open FRM's repository files.** Not on GitHub, not in a clone, not through code search, not in a cache, not
   in a release archive, and not FRM's installed plugin files or packaged assets on any machine. This includes its
   `Source`, `Content`, `JSON`, `Patches`, `www` and `docs` folders and its issues' code snippets.
2. **Allowed sources only:** the game's own headers shipped with the SML starter project; SML's documentation; exports of
   the *game's* assets made with FModel or similar (never FRM's); our own captures, including FRM's HTTP *output* from a
   running server, used as a black box; FRM's published API documentation pages at `docs.ficsit.app`, for endpoint and
   field names only (as ADR-0055 already does); the Unreal Engine reference.
3. **Our names come from the game or from our contract,** never from FRM's response field names. Internal variables,
   types and file names follow the game's property names and the `/agent/v1` schemas. Where FRM's output is the only
   spelling of a value (an enum string passed through, such as a train `Status`), the mapping table is written from
   observed output, not from FRM's code.
4. **If someone sees FRM source by accident** (a search hit, a pasted snippet): stop, tell the owner in the mod session,
   record it in the provenance file, and the reader it touched is rewritten by a different session working from headers
   only. The exposed session does not touch that reader again.
5. **Provenance record per reader,** a file in the mod repo beside the reader: the header files and lines used (the
   survey note's name), the SML pages, the captures used as an oracle, the author session or subagent, the date, and the
   line "FRM source was not opened". The reader's PR template carries the same checkbox, and CI fails a reader directory
   without its provenance file.

## Layout and the provenance file

- A native reader is a directory `Native/<Reader>/` under `mod/`. Pure code (testable by `SatisDashCoreTests`) lives in
  `SatisDashCore/Public/SatisDash/Native/<Reader>/` and `SatisDashCore/Private/Native/<Reader>/`; game-thread code may
  stay in `GameReader.cpp`. Nothing but reader directories sits directly inside a `Native/` directory.
- Each reader has exactly one `PROVENANCE.md`, in any one of its directories, with these lines (one value per line; `Code:`,
  `Headers:` and `Captures:` may repeat):

  ```
  Reader: <the directory name>
  Code: <repo-relative path of a file that implements the reader; it must exist>
  Headers: <header path:line, from the pinned SML or engine checkout>
  Survey: <the header-survey note's name>
  Captures: <FRM HTTP output files used as an oracle, or "none">
  Author: <session or subagent>
  Date: <YYYY-MM-DD>

  FRM source was not opened
  ```

- `cd harness; npm run harness -- check-provenance` runs the check; CI runs it on every push and pull request.
- The same symbols still need their `api-sources.md` rows (`check-api`).
