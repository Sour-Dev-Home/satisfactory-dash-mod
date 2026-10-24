# satisfactory-dash-mod

**This is an exploration, not a supported product.**

It is the spike repository for [ADR-0039](https://github.com/Sour-Dev-Home/satisfactory-dash/blob/main/docs-vault/wiki/decisions/0039-mod-as-end-state-research.md)
of [satisfactory-dash](https://github.com/Sour-Dev-Home/satisfactory-dash): a set of small, throwaway [SatisfactoryModLoader](https://github.com/satisfactorymodding/SatisfactoryModLoader)
(SML) mods that each test one narrow claim about whether a Satisfactory game mod could push data to a
satisfactory-dash backend, as an alternative to the [edge agent](https://github.com/Sour-Dev-Home/satisfactory-dash/blob/main/docs-vault/wiki/decisions/0031-edge-agent.md)
for players whose game server is rented and cannot run a second program beside it.

## Status

G2's spikes passed, and the owner went on to G3: the **core mod, v1** (`mod/SatisDashAgent`, spec
`docs/specs/2026-10-20-core-mod-v1-design.md`). It is built and proven on the test server, but not yet released: the
[edge agent](https://github.com/Sour-Dev-Home/satisfactory-dash) stays the supported way satisfactory-dash reads a game
server until the switchover (sub-project 5), and nothing here is installed on any real server but the owner's own.

## The core mod, v1

`SatisDashAgent` runs inside a dedicated server and replaces the Node edge agent for status, power and auto-pause. It
is an outbound HTTPS client only: it opens no port, serves nothing and has no UI. It enrols once with a code from the
dashboard, pushes a snapshot every 5 s, and long-polls for commands (only `set_auto_pause` in v1).

- **Config:** `FactoryGame/Configs/SatisDashAgent.cfg`, written with defaults on first start:
  `{"backendUrl": "https://api.satis-manager.com", "enrollmentCode": ""}`. Put the dashboard's code in
  `enrollmentCode`; the mod blanks it once enrolled. Without a valid config the mod stays idle.
  Optional: `frmPort` (FRM's HTTP port on this server, default 8080) and `frmToken` (only if this server's FRM
  requires one; sent only to FRM on 127.0.0.1). The mod logs "FRM requires a token" when FRM refuses it.
- **Credential:** `FactoryGame/Saved/SatisDash/credential.json`, the per-server secret. Revoke it from the dashboard.
- **Logs:** `LogSatisDash`, Display and Warning only; never the secret or the code.
- **Evidence:** every harness run's verdict, the security review and its decisions are listed in Plan 2B's final PR;
  the review is `docs/reviews/2026-10-core-mod-v1-security.md`, with `docs/reviews/2026-10-core-mod-v1-security-decisions.md`.
  `docs/runner.md` explains how the runs work.

## What's here

- `mod/SatisDashAgent` — the core mod: `SatisDashCore` (pure logic and its tests) and `SatisDashGame` (transport,
  game reader, driver).
- `harness/` and `runs/` — the harness that builds, installs and judges the mod on the test server, and each run's
  pass condition.
- `spikes/S0` … `spikes/S5` — one folder per spike, each testing exactly one claim from
  [`mod-claims.md`](https://github.com/Sour-Dev-Home/satisfactory-dash/blob/main/docs-vault/wiki/mod-claims.md) in the
  main repo (the table of claims, sources and status lives there, not here). A spike's evidence (log excerpts, an echo
  capture, tick numbers, exact game and SML versions) is committed back to the main repo's
  `docs-vault/raw-sources/mod-spikes/<spike>-<date>/`, not here.
- No claim here is fact until it has both a source in the main repo's `docs-vault/raw-sources/` and a passing spike.

## Building

Needs the Unreal Engine 5.6.1 toolchain SML documents (see
[`sml-getting-started-2026-09-27.adoc`](https://github.com/Sour-Dev-Home/satisfactory-dash/blob/main/docs-vault/raw-sources/sml-getting-started-2026-09-27.adoc)
in the main repo for exact versions and steps). UE builds do not fit free-tier runners, so the mod and the spikes are
built and run by a self-hosted runner on the owner's PC, against a separate test server, and every run waits for the
owner's approval (`docs/runner.md`). GitHub's runners only run the harness's own tests.

## Secrets

Spikes never carry a real secret. S1/S1b/S2 talk only to a loopback echo server or the public read-only
`/api/health` endpoint; S5 targets a local or throwaway database, never production. See each spike's own notes once it
exists.

## Licence

satisfactory-dash-mod

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License along with this program; the unmodified
license text is in [`LICENSE`](./LICENSE), and the copyright holder is named in [`NOTICE`](./NOTICE). If
not, see <https://www.gnu.org/licenses/>.

SPDX-License-Identifier: GPL-3.0-or-later

GPL-3.0-or-later because this links [SML](https://github.com/satisfactorymodding/SatisfactoryModLoader)
(GPL-3.0). This is a separate program from satisfactory-dash (AGPL-3.0-only): the two talk over HTTP, and
neither licence reaches the other. Every source file added later carries the same
`SPDX-License-Identifier: GPL-3.0-or-later` header.
