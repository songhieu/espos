// Output, errors and exit codes shared by every command.
//
// Human text goes to stdout; warnings and progress to stderr. With --json, stdout carries exactly one JSON document
// and everything else goes to stderr, so `espos check --json app | jq` always works.

export const EXIT = {
  OK: 0,
  /** the command ran and found problems or a step failed (validation errors, build or test failures) */
  FAIL: 1,
  /** bad arguments */
  USAGE: 2,
  /** a tool or file the command needs is missing (python3, pio, the simulator, the firmware repo, serialport) */
  MISSING: 3,
  /** no EspOS S3 found, or a serial port refused for safety */
  DEVICE: 4,
} as const;

export class CliError extends Error {
  constructor(message: string, readonly code: number = EXIT.FAIL, readonly hint?: string) {
    super(message);
  }
}

export const state = { json: false, quiet: false, color: process.stdout.isTTY && !process.env.NO_COLOR };

const paint = (code: string) => (s: string) => (state.color && process.stderr.isTTY ? `\x1b[${code}m${s}\x1b[0m` : s);
export const red = paint("31");
export const yellow = paint("33");
export const green = paint("32");
export const dim = paint("2");
export const bold = paint("1");

/** Normal output (stderr in --json mode). */
export function say(...a: unknown[]): void {
  if (state.quiet) return;
  (state.json ? process.stderr : process.stdout).write(a.join(" ") + "\n");
}
/** Progress and notes: always stderr. */
export function note(...a: unknown[]): void {
  if (state.quiet) return;
  process.stderr.write(a.join(" ") + "\n");
}
export function warn(...a: unknown[]): void {
  process.stderr.write(yellow("warning: ") + a.join(" ") + "\n");
}
/** The one JSON document of a --json run. */
export function emitJson(v: unknown): void {
  process.stdout.write(JSON.stringify(v, null, 2) + "\n");
}
