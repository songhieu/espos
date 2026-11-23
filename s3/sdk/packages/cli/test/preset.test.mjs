// espos preset (product presets, docs/app-slots.md §17): the open samples pass, each rule fires on its case, compile
// picks each board's parts, turns the saver template, the orientation and a fixed locale into settings, and refuses a
// board the preset does not name. The merge itself is the firmware's (test_host/preset_test.cpp, shared cases).
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { checkDef } from "../dist/jsonschema.js";
import { checkPreset, compilePreset, presetSchema, tzOffsetMin } from "../dist/preset.js";
import { espos, ROOT, tmp } from "./helpers.mjs";

const SAMPLES = path.join(ROOT, "sdk/presets");
const codes = (p, inputs) => checkPreset(p, "t.json", inputs).map((x) => x.code);
const base = () => ({ preset: 1, id: "my-desk", version: 1, name: "My desk", boards: ["es3c35p", "es3c28p"] });
const CATALOG = [
  { id: "vn.pawboop.tank", engine: "tank", parts: [{ id: "core", req: true }, { id: "bg28", group: "bg", board: "es3c28p" }, { id: "bg35", group: "bg", board: "es3c35p", default: true }] },
  { id: "vn.pawboop.pet", engine: "pet", parts: [{ id: "core", req: true }, { id: "game", opt: true }] },
  { id: "dev.espos.dice", engine: "js", parts: [{ id: "core", req: true }] },
];
const TEMPLATES = { templates: [{ id: "tank", p: { bg: { t: "tank" }, e: [] }, l: { bg: { t: "tank" }, e: [] } }] };

// The web's saver templates when this is the owner's tree (the open repo has no web folder: compiled without them).
const WEB_TEMPLATES = path.join(ROOT, "../web/app/src/os/saver-templates.json");
const saverTemplates = fs.existsSync(WEB_TEMPLATES) ? JSON.parse(fs.readFileSync(WEB_TEMPLATES, "utf8")) : undefined;

test("the open samples pass and compile for each of their boards", () => {
  for (const f of fs.readdirSync(SAMPLES).filter((n) => n.endsWith(".json"))) {
    const p = JSON.parse(fs.readFileSync(path.join(SAMPLES, f), "utf8"));
    assert.deepEqual(codes(p, { saverTemplates }), [], f);
    for (const b of p.boards) {
      const dev = compilePreset(p, b, { saverTemplates });
      if (saverTemplates) assert.ok(dev.settings["s3.saver_pages"], `${f}: the saver template compiled`);
      assert.deepEqual(checkDef(presetSchema(), "device", dev), [], `${f} ${b}`);
    }
  }
});

test("each rule on its case", () => {
  assert.ok(codes({ ...base(), preset: 2 }).includes("P001"));
  assert.ok(codes({ ...base(), extra: 1 }).includes("P001"));
  assert.ok(codes({ ...base(), settings: { "s3.pkgs": "[]" } }).includes("P002"));
  assert.ok(codes({ ...base(), settings: { "slot.assistant": "none" } }).includes("P002"));
  assert.ok(codes({ ...base(), settings: { "dash.ics_url": "https://x" } }).includes("P002"));
  assert.ok(codes({ ...base(), settings: { "s3.rot": "5" } }).includes("P003"));
  assert.ok(codes({ ...base(), settings: { "s3.saver_pages": "{not json" } }).includes("P003"));
  assert.ok(codes({ ...base(), settings: { "app.dev.espos.dice.sides": "6" } }).includes("P004"));
  assert.deepEqual(codes({ ...base(), packages: [{ id: "dev.espos.dice" }], settings: { "app.dev.espos.dice.sides": "6" } }), []);
  assert.ok(codes({ ...base(), packages: [{ id: "dev.espos.dice" }, { id: "dev.espos.dice" }] }).includes("P005"));
  assert.ok(codes({ ...base(), packages: [{ id: "com.example.nothere" }] }, { catalog: CATALOG }).includes("P006"));
  assert.ok(codes({ ...base(), packages: [{ id: "vn.pawboop.tank", parts: ["bg99"] }] }, { catalog: CATALOG }).includes("P006"));
  assert.ok(codes({ ...base(), slots: { assistant: "vn.pawboop.pet" } }).includes("P007"));
  assert.ok(codes({ ...base(), slots: { saver: "vn.pawboop.tank" } }).includes("P007"));
  assert.ok(codes({ ...base(), packages: [{ id: "dev.espos.dice" }], slots: { assistant: "dev.espos.dice" } }, { catalog: CATALOG }).includes("P008"));
  assert.ok(codes({ ...base(), saver: { template: "nope" } }, { saverTemplates: TEMPLATES }).includes("P009"));
  assert.ok(codes({ ...base(), home: { template: "weather" } }).includes("P010"));
  assert.ok(codes({ ...base(), locale: { city: "Hà Nội" } }).includes("P011"));
  assert.ok(codes({ ...base(), locale: { city: "ask", lat: "1" } }).includes("P011"));
  assert.ok(codes({ ...base(), orientation: "landscape", settings: { "s3.rot": "1" } }).includes("P012"));
  assert.ok(codes({ ...base(), saver: { template: "tank" }, settings: { "s3.saver_pages": "{}" } }).includes("P012"));
  assert.ok(codes({ ...base(), boards: ["es3c35p"], packages: [{ id: "vn.pawboop.tank", parts: ["core", "bg28"] }] }, { catalog: CATALOG }).includes("P013"));
  assert.ok(codes({ ...base(), entitlements: ["aquarium-full"] }).includes("P001"), "entitlements are @names");
});

test("compile: each board's parts, the saver template, orientation and a fixed locale as settings", () => {
  const p = { ...base(), packages: [{ id: "vn.pawboop.tank" }, { id: "vn.pawboop.pet", parts: ["core", "game"] }],
    slots: { assistant: "none", saver: "vn.pawboop.tank" }, saver: { template: "tank" }, orientation: "landscape",
    locale: { lang: "vi", timezone: "Asia/Ho_Chi_Minh", city: "Hà Nội", lat: "21.0245", lon: "105.8412" }, onboarding: { skip: ["dog"] } };
  assert.deepEqual(codes(p, { catalog: CATALOG, saverTemplates: TEMPLATES }), []);
  const a = compilePreset(p, "es3c35p", { catalog: CATALOG, saverTemplates: TEMPLATES });
  const b = compilePreset(p, "es3c28p", { catalog: CATALOG, saverTemplates: TEMPLATES });
  assert.deepEqual(a.packages[0].parts, ["core", "bg35"]);
  assert.deepEqual(b.packages[0].parts, ["core", "bg28"]);
  assert.deepEqual(a.packages[1].parts, ["core", "game"]);
  assert.equal(a.settings["s3.rot"], "1");
  assert.equal(b.settings["s3.rot"], "3", "the 2.8\" case stands on its other side");
  assert.equal(a.settings["slot.assistant"], "none");
  assert.equal(a.settings["slot.saver"], "vn.pawboop.tank");
  assert.deepEqual(JSON.parse(a.settings["s3.saver_pages"]), { v: 1, p: [{ bg: { t: "tank" }, e: [] }], l: [{ bg: { t: "tank" }, e: [] }] });
  assert.equal(a.settings["sys.lang"], "vi");
  assert.equal(a.settings["sys.tz"], "Asia/Ho_Chi_Minh");
  assert.equal(a.settings["sys.tz_min"], "420");
  assert.equal(a.settings["dash.city"], "Hà Nội");
  assert.deepEqual(a.skip, ["dog"]);
  assert.equal(a.ask, undefined);
  assert.deepEqual(checkDef(presetSchema(), "device", a), []);
  assert.throws(() => compilePreset({ ...p, boards: ["es3c35p"] }, "es3c28p", { catalog: CATALOG, saverTemplates: TEMPLATES }), /P014/);
  const asked = compilePreset({ ...base(), orientation: "ask", locale: { lang: "ask", timezone: "ask", city: "ask" } }, "es3c35p");
  assert.deepEqual(asked.ask, ["orientation", "lang", "timezone", "city"]);
});

test("the UTC offset of a zone, daylight saving included", () => {
  assert.equal(tzOffsetMin("Asia/Ho_Chi_Minh"), 420);
  assert.equal(tzOffsetMin("Europe/Berlin", new Date("2026-07-01T12:00:00Z")), 120);
  assert.equal(tzOffsetMin("Europe/Berlin", new Date("2026-01-15T12:00:00Z")), 60);
  assert.equal(tzOffsetMin("America/New_York", new Date("2026-01-15T12:00:00Z")), -300);
});

test("the commands: check (text and JSON), compile to a file, the rules list", () => {
  const d = tmp("preset");
  const bad = path.join(d, "bad.json");
  fs.writeFileSync(bad, JSON.stringify({ ...base(), settings: { "s3.rot": "9" } }));
  const ok = espos(["preset", "check", path.join(SAMPLES, "clock-desk.json")]);
  assert.equal(ok.code, 0, ok.out + ok.err);
  const no = espos(["preset", "check", bad, "--json"]);
  assert.equal(no.code, 1);
  assert.equal(no.json.ok, false);
  assert.ok(no.json.results[0].problems.some((p) => p.code === "P003"));
  const out = path.join(d, "dev.json");
  const c = espos(["preset", "compile", path.join(SAMPLES, "clock-desk.json"), "--board", "es3c28p", "--out", out]);
  assert.equal(c.code, 0, c.out + c.err);
  assert.equal(JSON.parse(fs.readFileSync(out, "utf8")).board, "es3c28p");
  const r = espos(["preset", "check", "--rules", "--json"]);
  assert.equal(r.json.length, 14);
});
