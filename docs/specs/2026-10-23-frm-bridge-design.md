# G3, sub-project 3: the FRM bridge

Status: design approved by the owner section by section on 2026-10-23; this written spec is in review. Sub-project 2's
spec is `2026-10-20-core-mod-v1-design.md`; the build decision is ADR-0042 (satisfactory-dash).

## What this is for

The mod replaces the Node agent (core spec, owner decision 2). v1 reads status, power and auto-pause natively. The
Node agent also sends the factory, players and live-map parts in each snapshot, and three slow-changing world layers
through their own route. Until the mod sends them too, the owner's server can't switch over (sub-project 5) without the
dashboard losing data.

The FRM bridge reads those layers from Ficsit Remote Monitoring (FRM), as the Node agent does, so the mod reaches
**full parity with the Node agent** (owner, 2026-10-23). Native readers that replace FRM stay sub-project 6.

## Owner decisions (2026-10-23)

1. **Full Node-agent parity.** Everything the Node agent sends beyond status, power and settings comes from the mod.
2. **Loopback HTTP to FRM** (approach A), not linking FRM (B) or native readers now (C).
3. **Light on the host, with no spikes.** No change may cause a performance hit or a hitch. Large work is spread out
   by queuing, staggering or slicing (owner: "If something is very large/a task requires large effort, implement
   queuing, pagination, or other methods to distribute load"). This joins the standing goal that the mod is light on
   the host, memory especially.
4. **The FRM token is optional.** The owner doesn't know whether their FRM enforces one, so the mod works without
   it, accepts one if set, and says in its log when FRM asks for one.

**Amendment to the core spec's decision 5.** That decision keeps the config to the backend URL and the one-time
enrolment code. The bridge adds `frmPort` and an optional `frmToken`. The token is the host's own FRM secret, not the
dashboard credential, which stays in `credential.json` alone.

## What the Node agent sends, which the mod must match

Source: satisfactory-dash at `9028b9ad`, `agent/` and `packages/game-adapter`. Every rule below is ported from there.
Field lists, derivations and caps are as in those files.

| Part | FRM endpoint(s) | Cadence | Where it goes | Caps |
| --- | --- | --- | --- | --- |
| `players` | `getPlayer` (only `Name`, `Online`, ADR-0029) | every status pass (5 s) | snapshot | 256 players |
| `factory` | `getFactory` | factory cadence (30 s) | snapshot | 20,000 buildings, 16 rates each |
| `mapLive` | `getTrains`, `getTrainStation` | factory cadence | snapshot | 500 of each kind, 256,000 bytes serialised, else the part is left out |
| `rails` | `getTrainRails` | 10 min ±10% | `POST /agent/v1/world/rails` | 5,000 segments of 2-1,000 points |
| `conveyors` | `getBelts`, `getLifts`, `getPipes` | 10 min ±10% | `POST /agent/v1/world/conveyors` | 50,000 edges, or a `skipped` body |
| `resourceNodes` | `getResourceNode` | 30 min ±10% | `POST /agent/v1/world/resourceNodes` | 5,000 nodes |

- **Cadences:** the backend sets the snapshot cadence in each response. The world layers use fixed cadences with
  jitter.
- **World layers are sent only on change:** each layer is sent only when the SHA-256 of its JSON differs from the last
  accepted send, and the hash is kept in memory, so the first read after start always sends. A layer whose body is
  over 2,000,000 bytes is skipped, and the backend keeps its last copy. The backend allows 20 world posts per 10
  minutes per credential.
- **Mapping lives in the Node agent's files:** `satisfactoryServerAdapter.ts` (the factory, players, trains and
  stations), `snapshot/factoryMapper.ts` (`isBackedUp`, `inputsFull`), `map/*Mapper.ts` (rails, resource nodes,
  conveyors, coordinates) and `agent/src/conform.ts` (string and list caps, negatives raised to 0, non-finite values
  dropped).
- **The contract copy is behind:** this repo's `harness/contract/src/map.ts` has no conveyors layer and no `skipped`
  body. It is re-pinned from satisfactory-dash before any of this is built.

## Design

### Components

All new logic is in `SatisDashCore` (pure, unit-tested). `SatisDashGame` changes only the transport.

| Unit | Module | Job |
| --- | --- | --- |
| `FFrmClient` | Core | Builds `GET http://127.0.0.1:<frmPort>/<endpoint>` with an optional `X-FRM-Authorization` header, and sends it through `ITransport` with that endpoint's response cap and timeout |
| `FFrmQueue` | Core | Every FRM read, in two priority lanes, at most one request in flight (below) |
| Mappers: factory, players, mapLive, rails, resourceNodes, conveyors | Core | FRM JSON to compact structs to contract JSON, ported field by field from the Node agent |
| `FWorldSender` | Core | The world-layer schedule, hashing, sending, retries and backoff |
| Driver | Core | The sample pass adds `factory` and `mapLive` (factory cadence) and `players` (every pass), each optional |
| `FCurlTransport` | Game | A per-request response cap. Backend calls keep `MaxResponseBytes` (64 KB) |

Status, power and auto-pause stay native and unchanged.

### Threading

- **Fetching** stays on libcurl's multi handle, pumped by the core ticker on the game thread, and never blocks.
- **Parsing, mapping, serialising and gzipping** run on a background task (`UE::Tasks::Launch`, its header and row
  looked up first). The task:
  - takes ownership of the body bytes, runs only pure core code (no `UObject`, world or transport), and hands finished
    contract JSON back to the game thread;
  - works through a body in slices (about 2,000 records each), yielding between them.
- At most one background parse runs at a time. **The game thread never parses an FRM body.**

### Load spreading

**Where FRM does its work** (FRM 1.5.4, `Source/FicsitRemoteMonitoring/Private/FicsitRemoteMonitoring.cpp` at tag
`1.5.4-515`):

- FRM serves its HTTP API on **one thread of its own**, a uWS event loop (`:253`), with a default port of 8080 (`:258`).
- An endpoint runs on the game thread only when it is registered with `RequiresGameThread()`:

| On the game thread | On FRM's own server thread |
| --- | --- |
| `getPlayer` (small, every 5 s), `getResourceNode` (about 288 KB, every 30 min) | `getFactory`, `getBelts`, `getLifts`, `getPipes`, `getTrains`, `getTrainStation`, `getTrainRails` (`:796-896`) |

So the largest reads don't cost the game's frames. What the queue protects is the mod's memory, FRM's single server
thread, and the latency of the small live reads.

**The queue (`FFrmQueue`):**

- **At most one FRM request in flight.** FRM answers on one thread, so parallel requests would only wait inside FRM
  while holding more bodies in the mod's memory.
- **Two priority lanes:**
  - The **live lane** (`players`, `factory`, `mapLive`) always goes first.
  - The **world lane** (`rails`, `conveyors`, `resourceNodes`) starts only when the live lane has nothing due.
- **Large and small reads:**
  - The next large read starts only after the previous large body's parse has finished, so at most one large raw body
    is held.
  - While a large body parses in the background, only `getPlayer` (tiny) may still be fetched, so players never wait
    behind a 3 MB parse.
- **Unchanged world layers skip their parse.** The raw FRM body of each world-layer read is hashed first. When it
  matches the last raw hash, the parse is skipped and the layer is not re-sent. Rails and resource nodes rarely
  change, so most world reads cost only the download.

**Timing:**

- **Cadences match the Node agent's:** the mod never asks FRM for more than the Node agent does.
- **Staggered start:** after boot, the world layers start at about 2 min (rails), 5 min (conveyors) and 8 min (resource
  nodes), each with jitter.
- **The three conveyor endpoints** are read one after another, each body parsed and released before the next request.
- **World-layer sends** are gzipped, and go one at a time behind snapshots, so they never delay a status snapshot.

**Not added, by design:** adaptive pacing (slowing an endpoint when its reads are slow), and FRM's WebSocket push (FRM
pushes on its own timer, giving less control over timing and size). Either can come later if the measurements below
show a need.

### Memory

- **No JSON tree.** Mappers read with UE's token-by-token reader (`TJsonReader`), building only the compact structs
  the contract needs. Fields the contract doesn't use (inventories, z values, FRM's extras) are skipped while reading,
  never stored.
- **Per-endpoint response caps, enforced while downloading:** 16 MB for `getFactory`, `getBelts` and `getPipes`; 4 MB
  for the rest. A body over its cap is cut off while it downloads, and that cycle's part is skipped with one warning.
- **Caps are applied while reading:** records past a cap are counted and dropped as they are read. The exception is
  conveyors: past 50,000 edges the reader stops and the layer is sent as `skipped: "tooManyEdges"`, as the Node agent
  does.
- **Peak per read:** one large raw body plus its compact output, and at most one small `getPlayer` body passing it. For example, a conveyor edge is about 200 bytes, so the
  owner's roughly 2,400 edges come to about 0.5 MB.
- **Steady state:** each world layer keeps only its last hash (32 bytes). Snapshot parts live until serialised, then
  in the existing queue (120 snapshots, 2 MiB, gzipped, oldest dropped first).
- **The game thread** only starts requests, hands finished parts to the snapshot and queues sends. Each of those is a
  small, fixed amount of work.

### Configuration

`SatisDashAgent.cfg` gains:

- **`frmPort`:** FRM's HTTP port, default 8080 (FRM's own default, `FicsitRemoteMonitoring.cpp:258`, and the Node
  agent's, `agent/src/store.ts:29`), checked to be 1-65535. The host is always `127.0.0.1`, because the mod
  and FRM are in the same game process.
- **`frmToken`, optional:** sent only as `X-FRM-Authorization`, never logged. A value with a line break is refused
  before any request.

If FRM answers 401 or 403, the mod logs once: "FRM requires a token: set frmToken in SatisDashAgent.cfg".

### Failures

| Case | What the mod does |
| --- | --- |
| FRM unreachable, timed out, or the body is bad or over its cap | That part is left out; the snapshot stays `reachable: true` (status and power are native). Logged once per streak |
| `getPlayer` fails | `players: {available: false, players: []}` |
| A world-layer read fails | That cycle is skipped; the backend keeps its last copy |
| A world-layer post fails transiently or gets 429 | Retry with backoff, honouring `Retry-After` |
| A world-layer post gets another 4xx | Give up until the next cycle |
| A world-layer post gets 401 | Halt the agent, as a snapshot 401 does |
| The agent isn't running (not enrolled, revoked, halted) | No FRM reads at all |

### Security

- **FRM's answers are untrusted:** byte and record caps, strings cut to 200 characters, non-finite numbers dropped,
  and the Node agent's conform rules applied.
- **Players' privacy (ADR-0029):** only `Name` and `Online` are read from `getPlayer`; everything else is skipped
  unread.
- **Secrets stay apart:** the backend secret never goes to FRM, and the FRM token never goes to the backend.
- **FRM requests are plain HTTP to loopback only,** through the S1 URL check, with proxies off.
- **New items for the security review:** the FRM token in a config file (readable by the host's admin, like the rest
  of the config), and FRM as a new untrusted input.

## Testing and evidence

**Unit tests (UE automation):**

- **Each mapper** is tested against the Node agent's FRM fixtures (satisfactory-dash's captured responses), expecting
  the Node agent's output, including:
  - an unconfigured recipe and negatives clamped to 0;
  - open conveyor ends, and an FRM older than 1.5.4 (`skipped: "unsupportedFrm"`);
  - lists over their caps.
- **The queue and scheduler,** with a fake clock:
  - one request in flight, the live lane ahead of the world lane, and `getPlayer` passing a large parse but no other
    read doing so;
  - the raw-hash skip (no parse and no send for an unchanged body), the boot offsets and jitter;
  - backoff, a 401 halt, and no reads while idle.
- **The token reader:**
  - skipped fields are never stored, and a capped list stops at its cap;
  - a malformed body is refused without a crash.

**Game runs (harness, ue-runner):**

- **`parity`** adds `factory`, `players` and `mapLive` in every round, and each world layer once. The mod and the Node
  agent run side by side on the staged world.
- **`tick-cost`'s window** includes a factory read, a conveyor read and a `getResourceNode` read. The evidence
  records peak memory around the largest read, and each endpoint's response time with the game's tick around it.
  `getResourceNode` is the one large endpoint FRM runs on the game thread; if its read hitches, the fix is a longer
  cadence, since resource nodes rarely change. The pass limits for these are the owner's ruling: the plan proposes numbers, and no existing limit
  changes without the owner.
- **New `frm-down`:** FRM stops mid-run. The parts are left out, players show `available: false`, status and power
  keep flowing, and the mod recovers when FRM returns.

**Rules carried over:** every new UE or SML symbol (for example `TJsonReader` and `UE::Tasks::Launch`) gets its
`api-sources.md` row from the pinned headers before it is used. Pass conditions are never weakened.

## Order of work

Each step is one PR, built, tested and run before the next:

1. Re-pin the contract. Add the per-request response cap, the FRM client and queue, and the config fields.
2. `players` and `factory`, with the token reader and background parsing.
3. `mapLive`.
4. The world sender, with `rails` and `resourceNodes`.
5. `conveyors`.
6. The extended `parity` and `tick-cost` runs, and `frm-down`, with the security review items.

## Out of scope

- Native readers that replace FRM (sub-project 6).
- Linux builds and ficsit.app (sub-project 4).
- The switchover (sub-project 5).
- Fixing the Node agent's own drift (an over-cap layer sends nothing, where ADR-0038 says it should send
  `{truncated, count}`); the mod matches the Node agent's behaviour.
