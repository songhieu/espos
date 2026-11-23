// {espos} in an auth binding's send_to: the device's own EspOS server (apps/sessions, desktop-protocol.md §9.0).
import { test } from "node:test";
import assert from "node:assert/strict";
import { checkAuth } from "../dist/settings.js";

const settings = [{ key: "espos_token", type: "secret", label: "Access", where: "server" }];
const auth = { espos: { bearer: "espos_token", send_to: ["{espos}/v1/device/app/dev.espos.sessions/*"] } };
const run = (perms) => { const out = []; checkAuth(auth, settings, perms, (code, text) => out.push(code)); return out; };

test("{espos} is the device's own server, not a settings field", () => {
  assert.deepEqual(run(["net:espos"]), []);
});
test("{espos} needs net:espos", () => {
  assert.deepEqual(run(["notify"]), ["S010"]);
});

test("a device binding: the device's own credential to the app's own path, no secret", () => {
  const out = [];
  checkAuth({ espos: { device: true, send_to: ["{espos}/v1/device/app/dev.espos.sessions/*"] } }, [], ["net:espos"], (c) => out.push(c));
  assert.deepEqual(out, []);
});
test("a device binding never points anywhere else", () => {
  const out = [];
  checkAuth({ espos: { device: true, send_to: ["https://example.com/*"] } }, [], ["net:*"], (c) => out.push(c));
  assert.ok(out.includes("S013"));
});
