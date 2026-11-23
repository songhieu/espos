// Bubbles: a screen saver app (guide: espos.dev/docs/guides/saver-apps). The OS runs this file while the saver shows
// and calls app.onFrame(g, dt) up to app.fps(n) times a second; g draws one full-screen frame. A touch leaves the
// saver. The settings (manifest "settings") are read with settings.get, here and on the web.
const THEMES = {
  ocean: {top: '#03263b', bottom: '#0b4f6c', bubble: '#7dd3fc', date: '#bae6fd'},
  sunset: {top: '#2a0f2e', bottom: '#a8432a', bubble: '#fde68a', date: '#fed7aa'},
  night: {top: '#000000', bottom: '#111827', bubble: '#64748b', date: '#94a3b8'},
};
let W = 320, H = 480, theme = THEMES.ocean, speed = 4, showDate = true;
let bubbles = [];

// A bubble at a random place across the screen, at y (under the bottom edge when y is left out).
function bubble(y) {
  const r = 4 + Math.random() * 14;
  return {x: Math.random() * W, y: y === undefined ? H + r : y, r, rise: 0.5 + Math.random(), a: 0.2 + Math.random() * 0.35};
}

app.onStart = (info) => {
  W = info.w, H = info.h;
  theme = THEMES[settings.get('theme')] || THEMES.ocean;
  speed = settings.get('speed');
  showDate = settings.get('date');
  bubbles = [];
  for (let i = 0; i < settings.get('count'); i++) bubbles.push(bubble(Math.random() * H));
  app.fps(12);
};

app.onFrame = (g, dt) => {
  const step = (dt || 80) / 80 * speed / 2;
  g.gradient(0, 0, g.w, g.h, theme.top, theme.bottom);
  for (const b of bubbles) {
    b.y -= b.rise * step;
    b.x += Math.sin(b.y / 30) * 0.3 * step;
    if (b.y < -b.r) Object.assign(b, bubble());
    g.circle(b.x, b.y, b.r, theme.bubble, {a: b.a});
  }
  // The time moves a few pixels every minute, so no pixel shows the same thing all night (burn-in).
  const m = Number(app.time('mm'));
  const dx = [0, 3, 0, -3][m % 4], dy = [0, 0, 3, 3][(m >> 2) % 4];
  const y = g.h / 2 - 70 + dy;
  g.text(app.time('HH:mm'), g.w / 2 + dx, y, {size: 96, color: '#ffffff', align: 'center', bold: true});
  if (showDate) g.text(app.time('dd/MM/yyyy'), g.w / 2 + dx, y + 112, {size: 20, color: theme.date, align: 'center'});
};
