# API sources (G2 anti-invention check)

Every SML, FactoryGame or Unreal Engine symbol, `#include` and module dependency used under `spikes/` has a row here.
`npm run harness -- check-api` (in CI, and before every spike run) fails on any use without a row.

- **Symbol:** as the code writes it. That's one of:
  - a type (`FHttpModule`);
  - a qualified name (`FHttpModule::Get`);
  - a macro (`UE_LOG`);
  - an include path as written (`HttpModule.h`);
  - a module dependency (`module:HTTP`).

  A method called through `->` or `.` is covered by any row ending in `::<Method>`.
- **Header:** `path:line` in the pinned SML or engine checkout (`harness/toolchain.json`) where the symbol is declared.
  Look it up; never write it from memory. If the docs and the header disagree, the header wins, and both are noted in
  Source.
- **Source:** the satisfactory-dash `docs-vault/raw-sources/` file that documents the symbol, or `header only` when no
  captured document covers it. In that case the spike's result is the only evidence.

| Symbol | Header | Source |
| --- | --- | --- |
