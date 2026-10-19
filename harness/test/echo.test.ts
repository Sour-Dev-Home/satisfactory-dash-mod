import { afterEach, describe, expect, it } from "vitest";
import { ECHO_MAX_BODY, type Echo, startEcho } from "../src/echo";

let echo: Echo | undefined;
afterEach(async () => {
  await echo?.close();
  echo = undefined;
});

describe("echo server", () => {
  it("binds loopback on an ephemeral port", async () => {
    echo = await startEcho(0);
    expect(echo.url).toBe(`http://127.0.0.1:${echo.port}`);
    expect(echo.port).toBeGreaterThan(0);
  });

  it("records a POST's method, path, headers and body, and answers 200", async () => {
    echo = await startEcho(0, () => new Date("2026-10-19T16:00:00Z"));
    const res = await fetch(`${echo.url}/s1`, { method: "POST", headers: { "content-type": "application/json" }, body: '{"a":1}' });
    expect(res.status).toBe(200);
    expect(echo.requests).toEqual([
      expect.objectContaining({ at: "2026-10-19T16:00:00.000Z", method: "POST", path: "/s1", body: '{"a":1}', tooLarge: false }),
    ]);
    expect(echo.requests[0]!.headers["content-type"]).toBe("application/json");
  });

  it("records a GET with an empty body", async () => {
    echo = await startEcho(0);
    await fetch(`${echo.url}/x?y=1`);
    expect(echo.requests[0]).toMatchObject({ method: "GET", path: "/x?y=1", body: "" });
  });

  it("refuses a body over 1 MiB with 413 and keeps no body", async () => {
    echo = await startEcho(0);
    const res = await fetch(`${echo.url}/big`, { method: "POST", body: "x".repeat(ECHO_MAX_BODY + 1) });
    expect(res.status).toBe(413);
    expect(echo.requests[0]).toMatchObject({ body: "", tooLarge: true });
  });
});
