// A countdown on the app's own timer (timer.every runs only while the app is open).

export function format(seconds: number): string {
  const m = Math.floor(seconds / 60);
  const s = seconds % 60;
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

export class Countdown {
  left: number;
  private total: number;
  private id = 0;

  constructor(seconds: number) {
    this.total = this.left = seconds;
  }

  get running(): boolean {
    return this.id !== 0;
  }

  /** 0 at the start, 1 at the end */
  get done(): number {
    return 1 - this.left / this.total;
  }

  start(onEnd: () => void): void {
    if (this.id) return;
    this.id = timer.every(1000, () => {
      this.left = Math.max(0, this.left - 1);
      if (this.left === 0) {
        this.stop();
        onEnd();
      }
    });
  }

  stop(): void {
    if (this.id) timer.cancel(this.id);
    this.id = 0;
  }

  reset(seconds: number): void {
    this.stop();
    this.total = this.left = seconds;
  }
}
