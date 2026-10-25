// Writes what satisfactory-dash's Node agent sends for FRM bodies (FRM bridge Plan 3A Tasks 5-6, Plan 3B Tasks 4 and 6). It
// imports the agent's own code from a satisfactory-dash checkout with node_modules installed, so the expected output is the
// Node agent's, byte for byte in meaning. Usage:
//   npx tsx scripts/frm-golden.ts --dash <satisfactory-dash checkout> --kind players|factory|rails|resourceNodes --in <body.json> --out <expected.json>
//   npx tsx scripts/frm-golden.ts --dash <checkout> --kind mapLive --in <getTrains body.json> --in2 <getTrainStation body.json> --out <expected.json>
// rails and resourceNodes write the layer's `data` array (what worldReader.ts hashes and posts under `data`); mapLive writes the
// snapshot's mapLive part exactly as Sampler builds it (500 per kind, strings cut, left out as `null` past 256,000 bytes).
import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

const arg = (name: string) => {
  const at = process.argv.indexOf(`--${name}`);
  const value = at === -1 ? undefined : process.argv[at + 1];
  if (!value) throw new Error(`missing --${name}`);
  return value;
};
const dash = arg("dash");
const kind = arg("kind");
const readBody = (name: string) => JSON.parse(readFileSync(arg(name), "utf8"));
const body = readBody("in");
const stations = kind === "mapLive" ? readBody("in2") : undefined;
const load = (path: string) => import(pathToFileURL(`${dash}/${path}`).href);
const adapterModule = await load("packages/game-adapter/src/index.ts");
const conform = await load("agent/src/conform.ts");
const adapter = new adapterModule.SatisfactoryServerAdapter(
  { call: async () => { throw new Error("no vanilla API in a golden"); } },
  { get: async (endpoint: string) => (endpoint === "getTrainStation" ? stations : body) },
);

/** The snapshot's mapLive as the Node agent's Sampler builds it from these two bodies. */
async function mapLive() {
  const { Sampler } = await load("agent/src/sampler.ts");
  const reader = {
    readStatus: async () => ({ tickHealth: "healthy", isGameRunning: true, gamePaused: false, sessionName: "golden", connectedPlayers: 0, playerLimit: 4, tickRate: 30, totalGameDurationSeconds: 1 }),
    readPower: async () => ({ circuits: [] }),
    readFactory: async () => ({ buildings: [] }),
    readPlayers: async () => ({ available: false, players: [] }),
    readAutoPause: async () => false,
    applyAutoPause: async () => undefined,
    readMapLive: async () => {
      const [trains, stationList] = await Promise.all([adapter.getTrains(), adapter.getTrainStations()]);
      return { trains: trains.data, stations: stationList.data, dropped: trains.dropped + stationList.dropped };
    },
  };
  const logger = { info() {}, warn() {}, error() {}, addSecret() {} };
  const sampler = new Sampler({ reader, cadence: () => ({ statusSeconds: 5, powerSeconds: 5, factorySeconds: 30 }), logger });
  const snapshot = await sampler.sample(Date.now());
  return snapshot.mapLive ?? null;
}

const kinds: Record<string, () => Promise<unknown>> = {
  factory: async () => conform.conformFactory({ buildings: (await adapter.getFactoryBuildings()).map(adapterModule.mapFactoryBuilding) }).value,
  players: async () => conform.conformPlayers(await adapterModule.readPlayers(() => adapter.getPlayers())).value,
  rails: async () => (await adapter.getRails()).data,
  resourceNodes: async () => (await adapter.getResourceNodes()).data,
  mapLive,
};
const produce = kinds[kind];
if (!produce) throw new Error(`unknown --kind ${kind}`);
writeFileSync(arg("out"), `${JSON.stringify(await produce(), null, 2)}\n`);
console.log(`wrote ${kind} for ${arg("in")}`);
