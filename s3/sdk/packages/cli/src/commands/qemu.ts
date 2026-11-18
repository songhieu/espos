// espos qemu: the real firmware in Espressif's QEMU (tools/qemu/run.sh), optionally with packages put onto its
// memory card image first. The card is changed in place (mtools, or hdiutil on macOS): what the firmware saved on
// it (store installs, game saves) and its flash (NVS link token, LittleFS) stay.
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import type { CmdSpec, Ctx } from "../args.js";
import { hasErrors, packagesFrom, printProblems, show, summary } from "../common.js";
import { installTree, type ReadPkg } from "../espk.js";
import { CliError, dim, EXIT, green, note, red, say, warn } from "../out.js";
import { hint, isMac, needRoot, pio, python, qemuBinary, run, runSync, which } from "../repo.js";

function boardFiles(root: string, board?: string) {
  const t = board === "28" ? "-28" : "";
  return {
    env: board === "28" ? "s3desk-28-qemu" : "s3desk-qemu",
    img: path.join(root, `build-qemu/flash${t}.bin`),
    sd: path.join(root, `build-qemu/sd${t}.img`),
    monitor: path.join(root, `build-qemu/monitor${t}.sock`),
    port: board === "28" ? 5556 : 5555,
  };
}

/** Is a QEMU listening on its monitor socket (then its card image is open and must not change)? */
function qemuRunning(sock: string): Promise<boolean> {
  if (!fs.existsSync(sock)) return Promise.resolve(false);
  return new Promise((res) => {
    const s = net.connect(sock);
    s.once("connect", () => {
      s.destroy();
      res(true);
    });
    s.once("error", () => res(false));
    setTimeout(() => {
      s.destroy();
      res(false);
    }, 500);
  });
}

/** Byte offset of the first partition of an MBR disk image (0 when there is no MBR). */
function firstPartitionOffset(img: string): number {
  const fd = fs.openSync(img, "r");
  const b = Buffer.alloc(512);
  fs.readSync(fd, b, 0, 512, 0);
  fs.closeSync(fd);
  if (b[510] !== 0x55 || b[511] !== 0xaa) return 0;
  const lba = b.readUInt32LE(446 + 8);
  return b[446 + 4] ? lba * 512 : 0;
}

/** macOS writes extended attributes on a FAT volume as "._name" files: the card does not need them. */
function dropAppleDouble(dir: string): void {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.name.startsWith("._")) fs.rmSync(p, { force: true });
    else if (e.isDirectory()) dropAppleDouble(p);
  }
}

/** Puts packages onto a FAT card image without rebuilding it. */
export async function installOnCard(card: string, groups: { id: string; core: ReadPkg; parts: ReadPkg[] }[]): Promise<void> {
  const mtools = which("mcopy") && which("mmd") && which("mdeltree");
  if (mtools) {
    const at = `${card}@@${firstPartitionOffset(card)}`;
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "espos-card-"));
    try {
      runSync("mmd", ["-i", at, "::/espos"]);
      runSync("mmd", ["-i", at, "::/espos/pkgs"]);
      for (const g of groups) {
        const dir = path.join(tmp, g.id);
        installTree(dir, g.core, g.parts);
        runSync("mdeltree", ["-i", at, `::/espos/pkgs/${g.id}`]);
        const r = runSync("mcopy", ["-s", "-o", "-i", at, dir, "::/espos/pkgs/"]);
        if (r.code !== 0) throw new CliError(`mcopy failed: ${r.stderr.trim()}`);
        note(`${green("installed")} ${g.id} on ${path.basename(card)} (mtools)`);
      }
    } finally {
      fs.rmSync(tmp, { recursive: true, force: true });
    }
    return;
  }
  if (!isMac) throw new CliError("changing the card image needs mtools here", EXIT.MISSING, "install: Linux: sudo apt install mtools; macOS: brew install mtools (or hdiutil, built in)");
  const mnt = fs.mkdtempSync(path.join(os.tmpdir(), "espos-card-"));
  const a = runSync("hdiutil", ["attach", "-plist", "-imagekey", "diskimage-class=CRawDiskImage", "-nobrowse", "-noverify", "-noautofsck", "-mountrandom", mnt, card]);
  if (a.code !== 0) throw new CliError(`hdiutil attach ${card} failed: ${a.stderr.trim()}`);
  const dev = /<key>dev-entry<\/key>\s*<string>(\/dev\/disk\d+)<\/string>/.exec(a.stdout)?.[1];
  const mounts = [...a.stdout.matchAll(/<key>mount-point<\/key>\s*<string>([^<]+)<\/string>/g)].map((m) => m[1]);
  try {
    if (!mounts.length) throw new CliError(`${card}: no FAT volume found on the card image`);
    const root = mounts[0];
    for (const g of groups) {
      const dir = path.join(root, "espos/pkgs", g.id);
      installTree(dir, g.core, g.parts);
      dropAppleDouble(dir);
      fs.rmSync(path.join(root, "espos/pkgs", `._${g.id}`), { force: true });
      note(`${green("installed")} ${g.id} on ${path.basename(card)} (hdiutil)`);
    }
  } finally {
    if (dev) {
      const d = runSync("hdiutil", ["detach", dev]);
      if (d.code !== 0) warn(`hdiutil detach ${dev}: ${d.stderr.trim()} (detach it by hand before QEMU starts)`);
    }
    fs.rmSync(mnt, { recursive: true, force: true });
  }
}

async function qemuCmd(c: Ctx): Promise<number> {
  const root = needRoot();
  const board = c.opts.board ? String(c.opts.board) : undefined;
  if (board && board !== "28" && board !== "35") throw new CliError("--board 28 (the 2.8\" board) or 35 (default)", EXIT.USAGE);
  if (c.opts.fresh && c.opts.keep) throw new CliError("--fresh or --keep, not both", EXIT.USAGE);
  const f = boardFiles(root, board === "28" ? "28" : undefined);
  const card = c.opts.card ? path.resolve(c.opts.card) : f.sd;
  const runSh = path.join(root, "tools/qemu/run.sh");
  if (!fs.existsSync(runSh)) throw new CliError(`${runSh} not found`, EXIT.MISSING);
  const q = qemuBinary(root);
  if (!q && c.opts.run !== false) throw new CliError("Espressif's QEMU (qemu-system-xtensa) not found", EXIT.MISSING, hint("qemu"));
  if (!isMac) warn("tools/qemu/run.sh is written for macOS: the viewer links -framework ApplicationServices with Homebrew's SDL2, DYLD_FALLBACK_LIBRARY_PATH, and mkimage.py makes the card with hdiutil; on Linux use --headless or --tcp with an existing build-qemu/ image");
  const passMode: string[] = [];
  if (c.opts.sdl) passMode.push("--sdl");
  if (c.opts.tcp) passMode.push("--tcp", String(c.opts.tcp));
  if (c.opts.headless) passMode.push("--headless");
  if (passMode.length > (c.opts.tcp ? 2 : 1)) throw new CliError("one of --sdl, --tcp, --headless", EXIT.USAGE);

  let keep = !!c.opts.keep;
  if (c.opts.install?.length) {
    const qemuCards = [boardFiles(root).sd, boardFiles(root, "28").sd];
    const inUse = qemuCards.includes(card) && (await qemuRunning(card === f.sd ? f.monitor : boardFiles(root, card.endsWith("-28.img") ? "28" : undefined).monitor));
    if (inUse) throw new CliError(`QEMU is running (${path.relative(root, f.monitor)} answers): stop it first, its card image is open`, EXIT.FAIL);
    const { pkgs, problems } = await packagesFrom(c.opts.install, { write: true });
    printProblems(problems.filter((p) => p.level !== "info"));
    if (hasErrors(problems) && !c.opts.force) {
      say(red("not installed") + `: ${summary(problems)} (--force installs anyway)`);
      return EXIT.FAIL;
    }
    const byId = new Map<string, ReadPkg[]>();
    for (const p of pkgs) byId.set(String(p.plain.id), [...(byId.get(String(p.plain.id)) ?? []), p]);
    const groups = [...byId].map(([id, parts]) => {
      const core = parts.find((p) => (p.plain.part ?? "core") === "core");
      if (!core) throw new CliError(`${id}: the core part is needed`);
      return { id, core, parts };
    });
    if (groups.some((g) => g.core.plain.engine === "j2me" && g.core.plain.type !== "pack")) note(dim("Java games need dev.espos.j2me on the device (the card image from sim/data-live has it; else add build-pkgs/dev.espos.j2me-*.espk to --install)"));
    const fresh = !!c.opts.fresh || !fs.existsSync(f.img) || !fs.existsSync(card);
    if (fresh && !c.opts.card) {
      // what run.sh would do, so the packages go onto the new card before QEMU starts
      const p = pio();
      if (!p) throw new CliError("PlatformIO (pio) not found", EXIT.MISSING, hint("pio"));
      note(dim(`building ${f.env} and a new image (data from sim/data-live, unlinked)`));
      const b = await run(p, ["run", "-e", f.env], { cwd: root, capture: true });
      if (b.code !== 0) {
        process.stderr.write(b.stdout.split("\n").filter((l) => /error|FAILED/i.test(l)).slice(-20).join("\n") + "\n");
        throw new CliError(`pio run -e ${f.env} failed`);
      }
      const m = await run(python(), [path.join(root, "tools/qemu/mkimage.py"), f.img, "--env", f.env, "--sd", f.sd], { cwd: root });
      if (m.code !== 0) throw new CliError("tools/qemu/mkimage.py failed");
      keep = true;
    } else if (!fs.existsSync(card)) throw new CliError(`${card}: no card image`, EXIT.USAGE);
    await installOnCard(card, groups);
    if (groups.length) note(`open it: type APP ${groups[0].id} in the QEMU terminal${c.opts.tcp ? ` (or tools/qemu/console.py ${c.opts.tcp} "APP ${groups[0].id}")` : ""}`);
  }
  if (c.opts.run === false) {
    say(green("ready") + (c.opts.install?.length ? `: ${show(card)} has the packages` : ""));
    return EXIT.OK;
  }
  const argv = [...(board === "28" ? ["--board", "28"] : []), ...(keep ? ["--keep"] : []), ...(c.opts.fresh && !c.opts.install?.length ? ["--fresh"] : []), ...passMode, ...c.rest];
  note(dim(`tools/qemu/run.sh ${argv.join(" ")}`));
  const env = { ...process.env, ...(q ? { QEMU: q } : {}) };
  const r = await run("sh", [runSh, ...argv], { cwd: root, env });
  return r.code === 0 ? EXIT.OK : EXIT.FAIL;
}

export const qemuCommand: CmdSpec = {
  name: "qemu",
  summary: "Run the real firmware in Espressif's QEMU (tools/qemu/run.sh), with packages put onto its card.",
  description:
    "Wraps tools/qemu/run.sh: builds env s3desk-qemu (s3desk-28-qemu for --board 28), puts it into build-qemu/flash.bin keeping the device's data (NVS link token, LittleFS, the card), and runs QEMU with the viewer. --install puts packages onto the card image in place (mtools, or hdiutil on macOS) before QEMU starts; with --fresh (or no image yet) the image is made first from sim/data-live, then the packages are added.",
  platform: "macOS (run.sh's viewer and mkimage.py's card use macOS tools); QEMU from Espressif (esp-develop-9.2.2) in ../tools-ext/qemu or $QEMU",
  opts: [
    { name: "board", type: "string", arg: "N", desc: "28 = the 2.8\" board (its own image, card, console port 5556, web 8081)" },
    { name: "install", short: "i", type: "string", arg: "PKG", desc: ".espk (or project folder) to put onto the card", repeat: true },
    { name: "fresh", type: "bool", desc: "a new image from sim/data-live (unlinked, the card made again)" },
    { name: "keep", type: "bool", desc: "the image as it is: no firmware build" },
    { name: "card", type: "string", arg: "IMG", desc: "the card image to change (default build-qemu/sd[-28].img)" },
    { name: "sdl", type: "bool", desc: "QEMU's own window (no mouse), the dev console on this terminal" },
    { name: "tcp", type: "number", arg: "PORT", desc: "QEMU's own window, the dev console on a TCP port (tools/qemu/console.py PORT \"APP id\")" },
    { name: "headless", type: "bool", desc: "no window, the dev console on port 5555 (5556 for --board 28)" },
    { name: "run", type: "bool", desc: "start QEMU (--no-run: only build/put the packages on the card)", default: true },
    { name: "force", type: "bool", desc: "install even when the checks find errors" },
  ],
  passthrough: "more arguments for tools/qemu/run.sh",
  notes: ["The card image must not change while QEMU runs: --install refuses when its monitor socket answers.", "Dev console in QEMU: APP <id>, TAP x y, DRAG ..., SHOT; screenshots: tools/qemu/console.py PORT shot:FILE.ppm."],
  examples: ["espos qemu", "espos qemu --install dist/vn.you.timer-1.0.0-core.espk", "espos qemu --board 28 --install . --headless", "espos qemu --install game.espk --no-run"],
  run: qemuCmd,
};
