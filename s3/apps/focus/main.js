// Focus: Pomodoro. 25 minutes of work, 5 minutes of rest (15 after every 4 sessions).
// Texts are English; TEXT has them in other languages (app.lang is the device's language: "vi", "de"...).
const TEXT = {
  vi: {
    'Focus': 'Tập trung', 'Work': 'Làm việc', 'Short break': 'Nghỉ ngắn', 'Long break': 'Nghỉ dài',
    '25 minutes done': 'Hết 25 phút', 'Take a break: stand up and walk a little': 'Nghỉ chút nhé, đứng dậy đi lại một lát',
    'Break is over': 'Hết giờ nghỉ', 'Back to work': 'Quay lại làm việc nào', 'Pause': 'Tạm dừng', 'Start': 'Bắt đầu',
    'Reset': 'Làm lại', 'Today': 'Hôm nay', '{0} focus sessions': '{0} phiên tập trung',
    '{0} minutes of work': '{0} phút làm việc',
  },
  'zh-Hans': {
    'Focus': '专注', 'Work': '工作', 'Short break': '短休息', 'Long break': '长休息',
    '25 minutes done': '25 分钟到了', 'Take a break: stand up and walk a little': '休息一下，起来走走吧',
    'Break is over': '休息结束', 'Back to work': '继续工作吧', 'Pause': '暂停', 'Start': '开始',
    'Reset': '复位', 'Today': '今天', '{0} focus sessions': '专注 {0} 次',
    '{0} minutes of work': '工作 {0} 分钟',
  },
  es: {
    'Focus': 'Enfoque', 'Work': 'Trabajo', 'Short break': 'Pausa corta', 'Long break': 'Pausa larga',
    '25 minutes done': '25 minutos completados', 'Take a break: stand up and walk a little': 'Descansa: levántate y camina un poco',
    'Break is over': 'Fin del descanso', 'Back to work': 'A trabajar', 'Pause': 'Pausa', 'Start': 'Iniciar',
    'Reset': 'Reiniciar', 'Today': 'Hoy', '{0} focus sessions': 'Sesiones: {0}',
    '{0} minutes of work': '{0} min de trabajo',
  },
  fr: {
    'Focus': 'Focus', 'Work': 'Travail', 'Short break': 'Pause courte', 'Long break': 'Pause longue',
    '25 minutes done': '25 minutes écoulées', 'Take a break: stand up and walk a little': 'Faites une pause, marchez un peu',
    'Break is over': 'Fin de la pause', 'Back to work': 'Au travail', 'Pause': 'Pause', 'Start': 'Démarrer',
    'Reset': 'Réinitialiser', 'Today': "Aujourd'hui", '{0} focus sessions': 'Sessions : {0}',
    '{0} minutes of work': '{0} min de travail',
  },
  de: {
    'Focus': 'Fokus', 'Work': 'Arbeit', 'Short break': 'Kurze Pause', 'Long break': 'Lange Pause',
    '25 minutes done': '25 Minuten geschafft', 'Take a break: stand up and walk a little': 'Mach Pause: steh auf und geh ein paar Schritte',
    'Break is over': 'Pause vorbei', 'Back to work': 'Zurück an die Arbeit', 'Pause': 'Pause', 'Start': 'Start',
    'Reset': 'Zurücksetzen', 'Today': 'Heute', '{0} focus sessions': 'Fokus-Einheiten: {0}',
    '{0} minutes of work': '{0} Min. Arbeit',
  },
  ja: {
    'Focus': '集中', 'Work': '作業', 'Short break': '小休憩', 'Long break': '長休憩',
    '25 minutes done': '25分経過しました', 'Take a break: stand up and walk a little': '休憩です。立って少し歩きましょう',
    'Break is over': '休憩終了', 'Back to work': '作業に戻りましょう', 'Pause': '一時停止', 'Start': '開始',
    'Reset': 'リセット', 'Today': '今日', '{0} focus sessions': '集中 {0} 回',
    '{0} minutes of work': '作業 {0} 分',
  },
  ko: {
    'Focus': '집중', 'Work': '작업', 'Short break': '짧은 휴식', 'Long break': '긴 휴식',
    '25 minutes done': '25분 완료', 'Take a break: stand up and walk a little': '잠시 쉬어요. 일어나서 조금 걸어 보세요',
    'Break is over': '휴식 끝', 'Back to work': '다시 집중할 시간이에요', 'Pause': '일시 정지', 'Start': '시작',
    'Reset': '재설정', 'Today': '오늘', '{0} focus sessions': '집중 {0}회',
    '{0} minutes of work': '작업 {0}분',
  },
  'pt-BR': {
    'Focus': 'Foco', 'Work': 'Trabalho', 'Short break': 'Pausa curta', 'Long break': 'Pausa longa',
    '25 minutes done': '25 minutos concluídos', 'Take a break: stand up and walk a little': 'Faça uma pausa: levante e ande um pouco',
    'Break is over': 'Fim da pausa', 'Back to work': 'De volta ao trabalho', 'Pause': 'Pausar', 'Start': 'Iniciar',
    'Reset': 'Zerar', 'Today': 'Hoje', '{0} focus sessions': 'Sessões: {0}',
    '{0} minutes of work': '{0} min de trabalho',
  },
  ru: {
    'Focus': 'Фокус', 'Work': 'Работа', 'Short break': 'Перерыв', 'Long break': 'Долгий перерыв',
    '25 minutes done': '25 минут прошло', 'Take a break: stand up and walk a little': 'Сделайте перерыв: встаньте и пройдитесь',
    'Break is over': 'Перерыв окончен', 'Back to work': 'Пора за работу', 'Pause': 'Пауза', 'Start': 'Старт',
    'Reset': 'Сброс', 'Today': 'Сегодня', '{0} focus sessions': 'Сессий: {0}',
    '{0} minutes of work': 'Работа: {0} мин',
  },
  id: {
    'Focus': 'Fokus', 'Work': 'Kerja', 'Short break': 'Rehat pendek', 'Long break': 'Rehat panjang',
    '25 minutes done': '25 menit selesai', 'Take a break: stand up and walk a little': 'Rehat dulu: berdiri dan jalan sebentar',
    'Break is over': 'Rehat selesai', 'Back to work': 'Ayo kerja lagi', 'Pause': 'Jeda', 'Start': 'Mulai',
    'Reset': 'Atur ulang', 'Today': 'Hari ini', '{0} focus sessions': '{0} sesi fokus',
    '{0} minutes of work': '{0} menit kerja',
  },
};
const t = (s, ...a) => ((TEXT[app.lang] || {})[s] || s).replace(/\{(\d)\}/g, (_, i) => a[i]);

const MODES = [
  {name: 'Work', min: 25, color: '#e0383b'},
  {name: 'Short break', min: 5, color: '#16a34a'},
  {name: 'Long break', min: 15, color: '#2563eb'},
];
let mode = 0, left = MODES[0].min * 60, running = false, tick = 0;
const today = () => new Date().toISOString().slice(0, 10);
let stats = store.get('stats', {day: today(), done: 0});
if (stats.day !== today()) stats = {day: today(), done: 0};

const mmss = (s) => String(Math.floor(s / 60)).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
function setMode(m) { mode = m; left = MODES[m].min * 60; stop(); }
function stop() { running = false; if (tick) { timer.cancel(tick); tick = 0; } }
function start() {
  if (running) return;
  running = true;
  tick = timer.every(1000, () => {
    left--;
    if (left > 0) return;
    stop();
    sound.beep();
    if (mode === 0) {
      stats.done++;
      store.set('stats', stats);
      notify(t('25 minutes done'), t('Take a break: stand up and walk a little'));
      setMode(stats.done % 4 === 0 ? 2 : 1);
    } else {
      notify(t('Break is over'), t('Back to work'));
      setMode(0);
    }
  });
}

app.title = t('Focus');
// Landscape (app.screen): the timer and today's count side by side (half cards), the buttons under them.
app.render = () => {
  const land = app.screen && app.screen.landscape;
  const timer = ui.card([
    ui.text(t(MODES[mode].name), {size: 15, bold: true, color: MODES[mode].color, center: true}),
    ui.big(mmss(left), {color: MODES[mode].color}),
    ui.progress(1 - left / (MODES[mode].min * 60), {color: MODES[mode].color}),
  ], {center: true, half: true});
  const buttons = ui.row([
    running ? ui.button(t('Pause'), stop, {bg: '#6b7280'}) : ui.button(t('Start'), start, {bg: MODES[mode].color}),
    ui.button(t('Reset'), () => setMode(mode), {bg: '#9ca3af'}),
  ], {center: true});
  const today = ui.card([
    ui.text(t('Today'), {size: 13, dim: true}),
    ui.text(t('{0} focus sessions', stats.done), {size: 20, bold: true}),
    ui.text(t('{0} minutes of work', stats.done * 25), {dim: true}),
  ], {half: true});
  const chips = ui.chips(MODES.map(m => t(m.name)), mode, (i) => setMode(i));
  return land ? ui.column([chips, ui.row([timer, today]), buttons]) : ui.column([chips, timer, buttons, today]);
};
