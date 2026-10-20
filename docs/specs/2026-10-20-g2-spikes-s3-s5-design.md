# G2, second half: spikes S3, S4 and S5 (claims C5, C6, C7)

Status: draft for the owner's review (2026-10-20). It extends `docs/specs/2026-10-19-g2-harness-design.md` (approved
2026-10-19) and changes none of its rules: the claims gate, `api-sources.md` and `check-api`, harness-written
evidence only, a failed spike stops the work.

## What this is for

ADR-0039 allows a G3 build decision only after G0–G2 are complete. S0–S2 proved C1–C4 (satisfactory-dash #460,
2026-10-20). This spec covers the rest of G2:

| Claim | The mod can… | Spike |
| --- | --- | --- |
| C5 | read one power value that matches FRM `getPower` at the same moment | S3 |
| C6 | store a credential in its config, not visible to game clients | S4 |
| C7 | enrol with a code and push a minimal valid snapshot to a local backend | S5 |

Success: each spike produces a harness-written PASS bundle, which an evidence lane task in satisfactory-dash then
commits, as #459 did for S0–S2.

## Owner decisions (2026-10-20)

1. **S3's world is a copy of the owner's real save**, placed on the test server once and never written back, with
   FRM 1.5.4 installed beside the spike mod.
2. **S4 proves "not visible to clients" server-side only.** No client connects; the claim's wording says so.
3. **S5 runs only from a local session, never from the GitHub runner.** Docker Desktop needs its users in
   `docker-users`, which is close to admin on Windows, so `satis-runner` stays out of that group.
4. **Order: S4, then S5, then S3.** S5 needs S4's config mechanism; S3 needs the most setup.
5. **Sources before spikes:** satisfactory-dash #462 captures where a dedicated server keeps and loads its saves,
   and FRM's port setting. S3 waits for it.

## Facts this spec relies on, and where they come from

| Fact | Source |
| --- | --- |
| The power classes exist: `FGCircuitSubsystem.h`, `FGPowerCircuit.h`, `FGPowerInfoComponent.h`, `FGPowerConnectionComponent.h` | the SML project's `Source/FactoryGame/Public/` at `8eebc9e` (found 2026-10-20; each symbol's line goes in `api-sources.md` before use) |
| SML configs are local JSON files, `<ModID>.cfg` under `FactoryGame/Configs` | `sml-config` raw-source (client installs only; S4 checks the dedicated-server location) |
| SML's config code has no replication or RPC | a search of `Mods/SML/Source/SML/Public/Configuration/` (17 headers) and every SML file with `Config` in its name found none (2026-10-20). S4's bundle records the search; it's evidence of absence, and the C6 row says so |
| The agent contract: `POST /agent/v1/enroll` with `{code, agentVersion}`, then the snapshot route with `Authorization: Bearer <agentSecret>`; codes are `^[A-Z2-7]{4}-[A-Z2-7]{4}$`, single use, valid 10 minutes, stored as a SHA-256 | satisfactory-dash `packages/shared/src/agent.ts` and `docs-vault/wiki/runbooks/agents.md`, at a pinned commit |
| How a code is stored | satisfactory-dash `backend/migrations/1790899200000_agents.sql` and `backend/src/modules/agents/repositories/agentRepository.ts`, at the same pinned commit |
| A valid snapshot body | satisfactory-dash `packages/shared/fixtures/agent.ts`, validated against the vendored `SnapshotRequestSchema` |
| FRM `getPower`'s field names | satisfactory-dash's captured responses `docs-vault/raw-sources/captured-responses/frm-getPower-2026-09-22-*.json` (running, paused and battery-charging states) |
| Where the server reads its save; how to make it load one | **#462** (pending) |
| FRM's port setting | **#462** (pending) |
| Turning auto-pause off through the server API | satisfactory-dash's captured dedicated-server API docs; whether insecure local access may change it is checked in S3, not assumed |

## S4: a credential in the config (C6)

- **The mod (`SatisModS4`)** declares an SML config with one string property, `credential`. At startup it reads the
  value through SML's config API and logs `satis-mod s4 read length=<n> sha256=<first 12 hex>`, never the value.
- **The harness, before the boot:** writes `SatisModS4.cfg` into the test server's `FactoryGame/Configs` with a
  random value made for this run.
- **PASS:**
  - the logged length and hash prefix match the value written;
  - the full value appears nowhere in the run's server log;
  - the server stays up through the run.
- **After the run:** the harness deletes the cfg file.
- **If the server reads its configs from somewhere else**, the logged hash won't match. That's a FAIL, and the bundle
  shows where SML wrote the default file.

## S5: enrol and push one snapshot (C7)

- **The local backend:**
  - The harness checks out satisfactory-dash at a pinned commit into `D:\satis-test\dash`.
  - It runs `docker compose up` with secrets it generates for this run and a fresh database, bound to loopback.
  - Afterwards it runs `docker compose down -v`.
  - It refuses to start if any backend URL in the compose environment isn't loopback, and never points at production.
- **The code:** the harness generates a valid code, then seeds the fresh database with a test user, a test server and
  the code's row (hash, expiry, unspent) exactly as the migration and `agentRepository.ts` define them. It writes the
  code into `SatisModS5.cfg` (S4's mechanism).
- **The mod (`SatisModS5`):**
  - It reads the code, POSTs `/agent/v1/enroll`, and keeps the returned secret in memory only. Persisting it is a G3
    design question.
  - It then POSTs one snapshot with a fixed minimal body shaped on the fixture.
  - It logs `satis-mod s5 enroll=<status> snapshot=<status>`, and never the code or the secret.
- **The harness also validates** the body the mod is built to send against the vendored `SnapshotRequestSchema`, in
  unit tests, before any run.
- **PASS:**
  - enrolment succeeds and the snapshot is accepted;
  - the database shows the code spent and an agent credential with the mod's `agentVersion`;
  - a snapshot is stored for the test server;
  - neither the code nor the secret appears in the server log.

## S3: a power value that matches FRM (C5)

- **The world:**
  - The test server loads the copy of the owner's save, placed as #462 says.
  - The harness turns auto-pause off through the server API before measuring. A paused server produces no power.
  - If insecure local access can't change that setting, S3 stops and is reported; there's no workaround.
- **FRM** 1.5.4 is installed in the test server's `Mods`, on the port #462 captures, so it can't clash with the real
  server's FRM on 8080.
- **The mod (`SatisModS3`)** reads every power circuit through `UFGCircuitSubsystem` and `UFGPowerCircuit` (each
  symbol cited). Every 10 s for 2 min it logs `satis-mod s3 circuit=<id> production=<MW> consumption=<MW>
  capacity=<MW>`, one line per circuit. It sends nothing.
- **The harness:** on each reading in the log it calls FRM `getPower` at once and pairs the two.
- **PASS:**
  - at least 10 paired readings;
  - the same set of circuits on both sides;
  - in every pair, capacity and production within 1%.
- **Consumption** is recorded but not judged: machines cycle within a tick, so a 1-second gap between the mod's
  reading and FRM's would make it flaky. C5's wording becomes "power **production and capacity** match FRM".
- **If FRM's circuit IDs aren't the game's**, pairing falls back to the circuit with the largest capacity, and the
  bundle says so.

## Harness changes

Each is a module with unit tests on fixtures, run by `ci.yml` on a hosted runner.

- **Config writer and secret-absence check** (S4, S5): writes a spike's cfg file, deletes it afterwards, and asserts
  that a given value never appears in a log.
- **Local backend** (S5):
  - a pinned checkout, compose up and down with generated secrets, and the loopback guard;
  - a database seeder for the user, server and code;
  - database assertions for the spent code, the credential and the stored snapshot.
- **FRM probe** (S3): `getPower` on the configured port; a parser for the captured response shape; pairing and the 1%
  comparison.
- **Server settings** (S3): turn auto-pause off through the server API.
- **Spike specs:** `SPIKES` gains S3, S4 and S5 with their pass conditions in `spikes/S#/pass.ts`.
- **The runner config** gains the paths these need: the save fixture, FRM's port, and the satisfactory-dash checkout
  and its pinned commit. `toolchain.json`'s pins are unchanged.

## Runner and safety

- **S3 and S4 can run on the runner.** S5 runs only locally (owner decision 3), and `runner.md` says so.
- **The save fixture and FRM's files are copied in once, by the owner or a local session**, never by the runner:
  `satis-runner` is denied the real server's folder.
- **No credential the spikes use is real.** Each is generated per run, written only to the test server or the
  throwaway database, and deleted afterwards.

## Out of scope

- Real game data in the snapshot, persisting the agent secret, retries and backoff. That's the product mod (G3).
- A game client connecting to the test server.
- Linux servers, hosting providers, and anything in production.
