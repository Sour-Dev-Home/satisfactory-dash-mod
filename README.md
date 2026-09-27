# satisfactory-dash-mod

**This is an exploration, not a supported product.**

It is the spike repository for [ADR-0039](https://github.com/Sour-Dev-Home/satisfactory-dash/blob/main/docs-vault/wiki/decisions/0039-mod-as-end-state-research.md)
of [satisfactory-dash](https://github.com/Sour-Dev-Home/satisfactory-dash): a set of small, throwaway [SatisfactoryModLoader](https://github.com/satisfactorymodding/SatisfactoryModLoader)
(SML) mods that each test one narrow claim about whether a Satisfactory game mod could push data to a
satisfactory-dash backend, as an alternative to the [edge agent](https://github.com/Sour-Dev-Home/satisfactory-dash/blob/main/docs-vault/wiki/decisions/0031-edge-agent.md)
for players whose game server is rented and cannot run a second program beside it.

## Status

Exploration only (ADR-0039 gates G0-G2). There is **no build decision (G3)** without the owner's explicit go, and the
[edge agent](https://github.com/Sour-Dev-Home/satisfactory-dash) remains the supported way satisfactory-dash reads a
game server. Nothing here is installed on, or affects, any real Satisfactory server other than the owner's own, during
a spike he runs himself.

## What's here

- `spikes/S0` … `spikes/S5` — one folder per spike, each testing exactly one claim from
  [`mod-claims.md`](https://github.com/Sour-Dev-Home/satisfactory-dash/blob/main/docs-vault/wiki/mod-claims.md) in the
  main repo (the table of claims, sources and status lives there, not here). A spike's evidence (log excerpts, an echo
  capture, tick numbers, exact game and SML versions) is committed back to the main repo's
  `docs-vault/raw-sources/mod-spikes/<spike>-<date>/`, not here.
- No claim here is fact until it has both a source in the main repo's `docs-vault/raw-sources/` and a passing spike.

## Building

Needs the Unreal Engine 5.6.1 toolchain SML documents (see
[`sml-getting-started-2026-09-27.adoc`](https://github.com/Sour-Dev-Home/satisfactory-dash/blob/main/docs-vault/raw-sources/sml-getting-started-2026-09-27.adoc)
in the main repo for exact versions and steps). No CI builds this repo: UE builds do not fit free-tier runners, so every
spike is built and run on the owner's own PC against his own dedicated server.

## Secrets

Spikes never carry a real secret. S1/S1b/S2 talk only to a loopback echo server or the public read-only
`/api/health` endpoint; S5 targets a local or throwaway database, never production. See each spike's own notes once it
exists.

## Licence

GPL-3.0-or-later (see [`LICENSE`](./LICENSE)), because this links [SML](https://github.com/satisfactorymodding/SatisfactoryModLoader)
(GPL-3.0). This is a separate program from satisfactory-dash (AGPL-3.0-only): the two talk over HTTP, and neither
licence reaches the other.
