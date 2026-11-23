// Bubbles as an app on Home: what it is, and its settings. The saver itself is saver.js.
// Its words in the device's language (app.lang), English otherwise.
const T = {
  en: ['Bubbles', 'A screen saver: bubbles rise behind the time.', 'Choose it in Settings, Default apps, Screen saver. Touch the saver to come back.', 'Settings'],
  vi: ['Bong bóng', 'Một màn chờ: bong bóng nổi lên sau đồng hồ.', 'Chọn trong Cài đặt, Ứng dụng mặc định, Màn chờ. Chạm vào màn chờ để quay lại.', 'Cài đặt'],
  'zh-Hans': ['泡泡', '一个屏保：泡泡在时间后面缓缓升起。', '在“设置”的“默认应用”中选择“屏保”。轻触屏保即可返回。', '设置'],
  es: ['Burbujas', 'Un salvapantallas: burbujas que suben detrás de la hora.', 'Elígelo en Ajustes, Apps predeterminadas, Salvapantallas. Toca el salvapantallas para volver.', 'Ajustes'],
  fr: ['Bulles', "Un écran de veille : des bulles montent derrière l'heure.", "Choisissez-le dans Réglages, Apps par défaut, Écran de veille. Touchez l'écran de veille pour revenir.", 'Réglages'],
  de: ['Blasen', 'Ein Bildschirmschoner: Blasen steigen hinter der Uhrzeit auf.', 'Wähle ihn in den Einstellungen unter Standard-Apps, Bildschirmschoner. Tippe auf den Schoner, um zurückzukehren.', 'Einstellungen'],
  ja: ['バブル', 'スクリーンセーバー：時刻の後ろを泡が昇っていきます。', '設定の「デフォルトのアプリ」で「スクリーンセーバー」に選んでください。タッチすると戻ります。', '設定'],
  ko: ['버블', '화면 보호기: 시간 뒤로 거품이 올라갑니다.', '설정의 기본 앱에서 화면 보호기로 고르세요. 화면 보호기를 터치하면 돌아옵니다.', '설정'],
  'pt-BR': ['Bolhas', 'Um protetor de tela: bolhas sobem atrás da hora.', 'Escolha em Ajustes, Apps padrão, Protetor de tela. Toque no protetor para voltar.', 'Ajustes'],
  ru: ['Пузыри', 'Заставка: пузыри поднимаются за часами.', 'Выберите её в Настройках, «Приложения по умолчанию», «Заставка». Коснитесь заставки, чтобы вернуться.', 'Настройки'],
  id: ['Gelembung', 'Sebuah screensaver: gelembung naik di belakang jam.', 'Pilih di Pengaturan, App default, Screensaver. Sentuh screensaver untuk kembali.', 'Pengaturan'],
};
const W = T[app.lang] || T.en;
app.title = W[0];
app.render = () => ui.column([
  ui.card([
    ui.title(W[0]),
    ui.text(W[1]),
    ui.text(W[2], {dim: true}),
  ]),
  ui.button(W[3], () => settings.open(), {wide: true}),
]);
