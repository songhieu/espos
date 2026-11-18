// Vocabulary: English word cards in Leitner boxes (right moves a card up a box, wrong sends it back to box 1).
// Texts are English; TEXT has them in other languages (app.lang is the device's language: "vi", "de"...).
const TEXT = {
  vi: {
    'Vocabulary': 'Từ vựng', 'Done for today': 'Xong bài hôm nay', 'Come back tomorrow to review.': 'Mai quay lại ôn tiếp nhé.',
    'Learned {0}/{1} words': 'Đã thuộc {0}/{1} từ', '{0} cards left today · {1} reviewed': 'Còn {0} thẻ hôm nay · đã ôn {1}',
    'Tap "Show meaning" once you know it': 'Chạm "Xem nghĩa" khi đã nghĩ ra', 'Not yet': 'Chưa nhớ', 'Got it': 'Nhớ rồi',
    'Show meaning': 'Xem nghĩa',
  },
  'zh-Hans': {
    'Vocabulary': '背单词', 'Done for today': '今天完成了', 'Come back tomorrow to review.': '明天再来复习。',
    'Learned {0}/{1} words': '已记住 {0}/{1} 个单词', '{0} cards left today · {1} reviewed': '今天还剩 {0} 张 · 已复习 {1}',
    'Tap "Show meaning" once you know it': '想好后点按“显示释义”', 'Not yet': '还没记住', 'Got it': '记住了',
    'Show meaning': '显示释义',
  },
  es: {
    'Vocabulary': 'Palabras', 'Done for today': 'Listo por hoy', 'Come back tomorrow to review.': 'Vuelve mañana a repasar.',
    'Learned {0}/{1} words': '{0}/{1} palabras aprendidas', '{0} cards left today · {1} reviewed': 'Quedan {0} tarjetas · {1} repasadas',
    'Tap "Show meaning" once you know it': 'Toca "Ver significado" cuando lo sepas', 'Not yet': 'Aún no', 'Got it': 'Lo sé',
    'Show meaning': 'Ver significado',
  },
  fr: {
    'Vocabulary': 'Mots', 'Done for today': "Fini pour aujourd'hui", 'Come back tomorrow to review.': 'Revenez demain pour réviser.',
    'Learned {0}/{1} words': '{0}/{1} mots appris', '{0} cards left today · {1} reviewed': 'Encore {0} cartes · {1} révisées',
    'Tap "Show meaning" once you know it': 'Réfléchissez, puis touchez « Voir le sens »', 'Not yet': 'Pas encore',
    'Got it': 'Je sais', 'Show meaning': 'Voir le sens',
  },
  de: {
    'Vocabulary': 'Vokabeln', 'Done for today': 'Für heute fertig', 'Come back tomorrow to review.': 'Komm morgen zum Wiederholen.',
    'Learned {0}/{1} words': '{0}/{1} Wörter gelernt', '{0} cards left today · {1} reviewed': 'Noch {0} Karten · {1} geübt',
    'Tap "Show meaning" once you know it': 'Tippe »Bedeutung zeigen«, wenn du es weißt', 'Not yet': 'Noch nicht',
    'Got it': 'Gewusst', 'Show meaning': 'Bedeutung zeigen',
  },
  ja: {
    'Vocabulary': '英単語', 'Done for today': '今日はここまで', 'Come back tomorrow to review.': '明日また復習しましょう。',
    'Learned {0}/{1} words': '覚えた単語 {0}/{1}', '{0} cards left today · {1} reviewed': '残り {0} 枚 · 復習 {1}',
    'Tap "Show meaning" once you know it': '思い出したら「意味を見る」をタップ', 'Not yet': 'まだ', 'Got it': '覚えた',
    'Show meaning': '意味を見る',
  },
  ko: {
    'Vocabulary': '영단어', 'Done for today': '오늘은 여기까지', 'Come back tomorrow to review.': '내일 다시 복습해요.',
    'Learned {0}/{1} words': '외운 단어 {0}/{1}', '{0} cards left today · {1} reviewed': '남은 카드 {0}장 · 복습 {1}장',
    'Tap "Show meaning" once you know it': '떠오르면 ‘뜻 보기’를 누르세요', 'Not yet': '아직', 'Got it': '외웠어요',
    'Show meaning': '뜻 보기',
  },
  'pt-BR': {
    'Vocabulary': 'Palavras', 'Done for today': 'Por hoje é só', 'Come back tomorrow to review.': 'Volte amanhã para revisar.',
    'Learned {0}/{1} words': '{0}/{1} palavras aprendidas', '{0} cards left today · {1} reviewed': 'Faltam {0} cartões · {1} revisados',
    'Tap "Show meaning" once you know it': 'Toque em "Ver significado" quando souber', 'Not yet': 'Ainda não',
    'Got it': 'Já sei', 'Show meaning': 'Ver significado',
  },
  ru: {
    'Vocabulary': 'Слова', 'Done for today': 'На сегодня всё', 'Come back tomorrow to review.': 'Завтра повторим снова.',
    'Learned {0}/{1} words': 'Выучено слов: {0}/{1}', '{0} cards left today · {1} reviewed': 'Сегодня осталось {0} · повторено {1}',
    'Tap "Show meaning" once you know it': 'Вспомнили? Нажмите «Показать значение»', 'Not yet': 'Не помню',
    'Got it': 'Помню', 'Show meaning': 'Показать значение',
  },
  id: {
    'Vocabulary': 'Kosakata', 'Done for today': 'Hari ini selesai', 'Come back tomorrow to review.': 'Kembali besok untuk mengulang.',
    'Learned {0}/{1} words': '{0}/{1} kata dihafal', '{0} cards left today · {1} reviewed': 'Sisa {0} kartu · {1} diulang',
    'Tap "Show meaning" once you know it': 'Ketuk "Lihat arti" jika sudah ingat', 'Not yet': 'Belum', 'Got it': 'Hafal',
    'Show meaning': 'Lihat arti',
  },
};
const t = (s, ...a) => ((TEXT[app.lang] || {})[s] || s).replace(/\{(\d)\}/g, (_, i) => a[i]);
// Word, its meaning in English, in Vietnamese (MEANING picks the column for the device's language).
const WORDS = [
  ['ephemeral', 'lasting a very short time', 'chóng tàn, phù du'], ['resilient', 'quick to recover', 'kiên cường, mau phục hồi'],
  ['meticulous', 'very careful with details', 'tỉ mỉ'], ['candid', 'honest and direct', 'thẳng thắn'],
  ['diligent', 'hard-working and careful', 'siêng năng'], ['eloquent', 'speaking well and clearly', 'hùng biện, lưu loát'],
  ['frugal', 'careful with money', 'tiết kiệm'], ['gratitude', 'the feeling of being thankful', 'lòng biết ơn'],
  ['humble', 'not proud', 'khiêm tốn'], ['inevitable', 'sure to happen', 'không tránh khỏi'],
  ['jeopardize', 'to put in danger', 'gây nguy hiểm'], ['keen', 'eager; sharp', 'háo hức, sắc bén'],
  ['lenient', 'not strict', 'khoan dung'], ['mundane', 'ordinary, dull', 'tầm thường'],
  ['nostalgia', 'fond longing for the past', 'nỗi hoài niệm'], ['obsolete', 'no longer used', 'lỗi thời'],
  ['persevere', 'to keep going despite trouble', 'kiên trì'], ['quaint', 'charmingly old-fashioned', 'cổ kính, lạ mắt'],
  ['reluctant', 'unwilling', 'miễn cưỡng'], ['serene', 'calm and peaceful', 'thanh bình'],
  ['tedious', 'long and boring', 'tẻ nhạt'], ['ubiquitous', 'found everywhere', 'có mặt khắp nơi'],
  ['vivid', 'bright and clear', 'sống động'], ['wary', 'careful about danger', 'cảnh giác'],
  ['yearn', 'to want very much', 'khao khát'], ['zealous', 'full of energy for a cause', 'nhiệt tình'],
  ['abundant', 'more than enough', 'dồi dào'], ['brisk', 'quick; pleasantly cold', 'nhanh nhẹn, mát mẻ'],
  ['cozy', 'warm and comfortable', 'ấm cúng'], ['delicate', 'easily broken; fine', 'mỏng manh, tinh tế'],
  ['eager', 'wanting to do something', 'háo hức'], ['fragile', 'easily broken', 'dễ vỡ'],
  ['genuine', 'real, sincere', 'chân thật'], ['hectic', 'very busy', 'bận rộn'],
  ['itinerary', 'a plan of a journey', 'lịch trình'], ['juggle', 'to handle several things at once', 'xoay xở nhiều việc'],
  ['knack', 'a natural skill', 'sở trường'], ['leisure', 'free time', 'thời gian rảnh'],
  ['mellow', 'soft and gentle', 'êm dịu'], ['nurture', 'to care for and help grow', 'nuôi dưỡng'],
  ['outgoing', 'friendly and social', 'hướng ngoại'], ['punctual', 'on time', 'đúng giờ'],
  ['quirky', 'odd in a charming way', 'kỳ quặc dễ thương'], ['remedy', 'a cure or fix', 'phương thuốc, cách chữa'],
  ['sturdy', 'strong and solid', 'chắc chắn'], ['thrive', 'to grow well', 'phát triển mạnh'],
  ['upbeat', 'cheerful, hopeful', 'lạc quan'], ['versatile', 'able to do many things', 'đa năng'],
  ['wholesome', 'good for you', 'lành mạnh'], ['spontaneous', 'done without planning', 'tự phát'],
];
const MEANING = app.lang === 'vi' ? 2 : 1;
const DAYS = [0, 1, 2, 4, 7, 15];  // review again after this many days, by box 1..5
const dayNo = () => Math.floor(Date.now() / 86400000);
let st = store.get('st', {});  // word -> [box, nextDay]
let cur = -1, shown = false, done = 0;

function due() {
  const d = dayNo();
  const list = [];
  for (let i = 0; i < WORDS.length; i++) {
    const s = st[WORDS[i][0]];
    if (!s || s[1] <= d) list.push(i);
  }
  return list;
}
function next() {
  const list = due();
  shown = false;
  cur = list.length ? list[Math.floor(Math.random() * Math.min(list.length, 8))] : -1;
}
function answer(ok) {
  const w = WORDS[cur][0];
  const box = ok ? Math.min(5, ((st[w] || [0])[0]) + 1) : 1;
  st[w] = [box, dayNo() + DAYS[box]];
  store.set('st', st);
  done++;
  next();
}
next();

app.title = t('Vocabulary');
app.render = () => {
  const learned = Object.values(st).filter(s => s[0] >= 3).length;
  if (cur < 0) return ui.column([
    ui.card([ui.title(t('Done for today'), {center: true}), ui.text(t('Come back tomorrow to review.'), {center: true, dim: true})], {center: true}),
    ui.text(t('Learned {0}/{1} words', learned, WORDS.length), {center: true}),
  ]);
  return ui.column([
    ui.text(t('{0} cards left today · {1} reviewed', due().length, done), {size: 13, dim: true}),
    ui.card([
      ui.title(WORDS[cur][0], {center: true, size: 24, color: '#15803d'}),
      ui.spacer(6),
      shown ? ui.text(WORDS[cur][MEANING], {center: true, size: 18}) : ui.text(t('Tap "Show meaning" once you know it'), {center: true, dim: true}),
    ], {center: true}),
    shown
      ? ui.row([ui.button(t('Not yet'), () => answer(false), {bg: '#9ca3af'}), ui.button(t('Got it'), () => answer(true), {bg: '#16a34a'})], {center: true})
      : ui.button(t('Show meaning'), () => { shown = true; }, {bg: '#15803d', wide: true}),
    ui.progress(learned / WORDS.length, {color: '#22c55e'}),
    ui.text(t('Learned {0}/{1} words', learned, WORDS.length), {size: 13, dim: true}),
  ]);
};
