// Water: counts glasses (250 ml) a day, 8 by default, and reminds every 2 hours in the day when you forget.
// Texts are English; TEXT has them in other languages (app.lang is the device's language: "vi", "de"...).
const TEXT = {
  vi: {
    'Water': 'Uống nước', 'Time for some water': 'Uống nước nào', '{0}/{1} glasses today': 'Đã {0}/{1} ly hôm nay',
    'Today': 'Hôm nay', ' · enough, well done': ' · đủ rồi, giỏi lắm', '+ 1 glass': '+ 1 ly', 'Daily goal': 'Mục tiêu mỗi ngày',
    '{0} glasses': '{0} ly', 'Last 7 days': '7 ngày qua',
  },
  'zh-Hans': {
    'Water': '喝水', 'Time for some water': '该喝水了', '{0}/{1} glasses today': '今天已喝 {0}/{1} 杯',
    'Today': '今天', ' · enough, well done': ' · 喝够了，真棒', '+ 1 glass': '+ 1 杯', 'Daily goal': '每日目标',
    '{0} glasses': '{0} 杯', 'Last 7 days': '最近 7 天',
  },
  es: {
    'Water': 'Agua', 'Time for some water': 'Hora de beber agua', '{0}/{1} glasses today': '{0}/{1} vasos hoy',
    'Today': 'Hoy', ' · enough, well done': ' · ¡meta cumplida!', '+ 1 glass': '+ 1 vaso', 'Daily goal': 'Meta diaria',
    '{0} glasses': '{0} vasos', 'Last 7 days': 'Últimos 7 días',
  },
  fr: {
    'Water': 'Eau', 'Time for some water': 'Pensez à boire', '{0}/{1} glasses today': "{0}/{1} verres aujourd'hui",
    'Today': "Aujourd'hui", ' · enough, well done': ' · objectif atteint, bravo', '+ 1 glass': '+ 1 verre',
    'Daily goal': 'Objectif du jour', '{0} glasses': '{0} verres', 'Last 7 days': '7 derniers jours',
  },
  de: {
    'Water': 'Wasser', 'Time for some water': 'Zeit für ein Glas Wasser', '{0}/{1} glasses today': 'Heute {0}/{1} Gläser',
    'Today': 'Heute', ' · enough, well done': ' · geschafft, super', '+ 1 glass': '+ 1 Glas', 'Daily goal': 'Tagesziel',
    '{0} glasses': '{0} Gläser', 'Last 7 days': 'Letzte 7 Tage',
  },
  ja: {
    'Water': '水分補給', 'Time for some water': 'お水を飲みましょう', '{0}/{1} glasses today': '今日は {0}/{1} 杯',
    'Today': '今日', ' · enough, well done': ' · 達成、よくできました', '+ 1 glass': '+ 1杯', 'Daily goal': '1日の目標',
    '{0} glasses': '{0}杯', 'Last 7 days': '過去7日間',
  },
  ko: {
    'Water': '물 마시기', 'Time for some water': '물 마실 시간이에요', '{0}/{1} glasses today': '오늘 {0}/{1}잔',
    'Today': '오늘', ' · enough, well done': ' · 목표 달성, 잘했어요', '+ 1 glass': '+ 1잔', 'Daily goal': '하루 목표',
    '{0} glasses': '{0}잔', 'Last 7 days': '최근 7일',
  },
  'pt-BR': {
    'Water': 'Água', 'Time for some water': 'Hora de beber água', '{0}/{1} glasses today': '{0}/{1} copos hoje',
    'Today': 'Hoje', ' · enough, well done': ' · meta batida, parabéns', '+ 1 glass': '+ 1 copo', 'Daily goal': 'Meta diária',
    '{0} glasses': '{0} copos', 'Last 7 days': 'Últimos 7 dias',
  },
  ru: {
    'Water': 'Вода', 'Time for some water': 'Пора выпить воды', '{0}/{1} glasses today': 'Сегодня выпито {0}/{1}',
    'Today': 'Сегодня', ' · enough, well done': ' · норма, молодец', '+ 1 glass': '+ 1 стакан', 'Daily goal': 'Цель на день',
    '{0} glasses': '{0} стак.', 'Last 7 days': 'За 7 дней',
  },
  id: {
    'Water': 'Minum air', 'Time for some water': 'Waktunya minum air', '{0}/{1} glasses today': '{0}/{1} gelas hari ini',
    'Today': 'Hari ini', ' · enough, well done': ' · cukup, hebat', '+ 1 glass': '+ 1 gelas', 'Daily goal': 'Target harian',
    '{0} glasses': '{0} gelas', 'Last 7 days': '7 hari terakhir',
  },
};
const t = (s, ...a) => ((TEXT[app.lang] || {})[s] || s).replace(/\{(\d)\}/g, (_, i) => a[i]);

const today = () => new Date().toISOString().slice(0, 10);
let s = store.get('s', {day: today(), cups: 0, goal: 8, last: 0, week: []});
function roll() {
  if (s.day === today()) return;
  s.week = (s.week || []).concat([{day: s.day, cups: s.cups}]).slice(-7);
  s.day = today(); s.cups = 0;
}
roll();
function add(n) {
  roll();
  s.cups = Math.max(0, s.cups + n);
  s.last = Date.now();
  store.set('s', s);
}
timer.every(60 * 1000, () => {
  roll();
  const h = new Date().getHours();
  if (h >= 8 && h < 21 && s.cups < s.goal && Date.now() - (s.last || 0) > 2 * 3600 * 1000) {
    notify(t('Time for some water'), t('{0}/{1} glasses today', s.cups, s.goal));
    s.last = Date.now();
    store.set('s', s);
  }
});

const GOALS = [6, 8, 10, 12];
app.title = t('Water');
app.render = () => {
  const pct = Math.min(1, s.cups / s.goal);
  return ui.column([
    ui.card([
      ui.text(t('Today'), {size: 13, dim: true, center: true}),
      ui.big(s.cups + '/' + s.goal, {color: '#0284c7'}),
      ui.text((s.cups * 250) + ' ml' + (s.cups >= s.goal ? t(' · enough, well done') : ''), {center: true, dim: true}),
      ui.progress(pct, {color: '#0ea5e9'}),
    ], {center: true}),
    ui.row([
      ui.button(t('+ 1 glass'), () => add(1), {bg: '#0284c7'}),
      ui.button('− 1', () => add(-1), {bg: '#9ca3af'}),
    ], {center: true}),
    ui.text(t('Daily goal'), {size: 13, dim: true}),
    ui.chips(GOALS.map(g => t('{0} glasses', g)), GOALS.indexOf(s.goal), (i) => { s.goal = GOALS[i]; store.set('s', s); }),
    ui.text(t('Last 7 days'), {size: 13, dim: true}),
    ui.list((s.week || []).slice().reverse().map(d => ({title: d.day.slice(8) + '/' + d.day.slice(5, 7), right: t('{0} glasses', d.cups)}))),
  ]);
};
