// The manifest's "settings" schema (docs/manifest-driven.md §1): the shared value cases, and every schema rule.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { checkAuth, checkSettings, checkValue, fieldsByKey, SETTING_TYPES } from "../dist/settings.js";
import { checkManifest, RULES } from "../dist/validate.js";
import { goodJs, here } from "./helpers.mjs";

const SCHEMAS = path.resolve(here, "../../sdk/schemas");
const vectors = JSON.parse(fs.readFileSync(path.join(SCHEMAS, "settings-vectors.json"), "utf8"));

const problems = (settings, perms = [], id = "vn.test.app") => {
  const out = [];
  checkSettings(settings, perms, id, (code, msg, level) => out.push({ code, msg, level: level ?? RULES.find((r) => r.code === code)?.level }));
  return out;
};
const codes = (ps, level) => ps.filter((p) => !level || p.level === level).map((p) => p.code);

const GOOD = [
  { key: "shop_url", type: "url", label: "Shop address" },
  { key: "ck", type: "secret", label: "Consumer key", where: "device" },
  { key: "cs", type: "secret", label: "Consumer secret", where: "device" },
  { key: "report", type: "secret", label: "Report key", where: "server" },
  { key: "currency", type: "enum", label: "Currency", default: "EUR", options: [{ v: "EUR", label: "Euro" }, { v: "USD", label: "US dollar" }],
    i18n: { vi: { label: "Tiền tệ", options: { EUR: "Euro", USD: "Đô la Mỹ" } } } },
  { key: "sound", type: "bool", label: "Beep on sale", default: true },
  { key: "speed", type: "number", label: "Speed", min: 1, max: 10, step: 1, default: 4 },
  { type: "section", label: "Advanced" },
  { key: "tables", type: "list", label: "Tables", of: { type: "string", max: 12 }, max_items: 30, default: ["Table 1"] },
  { key: "wake", type: "time", label: "Wake up", default: "07:00" },
  { key: "city", type: "city", label: "City" },
  { key: "note", type: "text", label: "Note", max: 500, help: "Shown on the receipt" },
];

test("every shared value case (settings-vectors.json) gives the expected answer", () => {
  assert.ok(vectors.cases.length >= 40);
  for (const c of vectors.cases) {
    const e = checkValue(c.field, c.value);
    assert.equal(e === "", c.ok, `${c.field.type} ${JSON.stringify(c.value)}: ${e || "ok"}`);
  }
});

test("the JSON schema file is valid JSON and names every type", () => {
  const s = JSON.parse(fs.readFileSync(path.join(SCHEMAS, "settings.schema.json"), "utf8"));
  const types = s.items.oneOf.map((f) => f.properties.type.const);
  assert.deepEqual([...types].sort(), [...SETTING_TYPES].sort());
  const v2 = JSON.parse(fs.readFileSync(path.join(SCHEMAS, "manifest-v2.schema.json"), "utf8"));
  assert.equal(v2.properties.settings.$ref, "settings.schema.json");
  assert.deepEqual(v2.properties.provides.items.enum, ["saver", "assistant"]);
});

const AUTH = { store: { basic: ["ck", "cs"], query: { consumer_key: "ck", consumer_secret: "cs" }, send_to: ["{shop_url}/wp-json/*"] } };
const authProblems = (auth, settings = GOOD, perms = ["net:setting:shop_url"]) => {
  const out = [];
  checkAuth(auth, settings, perms, (code, msg) => out.push({ code, msg }));
  return out.map((p) => p.code);
};

test("a good schema has no problems; its auth binding is covered by perms", () => {
  assert.deepEqual(problems(GOOD), []);
  assert.deepEqual(authProblems(AUTH), []);
  assert.deepEqual(authProblems(AUTH, GOOD, []), ["S010"]); // the device would not send the keys
  assert.deepEqual(authProblems({ api: { bearer: "report", send_to: ["https://api.example.com/v1/*"] } }, GOOD, ["net:api.example.com"]), []);
});

test("each auth rule fires on its bad case", () => {
  const BAD_AUTH = [
    ["not an object", [], "S013"],
    ["two forms", { s: { basic: ["ck", "cs"], bearer: "ck", send_to: ["{shop_url}/x"] } }, "S013"],
    ["basic with one key", { s: { basic: ["ck"], send_to: ["{shop_url}/x"] } }, "S013"],
    ["Authorization header", { s: { header: { Authorization: "ck" }, send_to: ["{shop_url}/x"] } }, "S013"],
    ["no send_to", { s: { bearer: "ck" } }, "S013"],
    ["http send_to", { s: { bearer: "ck", send_to: ["http://x.example.com/*"] } }, "S013"],
    ["a key that is not a secret", { s: { bearer: "shop_url", send_to: ["{shop_url}/x"] } }, "S014"],
    ["an unknown key", { s: { basic: ["ck", "nope"], send_to: ["{shop_url}/x"] } }, "S014"],
    ["placeholder not a url field", { s: { bearer: "ck", send_to: ["{currency}/x"] } }, "S011"],
  ];
  for (const [what, auth, code] of BAD_AUTH) {
    const got = authProblems(auth, GOOD, ["net:*"]);
    assert.ok(got.includes(code), `${what}: expected ${code}, got ${got.join(", ") || "nothing"}`);
  }
});

test("fieldsByKey skips sections", () => {
  const m = fieldsByKey(GOOD);
  assert.equal(m.size, GOOD.length - 1);
  assert.equal(m.get("cs").where, "device");
  assert.equal(m.get("speed").max, 10);
});

const BAD = [
  ["not a list", { a: 1 }, "S001"],
  ["41 fields", Array.from({ length: 41 }, (_, i) => ({ key: `k${i}`, type: "bool", label: "x" })), "S001"],
  ["unknown type", [{ key: "a", type: "slider", label: "A" }], "S002"],
  ["bad key", [{ key: "Speed", type: "bool", label: "A" }], "S003"],
  ["key twice", [{ key: "a", type: "bool", label: "A" }, { key: "a", type: "bool", label: "B" }], "S003"],
  ["section with a key", [{ key: "a", type: "section", label: "A" }], "S003"],
  ["no label", [{ key: "a", type: "bool" }], "S004"],
  ["long label", [{ key: "a", type: "bool", label: "x".repeat(41) }], "S004"],
  ["long help", [{ key: "a", type: "bool", label: "A", help: "x".repeat(121) }], "S004"],
  ["string max 0", [{ key: "a", type: "string", label: "A", max: 0 }], "S005"],
  ["text max 3000", [{ key: "a", type: "text", label: "A", max: 3000 }], "S005"],
  ["min >= max", [{ key: "a", type: "number", label: "A", min: 5, max: 5 }], "S005"],
  ["step 0", [{ key: "a", type: "number", label: "A", step: 0 }], "S005"],
  ["one option", [{ key: "a", type: "enum", label: "A", options: [{ v: "x", label: "X" }] }], "S005"],
  ["same option twice", [{ key: "a", type: "enum", label: "A", options: [{ v: "x", label: "X" }, { v: "x", label: "Y" }] }], "S005"],
  ["option without label", [{ key: "a", type: "enum", label: "A", options: [{ v: "x" }, { v: "y", label: "Y" }] }], "S005"],
  ["list of numbers", [{ key: "a", type: "list", label: "A", of: { type: "number" } }], "S006"],
  ["list max_items 99", [{ key: "a", type: "list", label: "A", of: { type: "string" }, max_items: 99 }], "S006"],
  ["list of a bad enum", [{ key: "a", type: "list", label: "A", of: { type: "enum", options: [{ v: "x", label: "X" }] } }], "S006"],
  ["secret without where", [{ key: "a", type: "secret", label: "A" }], "S007"],
  ["secret with the old per-field send_to", [{ key: "a", type: "secret", label: "A", where: "device", send_to: ["https://x.example.com/*"] }], "S007"],
  ["secret with a default", [{ key: "a", type: "secret", label: "A", where: "server", default: "x" }], "S007"],
  ["default out of range", [{ key: "a", type: "number", label: "A", min: 1, max: 3, default: 9 }], "S008"],
  ["bool default as text", [{ key: "a", type: "bool", label: "A", default: "1" }], "S008"],
  ["enum default not an option", [{ key: "a", type: "enum", label: "A", default: "z", options: [{ v: "x", label: "X" }, { v: "y", label: "Y" }] }], "S008"],
  ["city default", [{ key: "a", type: "city", label: "A", default: "Berlin" }], "S008"],
  ["i18n unknown language", [{ key: "a", type: "bool", label: "A", i18n: { xx: { label: "B" } } }], "S009"],
  ["i18n unknown field", [{ key: "a", type: "bool", label: "A", i18n: { vi: { name: "B" } } }], "S009"],
  ["store_as for a third party", [{ key: "a", type: "bool", label: "A", store_as: "s3.assist#wake" }], "S012"],
];

test("each schema rule fires on its bad case", () => {
  for (const [what, settings, code] of BAD) {
    const got = codes(problems(settings, ["net:*"]));
    assert.ok(got.includes(code), `${what}: expected ${code}, got ${got.join(", ") || "nothing"}`);
  }
});

test("store_as is allowed for the OS's own packages", () => {
  assert.deepEqual(problems([{ key: "wake", type: "bool", label: "Wake word", store_as: "s3.assist#wake" }], [], "dev.espos.assistant"), []);
});

test("espos check: settings in a manifest are checked; v2 permissions are kept with a warning", () => {
  const m = goodJs({ perms: ["sound", "net:setting:shop_url", "cache:2048", "dictate"], settings: GOOD, auth: AUTH });
  const ps = checkManifest(m, "manifest.json");
  assert.deepEqual(codes(ps, "error"), []);
  assert.ok(codes(ps, "warning").includes("M048")); // net:setting:shop_url is not granted by firmware 0.5.0 yet
  assert.ok(codes(ps, "warning").includes("M031")); // settings: not read by firmware 0.5.0 yet
  const bad = checkManifest(goodJs({ settings: [{ key: "a", type: "slider", label: "A" }] }), "manifest.json");
  assert.ok(codes(bad, "error").includes("S002"));
  const badAuth = checkManifest(goodJs({ settings: GOOD, auth: { s: { bearer: "nope", send_to: ["{shop_url}/x"] } } }), "manifest.json");
  assert.ok(codes(badAuth, "error").includes("S014"));
});

test("level 2: async code and the perms level-2 firmware grants are not warned about; the others still are", async () => {
  const { checkCode } = await import("../dist/validate.js");
  const asyncSrc = "app.render = () => ui.text('x');\nnet.request({url: 'https://a.example.com/x'}).then((r) => toast(r.status));\n";
  assert.ok(checkCode(asyncSrc, [], "main.js", 1).some((p) => p.code === "C007"));
  assert.ok(!checkCode(asyncSrc, [], "main.js", 2).some((p) => p.code === "C007"));
  const m2 = goodJs({ api: { min: 2 }, perms: ["net:*.example.com", "net:setting:shop_url", "cache:512", "overlay"] });
  const w = checkManifest(m2, "manifest.json").filter((p) => p.code === "M048").map((p) => p.msg);
  assert.equal(w.length, 1);
  assert.match(w[0], /overlay/);
  assert.deepEqual(codes(checkManifest(goodJs({ perms: ["net:*.example.com"] }), "manifest.json"), "error"), []);
  // a wildcard domain covers an auth binding's host
  assert.deepEqual(authProblems({ api: { bearer: "ck", send_to: ["https://shop.example.com/v1/*"] } }, GOOD, ["net:*.example.com"]), []);
});
