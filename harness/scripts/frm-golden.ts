// Writes what satisfactory-dash's Node agent sends for one FRM body (FRM bridge Plan 3A, Tasks 5-6). It imports the
// agent's own code from a satisfactory-dash checkout with node_modules installed, so the expected output is the Node
// agent's, byte for byte in meaning. Usage:
//   npx tsx scripts/frm-golden.ts --dash <satisfactory-dash checkout> --kind players|factory --in <body.json> --out <expected.json>
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
const body = JSON.parse(readFileSync(arg("in"), "utf8"));
const adapterModule = await import(pathToFileURL(`${dash}/packages/game-adapter/src/index.ts`).href);
const conform = await import(pathToFileURL(`${dash}/agent/src/conform.ts`).href);
const adapter = new adapterModule.SatisfactoryServerAdapter(
  { call: async () => { throw new Error("no vanilla API in a golden"); } },
  { get: async () => body },
);
const out =
  kind === "factory"
    ? conform.conformFactory({ buildings: (await adapter.getFactoryBuildings()).map(adapterModule.mapFactoryBuilding) }).value
    : conform.conformPlayers(await adapterModule.readPlayers(() => adapter.getPlayers())).value;
writeFileSync(arg("out"), `${JSON.stringify(out, null, 2)}\n`);
console.log(`wrote ${kind} for ${arg("in")}`);
