# R7 plan: remove the FRM dependency from the mod (ADR-0058)

Status: **scope A confirmed by the owner (2026-11-02, "A as recommended").** D2-D7 take the recommended defaults below unless the owner changes them.
Inventory: a read-only pass on 2026-11-02, mod `main` 1c26359 and dash `main`. Paths are repo-relative.
Counts are file counts from a text search for "frm", so they include docs and fixtures.

## Scope (decision D1)

ADR-0058 Decision 4 (R7) covers the **mod**. Decision 5 and owner answer 2 keep the dash's Node agent and its FRM adapter (`packages/game-adapter`) on FRM, frozen, for hosts that run FRM themselves, until a later ADR retires them.

- **A (confirmed): R7 as the ADR says.** The mod stops talking to FRM and loses every FRM code path and setting. The dash stops its direct FRM reads only where the mod now sends the data. The Node agent, the game adapter, the local poller and the per-server FRM settings stay for hosts without the mod, frozen.
- **B (not chosen): remove FRM everywhere.** This would also retire the game adapter (about 15 endpoints), the Node agent, the local poller, the FRM DB columns, the UI and the contracts. Hosts without the mod would lose all data, and it would need an ADR amendment first. If B is ever wanted, it gets its own ADR after A ships.

The rest of this plan is scope A.

## Inventory

### Mod: the FRM dependency is runtime HTTP only

- **Packaging:** `SatisDashAgent.uplugin` lists only SML, and no `*.Build.cs` names FRM, so there's no manifest or module dependency to remove.
- **HTTP client:**
  - `Private/Frm.cpp` builds `http://127.0.0.1:<port>/<endpoint>`.
  - `Private/FrmQueue.cpp` sends it, with the token header and backoff.
  - Headers: `Public/SatisDash/Frm.h`, `FrmQueue.h`.
- **`FFrmBridge` is not bridge-only** (`Public/SatisDash/FrmBridge.h`, `Private/FrmBridge.cpp`). The native path also runs through it:
  - `SubmitNativeWorld/MapLive/Factory/Extractors/Generators/Storage`;
  - `EncodeSnapshot`, `Tick`, `TakeEncoded`, `EncodePending`;
  - called from `Driver.cpp` and `WorldSender.cpp`.
  
  Its snapshot-encode and part-holding role has to be split out before the FRM request role can be deleted. **This is the main risk of R7.**
- **FRM-shaped input readers:** `Private/FrmFactory.cpp`, `FrmMap.cpp`, `FrmMapLive.cpp`, `FrmPlayers.cpp`, `FrmRails.cpp`, `FrmRecords.cpp`, `FrmResourceNodes.cpp`, `FrmWorld.cpp`, and their headers.
  - Shared record writers that the native path also uses (for example `AppendTrainRecord`, `AppendStationRecord`) move out with the refactor.
  - `JsonTokens.h` goes only if no native path uses it.
- **Config** (`Config.h`, `Config.cpp`):
  - `DefaultFrmPort`, `FrmPort`, `FrmToken`, `GetFrmPort`, `IsFrmPort`, the `frmPort`/`frmToken` parse and write-back;
  - `enum ESource { Bridge, Native }`, with 13 `*Source` keys, defaults and getters. A bad value marks the config Broken today.
- **Bridge branches:** in `Driver.cpp` (`Drop*` on a flip to bridge, `Bridge.Tick(…, GetFrmPort(), FrmToken)`, `Take*`, `RequestPass(…Source==Bridge…)`), in `WorldSender.cpp` (`RequestWorld`, `TakeWorld`), and the FRM-sourced parts in `Snapshot.cpp`.
- **Tests and fixtures:**
  - `FrmBridgeTest`, `DriverFrmTest`, `FrmQueueTest`, the `Frm*Test` reader tests, and FRM parts of `WorldSenderTest`, `ConfigTest` and `DriverNativeTest`.
  - 16 `Fixtures/frm-*.json` files. The FRM-shaped inputs go. The `*.expected.json` goldens are ours and stay.
- **Docs:** `docs/native-readers.md` (the override section), `docs/runner.md`, `docs/clean-room.md`, README, and 17 `PROVENANCE.md` files that name FRM as the oracle. Those are history: reword them or keep them.

### Harness and runs

- **Oracles:**
  - `harness/src/frm.ts`, `frm-native.ts`, `frm-s2.ts`, `frm-s3.ts`, `frm-s4.ts`, `frm-s4b.ts`;
  - `harness/scripts/frm-golden.ts`;
  - 6 `frm*.test.ts` files;
  - the `frmBaseUrl` config.
- **Runs that need FRM:** `native-parity` (with `pass-s2/s3/s4/s4b.ts`), `parity`, `s4-capture`, `node-agent`, and the FRM legs of `enrol`, `manual`, `s3-census` and `tick-cost`.
- **Still guards quality without FRM:** `mod-test`, `tick-cost-native` (tick rate, memory, the 2 ms step ceiling), the committed goldens, each native reader's unit tests, `check-api` and `check-provenance`.
- **Lost:** a live equality check against an independent oracle for any future reader change.

### Dash (scope A only)

- **Stop direct FRM reads where the mod sends the data,** for a server fed by the mod:
  - map markers, drone routes and power lines in `backend/src/modules/map/services/localWorldPoller.ts` and `agent/src/worldReader.ts`;
  - the test connection's `getSessionInfo` (`packages/game-adapter/src/connectionTest.ts`) for mod servers.
  
  The game adapter, the Node agent and the per-server FRM settings stay for hosts without the mod.

## Steps (one PR each, no stacking; the owner merges)

| # | Repo | Scope | Proof | Risk |
|---|------|-------|-------|------|
| 1 | dash | Mod-fed servers take markers, drones and cables from the mod, and their test connection stops calling `getSessionInfo`. Hosts without the mod are unchanged. | dash CI, backend and agent unit tests; a mod-fed server shows every layer with FRM not running | low |
| 2 | mod | **Refactor, no behaviour change:** split `FFrmBridge` into a snapshot and world assembler (kept) and the FRM request and queue role. Move the shared record writers out. | `DriverNativeTest`, `WorldSenderTest`, `SnapshotTest` unchanged and green; `mod-test`; `tick-cost-native` | highest: shared code paths |
| 3 | mod | **Deletion:** Frm*/FrmQueue, `ESource` and the 13 `*Source` keys, `frmPort`/`frmToken`, the bridge branches, FRM tests and FRM-shaped fixtures. Stale keys handled per D2. Docs updated. Version bump. | `check-api`, `check-provenance`, a grep showing no `Frm`/`frmPort` symbol is left, `mod-test`, `tick-cost-native`, and the 24 h soak (D3) | installs that set removed keys (D2) |
| 4 | mod | **Harness retirement:** the `frm*` oracles, `frmBaseUrl`, `native-parity`, `parity`, `s4-capture`, `node-agent` (moves to the dash if still wanted), the FRM legs of the other runs; `docs/runner.md`. The last evidence is kept (D4). | harness Vitest, CI, `mod-test` and `tick-cost-native` still dispatchable | no oracle for later reader changes (D5) |
| 5 | both | **Release:** mod release notes ("FRM can be removed from a host running this mod"); dash notes and runbooks drop "install FRM" for mod hosts only; ADR-0058 status set to done. | release checklist; smoke test on the test server with FRM uninstalled | hosts update when they choose (ADR-0057) |

Order: step 1 first, so that no deployed dash needs FRM for a mod-fed server once the mod stops talking to it.
The mod already sends everything natively by default, so steps 2-4 don't depend on step 1 at runtime; the order follows ADR-0058's compatibility rule (mod N-1 and N both fully supported).

## Owner decisions

- **D1, scope:** **A**, confirmed by the owner on 2026-11-02.
- **D2, existing installs that still set `bridge`, `frmPort` or `frmToken`:** default **warn once in the log, then ignore**. A removed key must not mark a server's config Broken.
- **D3, the ADR's 24 h all-readers soak:** default **keep it** as the gate for step 3's release.
- **D4, the last evidence:** default **keep** native-parity 41340686528, tick-cost-native 41357114391 and the earlier sprint runs as the permanent record. They're already copied into the dash's `raw-sources/` for S1-S4-B.
- **D5, the `frm-*.ts` oracles:** default **delete them in step 4**, so nothing in the mod repo references FRM afterwards (the clean-room argument). Under scope A the dash's adapter keeps its own FRM knowledge.
- **D6, release timing and version numbers:** the owner's call, asked at step 5. The public mod release was held until R0-R4 (ADR answer 3).
- **D7, the dash's 39 FRM captures and FRM wiki pages:** under scope A they stay, because the frozen adapter still uses them.
