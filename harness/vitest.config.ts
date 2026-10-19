import { defineConfig } from "vitest/config";

export default defineConfig({
  // spikes/S#/pass.ts live outside this package; the tests import them.
  server: { fs: { allow: [".."] } },
  test: { include: ["test/**/*.test.ts"], testTimeout: 20_000 },
});
