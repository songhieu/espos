// {{name}}: a countdown timer with a reminder that fires even when the app is closed.
// EspOS JavaScript API level 1, typed by @espos/sdk (types/espos.d.ts). `espos build` type-checks
// this file and bundles it with its imports into one main.js.
import { Countdown, format } from './countdown';

const CHOICES = [1, 5, 10, 25]; // minutes
let pick = store.get<number>('pick', 2);
const clock = new Countdown(CHOICES[pick] * 60);

function start(): void {
  clock.start(() => {
    sound.chime(); // "sound" permission
    toast('Time is up');
    app.refresh();
  });
  // shown even if the app is closed meanwhile ("notify" permission); the same tag replaces an older one
  notify.schedule(Date.now() + clock.left * 1000, '{{name}}', 'Time is up', { tag: 'end' });
}

function stop(): void {
  clock.stop();
  notify.cancel('end');
}

app.title = '{{name}}';
app.render = () =>
  ui.column(
    [
      ui.chips(CHOICES.map((m) => `${m} min`), pick, (i) => {
        if (clock.running) return;
        pick = i;
        store.set('pick', i);
        clock.reset(CHOICES[i] * 60);
      }),
      ui.card([ui.big(format(clock.left), { color: clock.running ? '#e0383b' : '#2563eb' }), ui.progress(clock.done)], { center: true }),
      clock.running
        ? ui.button('Stop', stop, { wide: true, bg: '#6b7280' })
        : ui.button('Start', start, { wide: true }),
    ],
    { gap: 12 },
  );
