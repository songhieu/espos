// Built by apps/sessions/tools/build.py from src/app.js and i18n.json: edit those, not this file.
var TR = (function () {
  var K = ["In Claude Code, a line that starts with ! runs as a shell command at once, without Claude.","This deletes files on your Mac. It cannot be undone from here.","This rewrites or throws away git history.","This runs with administrator rights on your Mac.","This downloads a script and runs it.","This deletes data in a database.","This can wipe a disk.","This force-stops programs.","This opens file permissions to everyone.","This clears or ends the Claude session.","This stops what Claude is doing right now.","Approving lets Claude do this on your Mac now.","Yes","No","Continue","Explain more","now","{n} min","{n} h","Needs approval","Your turn","Working","Done","Error","Sent","Sent to the Mac","Waiting for you to confirm it on the Mac (60 s)","The Mac does not take commands from this device yet. On the Mac: EspOS Desktop > Settings > Devices, turn this device on.","The Mac is offline. Nothing was sent.","This is turned off in EspOS Desktop on the Mac.","The Mac is in view-only mode. Nothing was sent.","The session is no longer waiting.","That question was already answered or has expired.","Too many in a row. Wait a moment.","No connection. Nothing was sent.","Not sent: {why}","Not sent ({code}).","This needs EspOS AI.","Nothing can be sent while the Mac is offline","View only: answer it in iTerm on the Mac.","Use Approve or Deny for this request","Hold {key} for a second to stop Claude","Sent {key}","Type or say a few words first.","Nothing was heard.","Dictation did not work ({code}).","Not sent.","Not sent. Tap Send when it is right.","Text size {px}","Claude wants to edit a file","Claude wants to run a command","Deny","Approve","Mode","View only. Turned on in EspOS Desktop on the Mac: you can watch, not type or approve.","Try again","Message Claude","Send","Stop Claude?","Approve deleting files?","Approve this?","Approve a risky command?","This goes straight to the shell on your Mac","Send a risky line?","Cancel","Stop Claude","Approve delete","Send anyway","Your words","AI is writing a clear reply","AI could not write it now. Try again.","Clear reply","Written by AI from your words and the end of this session. Check it before you send.","Use mine","Send this","Send this?","Say OK to send it, or tap Send.","Looking for your Mac","One moment.","Connect your Mac","1. Install EspOS Desktop from espos.dev/desktop.","2. Open it: it shows a 6-digit code.","3. On espos.dev, open Devices, tap Add and type the code.","Your Claude Code sessions then show up here.","Check again","Sessions are paused","EspOS has paused computer control for everyone for a while. Your Mac and Claude keep working as usual. Try again later.","This device is not linked","Link it to your EspOS account on espos.dev, then open Sessions again.","Getting this app ready","EspOS has not seen Sessions on this device yet. It takes up to a minute after installing.","No internet connection.","Cannot reach EspOS right now.","Too many requests. Trying again soon.","EspOS had a problem. Trying again.","No computer is paired with this account yet. Install EspOS Desktop from espos.dev/desktop.","emergency stop","offline since {t}","view only","online","Emergency stop on {mac}","EspOS Desktop disconnected itself. Nothing is shown or sent until someone turns it back on at the Mac.","Your Mac is offline. This is what it sent last; nothing can be sent until it is back.","No Claude sessions right now","Start claude in iTerm2 on your Mac and it shows up here within seconds.","Sessions","This session has ended","Opening the session","It is no longer on the Mac.","Mac offline since {t}. This is the last screen it sent.","The Mac is in view-only mode","This session is not in iTerm2","Terminal sharing is off on this Mac","You still see the state, the last line and permission requests. To see the terminal, turn on \"Share terminal text\" in EspOS Desktop, under Data sent.","Last line","↓ {n} new lines"];
  var V = {"vi":["Trong Claude Code, dòng bắt đầu bằng ! sẽ chạy ngay như lệnh shell, không qua Claude.","Lệnh này xoá tệp trên Mac của bạn. Không thể hoàn tác từ đây.","Lệnh này viết lại hoặc xoá bỏ lịch sử git.","Lệnh này chạy với quyền quản trị trên Mac của bạn.","Lệnh này tải một script về rồi chạy nó.","Lệnh này xoá dữ liệu trong cơ sở dữ liệu.","Lệnh này có thể xoá sạch một ổ đĩa.","Lệnh này buộc các chương trình phải dừng.","Lệnh này mở quyền truy cập tệp cho mọi người.","Lệnh này xoá trắng hoặc kết thúc phiên Claude.","Việc này dừng ngay những gì Claude đang làm.","Duyệt là Claude được làm việc này trên Mac ngay.","Có","Không","Tiếp tục","Giải thích thêm","vừa xong","{n} phút","{n} giờ","Chờ duyệt","Tới lượt bạn","Đang làm","Xong","Lỗi","Đã gửi","Đã gửi tới Mac","Đang chờ bạn xác nhận trên Mac (60 giây)","Mac chưa cho máy này ra lệnh. Trên Mac: EspOS Desktop > Cài đặt > Máy ra lệnh, bật máy này.","Mac đang ngoại tuyến. Chưa gửi gì cả.","Tính năng này đang tắt trong EspOS Desktop trên Mac.","Mac đang ở chế độ chỉ xem. Chưa gửi gì cả.","Phiên này không còn chờ nữa.","Câu hỏi đó đã được trả lời hoặc đã hết hạn.","Bạn gửi liên tục quá nhiều. Đợi một chút nhé.","Không có kết nối. Chưa gửi gì cả.","Chưa gửi: {why}","Chưa gửi được ({code}).","Cần EspOS AI để dùng tính năng này.","Không gửi được gì khi Mac đang ngoại tuyến","Chỉ xem: hãy trả lời trong iTerm trên Mac.","Dùng nút Duyệt hoặc Từ chối cho yêu cầu này","Giữ {key} một giây để dừng Claude","Đã gửi {key}","Hãy gõ hoặc nói vài từ trước đã.","Không nghe thấy gì.","Nhập bằng giọng nói không thành công ({code}).","Chưa gửi.","Chưa gửi. Bấm Gửi khi đã đúng.","Cỡ chữ {px}","Claude muốn sửa một tệp","Claude muốn chạy một lệnh","Từ chối","Duyệt","Chế độ","Chỉ xem. Đã bật trong EspOS Desktop trên Mac: bạn xem được, nhưng không gõ hay duyệt được.","Thử lại","Nhắn Claude","Gửi","Dừng Claude?","Duyệt xoá tệp?","Duyệt việc này?","Duyệt lệnh rủi ro?","Dòng này chạy thẳng trong shell trên Mac của bạn","Gửi dòng lệnh rủi ro?","Huỷ","Dừng Claude","Duyệt xoá","Vẫn gửi","Lời của bạn","AI đang viết câu trả lời rõ ràng","Lúc này AI chưa viết được. Thử lại nhé.","Câu trả lời rõ ràng","AI viết từ lời của bạn và phần cuối của phiên này. Hãy kiểm tra trước khi gửi.","Dùng lời tôi","Gửi bản này","Gửi câu này?","Nói OK để gửi, hoặc bấm Gửi.","Đang tìm Mac của bạn","Chờ một chút.","Kết nối Mac của bạn","1. Cài EspOS Desktop từ espos.dev/desktop.","2. Mở ứng dụng: nó hiện một mã 6 số.","3. Trên espos.dev, mở Thiết bị, chạm Thêm rồi nhập mã.","Sau đó các phiên Claude Code của bạn sẽ hiện ở đây.","Kiểm tra lại","Phiên Claude đang tạm dừng","EspOS đã tạm dừng điều khiển máy tính cho mọi người trong một lúc. Mac và Claude của bạn vẫn chạy bình thường. Hãy thử lại sau.","Máy này chưa được liên kết","Liên kết máy với tài khoản EspOS trên espos.dev, rồi mở lại Phiên Claude.","Đang chuẩn bị ứng dụng","EspOS chưa thấy Phiên Claude trên máy này. Sau khi cài có thể mất tới một phút.","Không có kết nối Internet.","Hiện không kết nối được tới EspOS.","Quá nhiều yêu cầu. Sẽ thử lại ngay sau đây.","EspOS gặp sự cố. Đang thử lại.","Chưa có máy tính nào được ghép với tài khoản này. Cài EspOS Desktop từ espos.dev/desktop.","dừng khẩn cấp","ngoại tuyến từ {t}","chỉ xem","trực tuyến","Dừng khẩn cấp trên {mac}","EspOS Desktop đã tự ngắt kết nối. Sẽ không hiện hay gửi gì cho tới khi có người bật lại trên Mac.","Mac của bạn đang ngoại tuyến. Đây là nội dung nó gửi lần cuối; chưa gửi được gì cho tới khi Mac kết nối lại.","Hiện chưa có phiên Claude nào","Chạy claude trong iTerm2 trên Mac, vài giây sau phiên sẽ hiện ở đây.","Phiên Claude","Phiên này đã kết thúc","Đang mở phiên","Phiên không còn trên Mac nữa.","Mac ngoại tuyến từ {t}. Đây là màn hình cuối cùng nó gửi.","Mac đang ở chế độ chỉ xem","Phiên này không chạy trong iTerm2","Mac này đang tắt chia sẻ terminal","Bạn vẫn thấy trạng thái, dòng cuối và các yêu cầu cấp quyền. Để xem terminal, hãy bật \"Chia sẻ nội dung terminal\" trong EspOS Desktop, ở mục \"Dữ liệu gửi đi\".","Dòng cuối","↓ {n} dòng mới"],"zh-Hans":["在 Claude Code 中，以 ! 开头的一行会立即作为 shell 命令运行，不经过 Claude。","这会删除你 Mac 上的文件，在这里无法撤销。","这会改写或丢弃 git 历史。","这会在你的 Mac 上以管理员权限运行。","这会下载一个脚本并运行它。","这会删除数据库中的数据。","这可能会清空一个磁盘。","这会强制停止程序。","这会把文件权限开放给所有人。","这会清空或结束 Claude 会话。","这会让 Claude 立即停下手头的工作。","批准后 Claude 会立即在你的 Mac 上执行。","是","否","继续","详细解释","刚刚","{n} 分钟","{n} 小时","待批准","轮到你了","工作中","已完成","出错","已发送","已发送到 Mac","等你在 Mac 上确认（60 秒）","这台 Mac 还不接受此设备的指令。在 Mac 上：EspOS Desktop > Settings > Devices，打开此设备。","Mac 已离线，未发送任何内容。","此功能已在 Mac 上的 EspOS Desktop 中关闭。","Mac 处于仅查看模式，未发送任何内容。","该会话已不再等待。","这个问题已回答或已过期。","操作太频繁，请稍等片刻。","没有网络连接，未发送任何内容。","未发送：{why}","未发送（{code}）。","此功能需要 EspOS AI。","Mac 离线时无法发送任何内容","仅查看：请在 Mac 上的 iTerm 中回答。","请用“批准”或“拒绝”回复这个请求","按住 {key} 一秒以停止 Claude","已发送 {key}","请先输入或说几个字。","没有听到声音。","语音输入失败（{code}）。","未发送。","未发送。确认无误后点“发送”。","文字大小 {px}","Claude 想编辑一个文件","Claude 想运行一条命令","拒绝","批准","模式","仅查看。已在 Mac 上的 EspOS Desktop 中开启：你可以查看，但不能输入或批准。","重试","给 Claude 发消息","发送","停止 Claude？","批准删除文件？","批准吗？","批准有风险的命令？","这会直接发送到你 Mac 上的 shell","发送有风险的命令行？","取消","停止 Claude","批准删除","仍然发送","你的原话","AI 正在写一条清楚的回复","AI 暂时写不出来，请重试。","清楚的回复","由 AI 根据你的原话和此会话的结尾写成。发送前请检查。","用我的原话","发送这条","发送这段吗？","说“好”即可发送，或点“发送”。","正在查找你的 Mac","请稍候。","连接你的 Mac","1. 从 espos.dev/desktop 安装 EspOS Desktop。","2. 打开它：它会显示一个 6 位数代码。","3. 在 espos.dev 上打开“Devices”，点按“Add”并输入代码。","然后你的 Claude Code 会话就会显示在这里。","再次检查","会话已暂停","EspOS 暂时为所有人暂停了电脑控制。你的 Mac 和 Claude 照常运行。请稍后再试。","此设备尚未关联","请在 espos.dev 上将它关联到你的 EspOS 账户，然后重新打开会话。","正在准备此应用","EspOS 还没在这台设备上看到会话应用。安装后最多需要一分钟。","没有网络连接。","暂时无法连接 EspOS。","请求过多，稍后会重试。","EspOS 出了问题，正在重试。","此账户尚未配对任何电脑。请从 espos.dev/desktop 安装 EspOS Desktop。","紧急停止","自 {t} 起离线","仅查看","在线","{mac} 已紧急停止","EspOS Desktop 已自行断开连接。在有人于 Mac 上重新开启之前，不会显示或发送任何内容。","你的 Mac 已离线。这是它最后发送的内容；在它重新上线前无法发送任何内容。","当前没有 Claude 会话","在 Mac 上的 iTerm2 中启动 claude，几秒内就会显示在这里。","会话","此会话已结束","正在打开会话","它已不在 Mac 上。","Mac 自 {t} 起离线。这是它最后发送的画面。","Mac 处于仅查看模式","此会话不在 iTerm2 中","此 Mac 已关闭终端共享","你仍能看到状态、最后一行和权限请求。要查看终端，请在 EspOS Desktop 的“发送的数据”中开启“共享终端文字”。","最后一行","↓ {n} 行新内容"],"es":["En Claude Code, una línea que empieza por ! se ejecuta al instante como comando de shell, sin Claude.","Esto borra archivos de tu Mac. No se puede deshacer desde aquí.","Esto reescribe o descarta el historial de git.","Esto se ejecuta con permisos de administrador en tu Mac.","Esto descarga un script y lo ejecuta.","Esto borra datos de una base de datos.","Esto puede borrar un disco entero.","Esto fuerza el cierre de programas.","Esto abre los permisos de archivos a todo el mundo.","Esto borra o termina la sesión de Claude.","Esto detiene lo que Claude está haciendo ahora mismo.","Al aprobar, Claude lo hará ahora en tu Mac.","Sí","No","Continuar","Explica más","ahora","{n} min","{n} h","Por aprobar","Tu turno","Trabajando","Listo","Error","Enviado","Enviado al Mac","Esperando a que lo confirmes en el Mac (60 s)","El Mac aún no acepta órdenes de este dispositivo. En el Mac: EspOS Desktop > Settings > Devices, activa este dispositivo.","El Mac está desconectado. No se envió nada.","Esto está desactivado en EspOS Desktop en el Mac.","El Mac está en modo solo vista. No se envió nada.","La sesión ya no está esperando.","Esa pregunta ya se respondió o ha caducado.","Demasiados seguidos. Espera un momento.","Sin conexión. No se envió nada.","No enviado: {why}","No enviado ({code}).","Esto requiere EspOS AI.","No se puede enviar nada mientras el Mac está desconectado","Solo vista: responde en iTerm en el Mac.","Usa Aprobar o Rechazar para esta solicitud","Mantén {key} un segundo para detener a Claude","{key} enviado","Escribe o di unas palabras primero.","No se oyó nada.","El dictado no funcionó ({code}).","No se envió.","No se envió. Toca Enviar cuando esté bien.","Tamaño del texto {px}","Claude quiere editar un archivo","Claude quiere ejecutar un comando","Rechazar","Aprobar","Modo","Solo vista. Activado en EspOS Desktop en el Mac: puedes mirar, pero no escribir ni aprobar.","Reintentar","Escribe a Claude","Enviar","¿Detener a Claude?","¿Aprobar borrar archivos?","¿Aprobar esto?","¿Aprobar un comando arriesgado?","Esto va directo a la shell de tu Mac","¿Enviar una línea arriesgada?","Cancelar","Detener Claude","Sí, borrar","Enviar igual","Tus palabras","La IA está escribiendo una respuesta clara","La IA no pudo escribirlo ahora. Inténtalo de nuevo.","Respuesta clara","Escrito por IA a partir de tus palabras y el final de esta sesión. Revísalo antes de enviarlo.","Usar el mío","Enviar esto","¿Enviar esto?","Di OK para enviarlo, o toca Enviar.","Buscando tu Mac","Un momento.","Conecta tu Mac","1. Instala EspOS Desktop desde espos.dev/desktop.","2. Ábrelo: muestra un código de 6 dígitos.","3. En espos.dev, abre \"Devices\", toca \"Add\" y escribe el código.","Después, tus sesiones de Claude Code aparecerán aquí.","Comprobar","Las sesiones están en pausa","EspOS ha pausado el control de los equipos para todos durante un rato. Tu Mac y Claude siguen funcionando como siempre. Inténtalo más tarde.","Este dispositivo no está vinculado","Vincúlalo a tu cuenta de EspOS en espos.dev y vuelve a abrir Sesiones.","Preparando esta app","EspOS aún no ve Sesiones en este dispositivo. Tarda hasta un minuto tras instalarla.","Sin conexión a internet.","No se puede conectar con EspOS ahora.","Demasiadas solicitudes. Se reintentará pronto.","EspOS tuvo un problema. Reintentando.","Aún no hay ningún equipo vinculado a esta cuenta. Instala EspOS Desktop desde espos.dev/desktop.","parada de emergencia","desconectado desde {t}","solo vista","conectado","Parada de emergencia en {mac}","EspOS Desktop se desconectó por sí mismo. No se muestra ni se envía nada hasta que alguien lo vuelva a activar en el Mac.","Tu Mac está desconectado. Esto es lo último que envió; no se puede enviar nada hasta que vuelva.","No hay sesiones de Claude ahora","Inicia claude en iTerm2 en tu Mac y aparecerá aquí en segundos.","Sesiones","Esta sesión ha terminado","Abriendo la sesión","Ya no está en el Mac.","Mac desconectado desde {t}. Es la última pantalla que envió.","El Mac está en modo solo vista","Esta sesión no está en iTerm2","Este Mac no comparte el terminal","Sigues viendo el estado, la última línea y las solicitudes de permiso. Para ver el terminal, activa \"Compartir texto del terminal\" en EspOS Desktop, en \"Datos enviados\".","Última línea","↓ {n} líneas nuevas"],"fr":["Dans Claude Code, une ligne qui commence par ! s'exécute aussitôt comme commande shell, sans Claude.","Cela supprime des fichiers sur votre Mac. Impossible d'annuler depuis ici.","Cela réécrit ou supprime l'historique git.","Cela s'exécute avec les droits d'administrateur sur votre Mac.","Cela télécharge un script et l'exécute.","Cela supprime des données dans une base de données.","Cela peut effacer un disque.","Cela force l'arrêt de programmes.","Cela ouvre les droits des fichiers à tout le monde.","Cela efface ou termine la session Claude.","Cela arrête ce que Claude est en train de faire.","En approuvant, Claude le fait maintenant sur votre Mac.","Oui","Non","Continuer","Expliquer plus","à l'instant","{n} min","{n} h","À approuver","À vous","En cours","Terminé","Erreur","Envoyé","Envoyé au Mac","En attente de votre confirmation sur le Mac (60 s)","Le Mac n'accepte pas encore les commandes de cet appareil. Sur le Mac : EspOS Desktop > Settings > Devices, activez cet appareil.","Le Mac est hors ligne. Rien n'a été envoyé.","Cette option est désactivée dans EspOS Desktop sur le Mac.","Le Mac est en mode lecture seule. Rien n'a été envoyé.","La session n'attend plus.","Cette question a déjà reçu une réponse ou a expiré.","Trop d'envois d'affilée. Patientez un instant.","Pas de connexion. Rien n'a été envoyé.","Non envoyé : {why}","Non envoyé ({code}).","Cela nécessite EspOS AI.","Rien ne peut être envoyé tant que le Mac est hors ligne","Lecture seule : répondez dans iTerm sur le Mac.","Utilisez Approuver ou Refuser pour cette demande","Maintenez {key} une seconde pour arrêter Claude","{key} envoyé","Tapez ou dites d'abord quelques mots.","Rien n'a été entendu.","La dictée n'a pas fonctionné ({code}).","Pas envoyé.","Pas envoyé. Touchez Envoyer quand c'est bon.","Taille du texte {px}","Claude veut modifier un fichier","Claude veut exécuter une commande","Refuser","Approuver","Mode","Lecture seule. Activée dans EspOS Desktop sur le Mac : vous pouvez regarder, mais pas taper ni approuver.","Réessayer","Message à Claude","Envoyer","Arrêter Claude ?","Approuver la suppression de fichiers ?","Approuver ?","Approuver une commande risquée ?","Cela part directement dans le shell de votre Mac","Envoyer une ligne risquée ?","Annuler","Arrêter Claude","Oui, supprimer","Oui, envoyer","Vos mots","L'IA rédige une réponse claire","L'IA n'a pas pu rédiger pour l'instant. Réessayez.","Réponse claire","Rédigé par l'IA à partir de vos mots et de la fin de cette session. Vérifiez avant d'envoyer.","Mon texte","Envoyer ceci","Envoyer ceci ?","Dites OK pour l'envoyer, ou touchez Envoyer.","Recherche de votre Mac","Un instant.","Connectez votre Mac","1. Installez EspOS Desktop depuis espos.dev/desktop.","2. Ouvrez-le : il affiche un code à 6 chiffres.","3. Sur espos.dev, ouvrez « Devices », touchez « Add » et saisissez le code.","Vos sessions Claude Code apparaîtront ensuite ici.","Revérifier","Sessions en pause","EspOS a mis en pause le contrôle des ordinateurs pour tout le monde pendant un moment. Votre Mac et Claude continuent de fonctionner normalement. Réessayez plus tard.","Cet appareil n'est pas associé","Associez-le à votre compte EspOS sur espos.dev, puis rouvrez Sessions.","Préparation de l'app","EspOS ne voit pas encore Sessions sur cet appareil. Cela prend jusqu'à une minute après l'installation.","Pas de connexion Internet.","Impossible de joindre EspOS pour le moment.","Trop de requêtes. Nouvel essai bientôt.","EspOS a rencontré un problème. Nouvel essai.","Aucun ordinateur n'est encore associé à ce compte. Installez EspOS Desktop depuis espos.dev/desktop.","arrêt d'urgence","hors ligne depuis {t}","lecture seule","en ligne","Arrêt d'urgence sur {mac}","EspOS Desktop s'est déconnecté de lui-même. Rien n'est affiché ni envoyé tant que quelqu'un ne le réactive pas sur le Mac.","Votre Mac est hors ligne. Voici ce qu'il a envoyé en dernier ; rien ne peut être envoyé avant son retour.","Aucune session Claude pour l'instant","Lancez claude dans iTerm2 sur votre Mac : la session apparaît ici en quelques secondes.","Sessions","Cette session est terminée","Ouverture de la session","Elle n'est plus sur le Mac.","Mac hors ligne depuis {t}. Voici le dernier écran qu'il a envoyé.","Le Mac est en mode lecture seule","Cette session n'est pas dans iTerm2","Le partage du terminal est désactivé sur ce Mac","Vous voyez toujours l'état, la dernière ligne et les demandes d'autorisation. Pour voir le terminal, activez « Partager le texte du terminal » dans EspOS Desktop, sous « Données envoyées ».","Dernière ligne","↓ {n} nouvelles lignes"],"de":["In Claude Code wird eine Zeile, die mit ! beginnt, sofort als Shell-Befehl ausgeführt, ohne Claude.","Das löscht Dateien auf deinem Mac. Von hier aus lässt es sich nicht rückgängig machen.","Das schreibt den Git-Verlauf um oder verwirft ihn.","Das läuft mit Administratorrechten auf deinem Mac.","Das lädt ein Skript herunter und führt es aus.","Das löscht Daten in einer Datenbank.","Das kann ein Laufwerk komplett löschen.","Das beendet Programme zwangsweise.","Das gibt Dateirechte für alle frei.","Das leert oder beendet die Claude-Sitzung.","Das stoppt, was Claude gerade tut.","Wenn du erlaubst, macht Claude das jetzt auf deinem Mac.","Ja","Nein","Weiter","Mehr erklären","gerade","{n} Min.","{n} Std.","Freigabe nötig","Du bist dran","Arbeitet","Fertig","Fehler","Gesendet","An den Mac gesendet","Warte auf deine Bestätigung am Mac (60 s)","Der Mac nimmt noch keine Befehle von diesem Gerät an. Auf dem Mac: EspOS Desktop > Settings > Devices, dieses Gerät einschalten.","Der Mac ist offline. Es wurde nichts gesendet.","Das ist in EspOS Desktop auf dem Mac ausgeschaltet.","Der Mac ist im Nur-Ansehen-Modus. Es wurde nichts gesendet.","Die Sitzung wartet nicht mehr.","Diese Frage wurde schon beantwortet oder ist abgelaufen.","Zu viele auf einmal. Warte kurz.","Keine Verbindung. Es wurde nichts gesendet.","Nicht gesendet: {why}","Nicht gesendet ({code}).","Dafür brauchst du EspOS AI.","Solange der Mac offline ist, kann nichts gesendet werden","Nur ansehen: Antworte in iTerm auf dem Mac.","Nutze Erlauben oder Ablehnen für diese Anfrage","Halte {key} eine Sekunde, um Claude zu stoppen","{key} gesendet","Tippe oder sprich zuerst ein paar Wörter.","Nichts gehört.","Diktieren hat nicht geklappt ({code}).","Nicht gesendet.","Nicht gesendet. Tippe auf Senden, wenn es passt.","Textgröße {px}","Claude möchte eine Datei bearbeiten","Claude möchte einen Befehl ausführen","Ablehnen","Erlauben","Modus","Nur ansehen. In EspOS Desktop auf dem Mac eingeschaltet: Du kannst zusehen, aber nicht tippen oder erlauben.","Neu versuchen","Nachricht an Claude","Senden","Claude stoppen?","Löschen von Dateien erlauben?","Das erlauben?","Riskanten Befehl erlauben?","Das geht direkt an die Shell auf deinem Mac","Riskante Zeile senden?","Abbrechen","Claude stoppen","Ja, löschen","Ja, senden","Deine Worte","KI schreibt eine klare Antwort","Die KI konnte es gerade nicht schreiben. Versuch es noch mal.","Klare Antwort","Von der KI aus deinen Worten und dem Ende dieser Sitzung geschrieben. Prüf es vor dem Senden.","Mein Text","Das senden","Das senden?","Sag OK zum Senden oder tippe auf Senden.","Suche deinen Mac","Einen Moment.","Verbinde deinen Mac","1. Installiere EspOS Desktop von espos.dev/desktop.","2. Öffne es: Es zeigt einen 6-stelligen Code.","3. Öffne auf espos.dev »Devices«, tippe auf »Add« und gib den Code ein.","Danach erscheinen deine Sitzungen von Claude Code hier.","Erneut prüfen","Sitzungen pausiert","EspOS hat die Computersteuerung für alle eine Weile pausiert. Dein Mac und Claude arbeiten ganz normal weiter. Versuch es später noch mal.","Dieses Gerät ist nicht verbunden","Verbinde es auf espos.dev mit deinem EspOS-Konto und öffne Sitzungen dann erneut.","App wird vorbereitet","EspOS sieht Sitzungen auf diesem Gerät noch nicht. Nach der Installation dauert das bis zu einer Minute.","Keine Internetverbindung.","EspOS ist gerade nicht erreichbar.","Zu viele Anfragen. Neuer Versuch gleich.","EspOS hatte ein Problem. Neuer Versuch.","Mit diesem Konto ist noch kein Computer gekoppelt. Installiere EspOS Desktop von espos.dev/desktop.","Notstopp","offline seit {t}","nur ansehen","online","Notstopp auf {mac}","EspOS Desktop hat sich selbst getrennt. Nichts wird angezeigt oder gesendet, bis jemand es am Mac wieder einschaltet.","Dein Mac ist offline. Das hat er zuletzt gesendet; es kann nichts gesendet werden, bis er wieder da ist.","Gerade keine Claude-Sitzungen","Starte claude in iTerm2 auf deinem Mac, dann erscheint es hier in wenigen Sekunden.","Sitzungen","Diese Sitzung ist beendet","Sitzung wird geöffnet","Sie ist nicht mehr auf dem Mac.","Mac offline seit {t}. Das ist der letzte Bildschirm, den er gesendet hat.","Der Mac ist im Nur-Ansehen-Modus","Diese Sitzung läuft nicht in iTerm2","Terminalfreigabe ist auf diesem Mac aus","Du siehst weiterhin den Status, die letzte Zeile und Berechtigungsanfragen. Um das Terminal zu sehen, schalte in EspOS Desktop unter »Gesendete Daten« die Option »Terminaltext teilen« ein.","Letzte Zeile","↓ {n} neue Zeilen"],"ja":["Claude Code では、! で始まる行は Claude を通さず、すぐにシェルコマンドとして実行されます。","Mac のファイルを削除します。ここからは元に戻せません。","git の履歴を書き換えるか、破棄します。","Mac 上で管理者権限で実行されます。","スクリプトをダウンロードして実行します。","データベースのデータを削除します。","ディスクを消去するおそれがあります。","プログラムを強制終了します。","ファイルの権限を全員に開放します。","Claude のセッションをクリアまたは終了します。","Claude が今している作業を止めます。","承認すると Claude が今すぐ Mac で実行します。","はい","いいえ","続ける","もっと詳しく","たった今","{n}分","{n}時間","承認待ち","あなたの番","作業中","完了","エラー","送信済み","Mac に送信しました","Mac での確認を待っています（60 秒）","Mac はまだこのデバイスからの操作を受け付けていません。Mac で EspOS Desktop > Settings > Devices を開き、このデバイスをオンにしてください。","Mac がオフラインです。何も送信されていません。","Mac の EspOS Desktop でオフになっています。","Mac は閲覧のみモードです。何も送信されていません。","このセッションはもう待っていません。","その質問は回答済みか、期限切れです。","連続で送りすぎです。少し待ってください。","接続がありません。何も送信されていません。","送信できませんでした：{why}","送信できませんでした（{code}）。","EspOS AI が必要です。","Mac がオフラインの間は何も送信できません","閲覧のみ：Mac の iTerm で回答してください。","このリクエストには「承認」か「拒否」を使ってください","{key} を1秒押し続けると Claude を止めます","{key} を送信しました","先に少し入力するか、話してください。","何も聞こえませんでした。","音声入力に失敗しました（{code}）。","送信していません。","送信していません。正しければ送信をタップ。","文字サイズ {px}","Claude がファイルを編集しようとしています","Claude がコマンドを実行しようとしています","拒否","承認","モード","閲覧のみ。Mac の EspOS Desktop でオンになっています。見ることはできますが、入力や承認はできません。","再試行","Claude にメッセージ","送信","Claude を止めますか？","ファイルの削除を承認しますか？","承認しますか？","危険なコマンドを承認しますか？","これは Mac のシェルに直接送られます","危険な行を送信しますか？","キャンセル","Claude を止める","削除を承認","このまま送信","あなたの言葉","AI がわかりやすい返信を書いています","AI は今は書けませんでした。もう一度お試しください。","わかりやすい返信","あなたの言葉とこのセッションの最後の部分をもとに AI が書きました。送信する前に確認してください。","自分の文を使う","これを送信","これを送信しますか？","「OK」と言うと送信します。または送信をタップ。","Mac を探しています","少しお待ちください。","Mac を接続","1. espos.dev/desktop から EspOS Desktop をインストールします。","2. 開くと 6 桁のコードが表示されます。","3. espos.dev で「Devices」を開き、「Add」をタップしてコードを入力します。","その後、Claude Code のセッションがここに表示されます。","再確認","セッションは一時停止中です","EspOS は全員のコンピュータ操作をしばらく一時停止しています。Mac と Claude はいつもどおり動いています。後でもう一度お試しください。","このデバイスはまだ連携されていません","espos.dev で EspOS アカウントに連携してから、セッションを開き直してください。","アプリを準備しています","EspOS はまだこのデバイスでセッションを確認していません。インストール後、最大 1 分かかります。","インターネットに接続されていません。","現在 EspOS に接続できません。","リクエストが多すぎます。まもなく再試行します。","EspOS で問題が発生しました。再試行しています。","このアカウントにはまだコンピュータがペアリングされていません。espos.dev/desktop から EspOS Desktop をインストールしてください。","緊急停止","{t} からオフライン","閲覧のみ","オンライン","{mac} で緊急停止","EspOS Desktop が自ら接続を切りました。Mac で誰かがもう一度オンにするまで、何も表示・送信されません。","Mac がオフラインです。これは最後に送られた内容です。復帰するまで何も送信できません。","今は Claude のセッションがありません","Mac の iTerm2 で claude を起動すると、数秒でここに表示されます。","セッション","このセッションは終了しました","セッションを開いています","Mac 上にもうありません。","Mac は {t} からオフラインです。これは最後に送られた画面です。","Mac は閲覧のみモードです","このセッションは iTerm2 にありません","この Mac ではターミナル共有がオフです","状態、最後の行、権限リクエストは引き続き表示されます。ターミナルを見るには、EspOS Desktop の「送信するデータ」で「ターミナルのテキストを共有」をオンにしてください。","最後の行","↓ 新しい行 {n} 行"],"ko":["Claude Code에서 !로 시작하는 줄은 Claude를 거치지 않고 바로 셸 명령으로 실행돼요.","Mac의 파일을 삭제해요. 여기서는 되돌릴 수 없어요.","git 기록을 다시 쓰거나 버려요.","Mac에서 관리자 권한으로 실행돼요.","스크립트를 내려받아 실행해요.","데이터베이스의 데이터를 삭제해요.","디스크를 지울 수 있어요.","프로그램을 강제로 종료해요.","파일 권한을 모두에게 열어요.","Claude 세션을 비우거나 끝내요.","Claude가 지금 하는 일을 멈춰요.","승인하면 Claude가 지금 Mac에서 실행해요.","예","아니요","계속","자세히 설명","방금","{n}분","{n}시간","승인 필요","내 차례","작업 중","완료","오류","보냄","Mac으로 보냈어요","Mac에서 확인하기를 기다리는 중 (60초)","Mac이 아직 이 기기의 명령을 받지 않습니다. Mac에서 EspOS Desktop > Settings > Devices로 가서 이 기기를 켜세요.","Mac이 오프라인이에요. 아무것도 보내지 않았어요.","Mac의 EspOS Desktop에서 꺼져 있어요.","Mac이 보기 전용 모드예요. 아무것도 보내지 않았어요.","세션이 더 이상 기다리지 않아요.","이미 답했거나 만료된 질문이에요.","너무 연달아 보냈어요. 잠시 기다려 주세요.","연결이 없어요. 아무것도 보내지 않았어요.","보내지 못함: {why}","보내지 못했어요 ({code}).","EspOS AI가 필요해요.","Mac이 오프라인이면 아무것도 보낼 수 없어요","보기 전용: Mac의 iTerm에서 답해 주세요.","이 요청은 승인 또는 거부 버튼으로 답하세요","Claude를 멈추려면 {key}를 1초 누르세요","{key} 보냄","먼저 몇 마디 입력하거나 말해 주세요.","아무 소리도 들리지 않았어요.","받아쓰기가 안 됐어요 ({code}).","보내지 않았어요.","보내지 않았어요. 맞으면 보내기를 누르세요.","글자 크기 {px}","Claude가 파일을 수정하려고 해요","Claude가 명령을 실행하려고 해요","거부","승인","모드","보기 전용. Mac의 EspOS Desktop에서 켜져 있어요. 볼 수는 있지만 입력하거나 승인할 수는 없어요.","다시 시도","Claude에게 메시지","보내기","Claude를 멈출까요?","파일 삭제를 승인할까요?","승인할까요?","위험한 명령을 승인할까요?","Mac의 셸로 바로 전달돼요","위험한 줄을 보낼까요?","취소","Claude 멈추기","삭제 승인","그래도 보내기","내가 한 말","AI가 깔끔한 답장을 쓰는 중","AI가 지금은 쓸 수 없어요. 다시 시도해 주세요.","깔끔한 답장","내가 한 말과 이 세션의 마지막 부분을 바탕으로 AI가 썼어요. 보내기 전에 확인하세요.","내 글 사용","이걸로 보내기","이걸 보낼까요?","OK라고 말하면 보내요. 아니면 보내기를 누르세요.","Mac을 찾는 중","잠시만요.","Mac 연결하기","1. espos.dev/desktop에서 EspOS Desktop을 설치하세요.","2. 앱을 열면 6자리 코드가 보여요.","3. espos.dev에서 \"Devices\"를 열고 \"Add\"를 누른 뒤 코드를 입력하세요.","그러면 Claude Code 세션이 여기에 나타나요.","다시 확인","세션이 일시 중지됨","EspOS가 모든 사용자의 컴퓨터 제어를 잠시 멈췄어요. Mac과 Claude는 평소대로 작동해요. 나중에 다시 시도하세요.","이 기기는 아직 연결되지 않았어요","espos.dev에서 EspOS 계정에 연결한 뒤 세션을 다시 여세요.","앱 준비 중","EspOS가 아직 이 기기에서 세션 앱을 확인하지 못했어요. 설치 후 최대 1분 걸려요.","인터넷 연결이 없어요.","지금은 EspOS에 연결할 수 없어요.","요청이 너무 많아요. 곧 다시 시도해요.","EspOS에 문제가 생겼어요. 다시 시도하는 중이에요.","이 계정에 연결된 컴퓨터가 아직 없어요. espos.dev/desktop에서 EspOS Desktop을 설치하세요.","긴급 정지","{t}부터 오프라인","보기 전용","온라인","{mac} 긴급 정지","EspOS Desktop이 스스로 연결을 끊었어요. 누군가 Mac에서 다시 켤 때까지 아무것도 표시하거나 보내지 않아요.","Mac이 오프라인이에요. 마지막으로 보낸 내용이에요. 다시 연결될 때까지 아무것도 보낼 수 없어요.","지금은 Claude 세션이 없어요","Mac의 iTerm2에서 claude를 시작하면 몇 초 안에 여기에 나타나요.","세션","이 세션은 끝났어요","세션 여는 중","더 이상 Mac에 없어요.","Mac이 {t}부터 오프라인이에요. 마지막으로 보낸 화면이에요.","Mac이 보기 전용 모드예요","이 세션은 iTerm2에 없어요","이 Mac에서 터미널 공유가 꺼져 있어요","상태, 마지막 줄, 권한 요청은 계속 볼 수 있어요. 터미널을 보려면 EspOS Desktop의 \"보내는 데이터\"에서 \"터미널 텍스트 공유\"를 켜세요.","마지막 줄","↓ 새 줄 {n}개"],"pt-BR":["No Claude Code, uma linha que começa com ! é executada na hora como comando do shell, sem o Claude.","Isso apaga arquivos do seu Mac. Não dá para desfazer daqui.","Isso reescreve ou descarta o histórico do git.","Isso roda com permissões de administrador no seu Mac.","Isso baixa um script e o executa.","Isso apaga dados de um banco de dados.","Isso pode apagar um disco inteiro.","Isso força o fechamento de programas.","Isso abre as permissões de arquivos para todo mundo.","Isso limpa ou encerra a sessão do Claude.","Isso interrompe o que o Claude está fazendo agora.","Ao aprovar, o Claude faz isso agora no seu Mac.","Sim","Não","Continuar","Explique mais","agora","{n} min","{n} h","Aguarda aprovação","Sua vez","Trabalhando","Concluído","Erro","Enviado","Enviado ao Mac","Aguardando sua confirmação no Mac (60 s)","O Mac ainda não aceita comandos deste dispositivo. No Mac: EspOS Desktop > Settings > Devices, ative este dispositivo.","O Mac está offline. Nada foi enviado.","Isso está desligado no EspOS Desktop do Mac.","O Mac está no modo só visualização. Nada foi enviado.","A sessão não está mais esperando.","Essa pergunta já foi respondida ou expirou.","Muitos envios seguidos. Espere um pouco.","Sem conexão. Nada foi enviado.","Não enviado: {why}","Não enviado ({code}).","Isso precisa do EspOS AI.","Nada pode ser enviado enquanto o Mac estiver offline","Só visualização: responda no iTerm do Mac.","Use Aprovar ou Negar para este pedido","Segure {key} por um segundo para parar o Claude","{key} enviado","Digite ou fale algumas palavras primeiro.","Nada foi ouvido.","O ditado não funcionou ({code}).","Não enviado.","Não enviado. Toque em Enviar quando estiver certo.","Tamanho do texto {px}","O Claude quer editar um arquivo","O Claude quer executar um comando","Recusar","Aprovar","Modo","Só visualização. Ativado no EspOS Desktop do Mac: você pode ver, mas não digitar nem aprovar.","Tentar de novo","Mensagem para o Claude","Enviar","Parar o Claude?","Aprovar apagar arquivos?","Aprovar isto?","Aprovar um comando arriscado?","Isso vai direto para o shell do seu Mac","Enviar uma linha arriscada?","Cancelar","Parar o Claude","Sim, apagar","Enviar assim","Suas palavras","A IA está escrevendo uma resposta clara","A IA não conseguiu escrever agora. Tente de novo.","Resposta clara","Escrito pela IA a partir das suas palavras e do final desta sessão. Confira antes de enviar.","Usar o meu","Enviar esta","Enviar isto?","Diga OK para enviar, ou toque em Enviar.","Procurando seu Mac","Um momento.","Conecte seu Mac","1. Instale o EspOS Desktop em espos.dev/desktop.","2. Abra o app: ele mostra um código de 6 dígitos.","3. No espos.dev, abra \"Devices\", toque em \"Add\" e digite o código.","Depois, suas sessões do Claude Code aparecem aqui.","Verificar","As sessões estão pausadas","O EspOS pausou o controle de computadores para todos por um tempo. Seu Mac e o Claude continuam funcionando normalmente. Tente mais tarde.","Este dispositivo não está vinculado","Vincule-o à sua conta EspOS em espos.dev e abra Sessões de novo.","Preparando este app","O EspOS ainda não viu Sessões neste dispositivo. Leva até um minuto após a instalação.","Sem conexão com a internet.","Não foi possível acessar o EspOS agora.","Muitas solicitações. Tentando de novo em breve.","O EspOS teve um problema. Tentando de novo.","Nenhum computador está pareado com esta conta ainda. Instale o EspOS Desktop em espos.dev/desktop.","parada de emergência","offline desde {t}","só visualização","online","Parada de emergência em {mac}","O EspOS Desktop se desconectou sozinho. Nada é mostrado nem enviado até alguém ligá-lo de novo no Mac.","Seu Mac está offline. Isto é o que ele enviou por último; nada pode ser enviado até ele voltar.","Nenhuma sessão do Claude agora","Inicie o claude no iTerm2 do seu Mac e ele aparece aqui em segundos.","Sessões","Esta sessão terminou","Abrindo a sessão","Ela não está mais no Mac.","Mac offline desde {t}. Esta é a última tela que ele enviou.","O Mac está no modo só visualização","Esta sessão não está no iTerm2","O compartilhamento do terminal está desligado neste Mac","Você ainda vê o estado, a última linha e os pedidos de permissão. Para ver o terminal, ative \"Compartilhar texto do terminal\" no EspOS Desktop, em \"Dados enviados\".","Última linha","↓ {n} linhas novas"],"ru":["В Claude Code строка, которая начинается с !, сразу выполняется как команда оболочки, без Claude.","Это удалит файлы на вашем Mac. Отменить это отсюда нельзя.","Это перепишет или удалит историю git.","Это запустится с правами администратора на вашем Mac.","Это скачает скрипт и запустит его.","Это удалит данные в базе данных.","Это может стереть диск.","Это принудительно закроет программы.","Это откроет доступ к файлам для всех.","Это очистит или завершит сессию Claude.","Это остановит то, что Claude делает прямо сейчас.","Если разрешить, Claude сразу сделает это на вашем Mac.","Да","Нет","Продолжить","Подробнее","только что","{n} мин","{n} ч","Ждёт одобрения","Ваш ход","Работает","Готово","Ошибка","Отправлено","Отправлено на Mac","Ждём вашего подтверждения на Mac (60 с)","Mac пока не принимает команды с этого устройства. На Mac: EspOS Desktop > Settings > Devices, включите это устройство.","Mac не в сети. Ничего не отправлено.","Это отключено в EspOS Desktop на Mac.","Mac в режиме только просмотра. Ничего не отправлено.","Сессия больше не ждёт.","На этот вопрос уже ответили, или он устарел.","Слишком много подряд. Подождите немного.","Нет связи. Ничего не отправлено.","Не отправлено: {why}","Не отправлено ({code}).","Для этого нужен EspOS AI.","Пока Mac не в сети, ничего отправить нельзя","Только просмотр: ответьте в iTerm на Mac.","Ответьте на этот запрос кнопкой «Разрешить» или «Отклонить»","Удерживайте {key} секунду, чтобы остановить Claude","{key} отправлено","Сначала напишите или скажите пару слов.","Ничего не слышно.","Диктовка не сработала ({code}).","Не отправлено.","Не отправлено. Нажмите «Отправить», когда всё верно.","Размер текста {px}","Claude хочет изменить файл","Claude хочет выполнить команду","Отклонить","Одобрить","Режим","Только просмотр. Включено в EspOS Desktop на Mac: можно смотреть, но не печатать и не одобрять.","Повторить","Сообщение Claude","Отправить","Остановить Claude?","Разрешить удаление файлов?","Разрешить?","Одобрить рискованную команду?","Это сразу попадёт в оболочку на вашем Mac","Отправить рискованную строку?","Отмена","Остановить","Да, удалить","Да, отправить","Ваши слова","ИИ пишет понятный ответ","ИИ сейчас не смог написать. Попробуйте ещё раз.","Понятный ответ","Написано ИИ по вашим словам и концу этой сессии. Проверьте перед отправкой.","Мой текст","Отправить это","Отправить это?","Скажите «OK», чтобы отправить, или нажмите «Отправить».","Ищем ваш Mac","Минутку.","Подключите Mac","1. Установите EspOS Desktop с espos.dev/desktop.","2. Откройте его: он покажет 6-значный код.","3. На espos.dev откройте «Devices», нажмите «Add» и введите код.","Затем здесь появятся ваши сессии Claude Code.","Проверить","Сессии приостановлены","EspOS на время приостановил управление компьютерами для всех. Ваш Mac и Claude работают как обычно. Попробуйте позже.","Это устройство не привязано","Привяжите его к аккаунту EspOS на espos.dev и снова откройте «Сеансы».","Готовим приложение","EspOS ещё не видит «Сеансы» на этом устройстве. После установки это занимает до минуты.","Нет подключения к интернету.","Сейчас не удаётся связаться с EspOS.","Слишком много запросов. Скоро повторим.","У EspOS проблема. Повторяем.","К этому аккаунту ещё не привязан ни один компьютер. Установите EspOS Desktop с espos.dev/desktop.","аварийная остановка","не в сети с {t}","только просмотр","в сети","Аварийная остановка на {mac}","EspOS Desktop сам отключился. Ничего не показывается и не отправляется, пока его снова не включат на Mac.","Ваш Mac не в сети. Это последнее, что он отправил; отправить ничего нельзя, пока он не вернётся.","Сейчас нет сессий Claude","Запустите claude в iTerm2 на Mac, и сессия появится здесь через несколько секунд.","Сессии","Эта сессия завершена","Открываем сессию","Её больше нет на Mac.","Mac не в сети с {t}. Это последний экран, который он отправил.","Mac в режиме только просмотра","Эта сессия не в iTerm2","На этом Mac отключён показ терминала","Вы по-прежнему видите статус, последнюю строку и запросы разрешений. Чтобы видеть терминал, включите «Показывать текст терминала» в EspOS Desktop, в разделе «Отправляемые данные».","Последняя строка","↓ новых строк: {n}"],"id":["Di Claude Code, baris yang diawali ! langsung dijalankan sebagai perintah shell, tanpa Claude.","Ini menghapus file di Mac Anda. Tidak bisa dibatalkan dari sini.","Ini menulis ulang atau membuang riwayat git.","Ini berjalan dengan hak administrator di Mac Anda.","Ini mengunduh skrip lalu menjalankannya.","Ini menghapus data di database.","Ini bisa menghapus seluruh isi disk.","Ini menghentikan program secara paksa.","Ini membuka izin file untuk semua orang.","Ini mengosongkan atau mengakhiri sesi Claude.","Ini menghentikan apa yang sedang dikerjakan Claude.","Jika disetujui, Claude langsung melakukannya di Mac Anda.","Ya","Tidak","Lanjut","Jelaskan lagi","baru saja","{n} mnt","{n} jam","Perlu disetujui","Giliran Anda","Bekerja","Selesai","Kesalahan","Terkirim","Terkirim ke Mac","Menunggu Anda mengonfirmasi di Mac (60 dtk)","Mac belum menerima perintah dari perangkat ini. Di Mac: EspOS Desktop > Settings > Devices, nyalakan perangkat ini.","Mac sedang offline. Tidak ada yang terkirim.","Fitur ini dimatikan di EspOS Desktop pada Mac.","Mac dalam mode hanya lihat. Tidak ada yang terkirim.","Sesi ini sudah tidak menunggu.","Pertanyaan itu sudah dijawab atau kedaluwarsa.","Terlalu banyak berturut-turut. Tunggu sebentar.","Tidak ada koneksi. Tidak ada yang terkirim.","Tidak terkirim: {why}","Tidak terkirim ({code}).","Ini memerlukan EspOS AI.","Tidak ada yang bisa dikirim selama Mac offline","Hanya lihat: jawab di iTerm pada Mac.","Gunakan Setujui atau Tolak untuk permintaan ini","Tahan {key} sedetik untuk menghentikan Claude","{key} terkirim","Ketik atau ucapkan beberapa kata dulu.","Tidak ada suara yang terdengar.","Dikte gagal ({code}).","Tidak dikirim.","Tidak dikirim. Ketuk Kirim jika sudah benar.","Ukuran teks {px}","Claude ingin mengedit file","Claude ingin menjalankan perintah","Tolak","Setujui","Mode","Hanya lihat. Diaktifkan di EspOS Desktop pada Mac: Anda bisa melihat, tetapi tidak bisa mengetik atau menyetujui.","Coba lagi","Pesan ke Claude","Kirim","Hentikan Claude?","Setujui penghapusan file?","Setujui ini?","Setujui perintah berisiko?","Ini langsung masuk ke shell di Mac Anda","Kirim baris berisiko?","Batal","Hentikan","Ya, hapus","Tetap kirim","Kata-kata Anda","AI sedang menulis balasan yang jelas","AI belum bisa menulisnya sekarang. Coba lagi.","Balasan jelas","Ditulis AI dari kata-kata Anda dan bagian akhir sesi ini. Periksa sebelum mengirim.","Teks saya","Kirim ini","Kirim ini?","Ucapkan OK untuk mengirim, atau ketuk Kirim.","Mencari Mac Anda","Sebentar.","Hubungkan Mac Anda","1. Instal EspOS Desktop dari espos.dev/desktop.","2. Buka aplikasinya: muncul kode 6 digit.","3. Di espos.dev, buka \"Devices\", ketuk \"Add\", lalu ketik kodenya.","Setelah itu, sesi Claude Code Anda muncul di sini.","Periksa lagi","Sesi sedang dijeda","EspOS menjeda kontrol komputer untuk semua orang untuk sementara. Mac dan Claude Anda tetap bekerja seperti biasa. Coba lagi nanti.","Perangkat ini belum ditautkan","Tautkan ke akun EspOS Anda di espos.dev, lalu buka Sesi lagi.","Menyiapkan aplikasi ini","EspOS belum melihat Sesi di perangkat ini. Butuh hingga satu menit setelah dipasang.","Tidak ada koneksi internet.","Tidak dapat menghubungi EspOS saat ini.","Terlalu banyak permintaan. Segera mencoba lagi.","EspOS mengalami masalah. Mencoba lagi.","Belum ada komputer yang dipasangkan dengan akun ini. Instal EspOS Desktop dari espos.dev/desktop.","berhenti darurat","offline sejak {t}","hanya lihat","online","Berhenti darurat di {mac}","EspOS Desktop memutus koneksinya sendiri. Tidak ada yang ditampilkan atau dikirim sampai seseorang menyalakannya lagi di Mac.","Mac Anda offline. Ini kiriman terakhirnya; tidak ada yang bisa dikirim sampai Mac kembali online.","Belum ada sesi Claude saat ini","Jalankan claude di iTerm2 pada Mac Anda, dan sesi muncul di sini dalam hitungan detik.","Sesi","Sesi ini sudah berakhir","Membuka sesi","Sesi ini sudah tidak ada di Mac.","Mac offline sejak {t}. Ini layar terakhir yang dikirimnya.","Mac dalam mode hanya lihat","Sesi ini tidak ada di iTerm2","Berbagi terminal mati di Mac ini","Anda tetap melihat status, baris terakhir, dan permintaan izin. Untuk melihat terminal, aktifkan \"Bagikan teks terminal\" di EspOS Desktop, bagian \"Data yang dikirim\".","Baris terakhir","↓ {n} baris baru"]};
  var out = {}, v = V[app.lang];
  if (v) { out[app.lang] = {}; K.forEach(function (k, i) { if (v[i]) out[app.lang][k] = v[i]; }); }
  return out;
})();
// Sessions (dev.espos.sessions): the Claude Code sessions of your Mac, live on the S3. Watch a session's terminal,
// answer it, approve or deny what it asks, with quick replies, AI suggestions, dictation and "compose with AI".
// Nothing is sent without a tap; risky input asks once. Protocol: s3/docs/desktop-protocol.md §9.
// API level 2: net.request (auth "espos": the app's own token, added by the OS), ui.term, ui.input, ui.split,
// voice.dictate, settings, app.full / onBack. Mockups: ~/code/mockups/claude-term/.
'use strict';

const API = '{espos}/v1/device/app/dev.espos.sessions';
const SCR = app.screen, LAND = SCR.landscape;
const SMALL = SCR.w === 360 || SCR.h === 360;  // the 2.8" board: its UI at 2/3 on the panel
const DARK = !!app.dark;
// The terminal's text sizes (the package's fonts/m<px>.bin): 8 px on the 3.5" (a 6 px cell: ~52 columns upright,
// ~78 sideways); the 2.8" draws its UI at 2/3, so 12 there is 8 on its panel.
const SIZES = SMALL ? [12, 14, 15, 17] : [8, 10, 12, 14];

// ---------------------------------------------------------------- words: English, the other languages in i18n.js
// (tools/mki18n.py puts them in TR when the package is built; app.lang picks one)
var TR = TR || {};
const t = (s, a) => {
  let out = (TR[app.lang] && TR[app.lang][s]) || s;
  if (a) for (const k in a) out = out.split('{' + k + '}').join(String(a[k]));
  return out;
};

// ---------------------------------------------------------------- colours (the OS theme's)
const C = DARK
  ? {fg: '#eef1f5', dim: '#9aa3b2', faint: '#6b7380', card: '#171c26', card2: '#1f2531', chip: '#1f2430', chipFg: '#c9d0d8',
     wait: '#ffc56b', waitBg: '#3a2a10', run: '#5aa9ff', ok: '#34c759', bad: '#ff6b6b', ai: '#a48bff', cmd: '#0b0e14', link: '#5aa9ff'}
  : {fg: '#16181d', dim: '#6b7280', faint: '#8a8f99', card: '#ffffff', card2: '#eceef3', chip: '#e4e6ec', chipFg: '#3a3f4a',
     wait: '#8a4b00', waitBg: '#fff3e0', run: '#0a6cff', ok: '#1e9e52', bad: '#e0383b', ai: '#6a4cf0', cmd: '#eceef3', link: '#1360d6'};

// ---------------------------------------------------------------- risky input (docs/desktop-risky.json v1)
const RISKY = [
  ['shell', /^\s*!/], ['delete', /\brm\s+-[A-Za-z]*[rRf]/], ['delete', /\bfind\b.*\s-delete\b/],
  ['force', /\bgit\s+push\b.*(\s-f\b|\s--force)/], ['force', /\bgit\s+reset\s+--hard\b/], ['force', /\bgit\s+clean\s+-[A-Za-z]*f/],
  ['sudo', /\bsudo\b/], ['pipe_shell', /\b(curl|wget)\b[^|]*\|\s*(sudo\s+)?(sh|bash|zsh)\b/],
  ['database', /\b(drop\s+(table|database|schema)|truncate\s+table)\b/i], ['disk', /\b(mkfs(\.[a-z0-9]+)?|dd\s+if=)/],
  ['kill', /\bkill(all)?\s+-9\b/], ['perms', /\bchmod\s+(-R\s+)?777\b/], ['session', /^\s*\/(clear|exit|quit|logout)\b/],
];
const riskyWhy = (text) => { for (const [why, re] of RISKY) if (re.test(text || '')) return why; return ''; };
const WHY = () => ({
  shell: t('In Claude Code, a line that starts with ! runs as a shell command at once, without Claude.'),
  delete: t('This deletes files on your Mac. It cannot be undone from here.'),
  force: t('This rewrites or throws away git history.'),
  sudo: t('This runs with administrator rights on your Mac.'),
  pipe_shell: t('This downloads a script and runs it.'),
  database: t('This deletes data in a database.'),
  disk: t('This can wipe a disk.'),
  kill: t('This force-stops programs.'),
  perms: t('This opens file permissions to everyone.'),
  session: t('This clears or ends the Claude session.'),
  interrupt: t('This stops what Claude is doing right now.'),
  grant: t('Approving lets Claude do this on your Mac now.'),
});

// ---------------------------------------------------------------- state
const S = {
  view: 'list',          // 'list' | 'term'
  macs: [], sessions: [], rev: 0, loaded: false, listErr: '', listAt: 0, failures: 0,
  cur: null,             // {mac, s}: the session on screen
  scr: {seq: 0, err: '', why: '', starting: false},
  text: '', clearField: false,  // the prompt field; clearField: emptied at the next render (after Send)
  sheet: null,           // {kind, ...}: a card over the screen
  note: null,            // {text, tone, until}: a line under the header
  sending: false, dict: '',
  font: +store.get(SMALL ? 'size28' : 'size35', SIZES[0]),
  touchAt: 0, fastUntil: 0,  // the last tap or key (short polls while you are busy), short polls until (after a key)
  sig: '',
};
if (SIZES.indexOf(S.font) < 0) S.font = SIZES[0];

const setting = (k, d) => { try { const v = settings.get(k); return v === null || v === undefined ? d : v; } catch (e) { return d; } };
// The manifest's default list is English: its words show in the device's language; the person's own words as typed.
const QUICK_DEFAULT = ['Yes', 'No', 'Continue', 'Explain more'];
const quickReplies = () => {
  const local = [t('Yes'), t('No'), t('Continue'), t('Explain more')];  // QUICK_DEFAULT in the device's language
  const v = setting('quick', null);
  return (Array.isArray(v) && v.length ? v.slice(0, 8) : QUICK_DEFAULT).map((w) => {
    const i = QUICK_DEFAULT.indexOf(w);
    return i >= 0 ? local[i] : w;
  });
};

const macOf = (id) => S.macs.find((m) => m.id === id) || null;
const cur = () => S.cur && S.sessions.find((x) => x.mac === S.cur.mac && x.id === S.cur.s) || null;
const RANK = {perm: 0, input: 1, error: 2, run: 3, idle: 4};
const sorted = () => S.sessions.slice().sort((a, b) => (RANK[a.state] ?? 5) - (RANK[b.state] ?? 5) || (b.since_ms || 0) - (a.since_ms || 0));

// "2 min", "0:42" (working), "18 min", "3 h"
function ago(ms, clock) {
  if (!ms) return '';
  const s = Math.max(0, Math.round((Date.now() - ms) / 1000));
  if (clock && s < 3600) return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
  if (s < 60) return t('now');
  if (s < 3600) return t('{n} min', {n: Math.floor(s / 60)});
  return t('{n} h', {n: Math.floor(s / 3600)});
}
function stateOf(x) {
  return {
    perm: [t('Needs approval'), C.wait], input: [t('Your turn'), C.wait], run: [t('Working'), C.run],
    idle: [t('Done'), C.dim], error: [t('Error'), C.bad],
  }[x.state] || [x.state || '', C.dim];
}
function stateLine(x, short) {
  const [w, c] = stateOf(x);
  const extra = short ? '' : x.state === 'run' ? [x.tool, ago(x.since_ms, true)].filter(Boolean).join(' ') : ago(x.since_ms);
  return ui.text(w + (extra ? ' · ' + extra : ''), {size: 12, bold: true, color: c});
}

// ---------------------------------------------------------------- the network (one scheduler: the OS sends one request
// at a time, so a held poll never sits in front of a tap for long: holds only while the person just watches)
let pollT = 0, polling = false, nonceN = 0;
const nonce = () => crypto.random(8);  // 16 hex
function call(method, path, body) {
  return net.request({method, url: API + path, body, auth: 'espos', timeout: 20000})
    .then((r) => r, (e) => ({status: 0, error: e && e.code || 'offline', sent: !!(e && e.sent)}));
}
const json = (r) => { try { return r.body ? JSON.parse(r.body) : {}; } catch (e) { return {}; } };

function schedule(ms) {
  if (pollT) timer.cancel(pollT);
  pollT = timer.after(Math.max(50, ms), () => { pollT = 0; poll(); return false; });  // false: no redraw (timers)
}
function quiet() { return !!S.sheet || S.sending; }
function poll() {
  if (polling) return;
  const watching = S.view === 'term' && cur() && !S.scr.err;
  if (watching && Date.now() - S.listAt < 5000) pollScreen();
  else pollList(watching || quiet() ? 0 : 4);
}

function listError(r) {
  const e = json(r).error || '';
  if (r.status === 404 && e === 'no_desktop') return 'not_paired';
  if (r.status === 503) return 'disabled';
  if (r.status === 401 || r.status === 403) return e === 'app_not_installed' ? 'not_installed' : 'auth';
  if (r.status === 429) return 'rate';
  if (!r.status && r.error === 'denied') return 'auth';  // the OS sends no credential: the device is not linked
  if (!r.status) return net.online === false ? 'offline_s3' : 'network';
  return 'server';
}

function sigOf() {
  if (S.view !== 'term') return JSON.stringify([S.listErr, S.macs, S.sessions, denied]);
  const x = cur(), m = x && macOf(x.mac);
  return JSON.stringify([S.listErr, x && [x.name, x.state, x.req], m && [m.online, m.mode, m.stream, m.stream_why, lockOf(m)]]);
}

function pollList(wait) {
  polling = true;
  call('GET', '/sessions?rev=' + (S.rev || 0) + '&wait=' + wait).then((r) => {
    polling = false;
    S.listAt = Date.now();
    const before = S.sig;
    if (r.status === 200) {
      const d = json(r);
      S.macs = d.macs || [], S.sessions = d.sessions || [], S.rev = d.rev || 0;
      for (const m of S.macs) if (m.allowed === true) delete denied[m.id];  // turned on at the Mac
      S.loaded = true, S.listErr = '', S.failures = 0;
      schedule(S.view === 'term' ? 50 : 300);
    } else {
      S.listErr = listError(r), S.loaded = true, S.failures++;
      const retry = json(r).retry_s;
      schedule(retry ? retry * 1000 : S.listErr === 'auth' ? 3000 : [2000, 5000, 10000, 30000][Math.min(S.failures - 1, 3)]);
    }
    // Drawn again only when something shown changed: a render rebuilds the screen (74 ms on the board), while the
    // terminal draws its own lines. On the terminal only the session's state, its request and the Mac's mode count.
    S.sig = sigOf();
    if (S.sig !== before) app.refresh();
  });
}

// The OS sends one request at a time, so a held poll is what a key waits behind. For 4 s after a key: a 0.15 s hold
// (one look of the server's); 0.5 s while you are busy; 2 s after 30 s without a touch. A new frame asks again at once
// (the server's hold, looking every 150 ms, paces it). A server that rounds the hold down (an older or self-hosted
// one) answers early: then the holds are whole seconds and the quick ask after a key waits 150 ms.
function pollScreen() {
  const s = cur();
  if (!s) return schedule(1000);
  polling = true;
  const now = Date.now();
  const fast = now < S.fastUntil;
  const wait = S.sheet ? 0 : fast ? (S.whole ? 0 : 0.15) : now - S.touchAt < 30000 ? (S.whole ? 1 : 0.5) : 2;
  call('GET', '/screen?mac=' + s.mac + '&s=' + encodeURIComponent(s.id) + '&since=' + S.scr.seq + '&wait=' + wait).then((r) => {
    polling = false;
    ST.polls++;
    if (S.view !== 'term' || !cur() || cur().id !== s.id) return schedule(50);
    const before = S.scr.err + S.scr.why + S.scr.starting;
    if (r.status === 200) {
      let info = null;
      try { info = term.apply('t', r.body); } catch (e) { info = null; }
      S.scr.err = '';
      if (info && info.starting) {
        S.scr.starting = true;
        if (before !== S.scr.err + S.scr.why + S.scr.starting) app.refresh();
        return schedule(800);
      }
      S.scr.starting = false;
      if (info) S.scr.seq = info.seq;
      // a 0.5 s hold that keeps coming back early with nothing new: this server rounds holds down (3 in a row)
      if (info && !info.lines && wait === 0.5) S.early = Date.now() - now < 300 ? (S.early || 0) + 1 : 0, S.whole = S.early >= 3;
      if (info && info.lines) {
        ST.frames++;
        if (info.at) ST.age.push(Date.now() - info.at);
        if (ST.keyAt) ST.effect.push(Date.now() - ST.keyAt), ST.keyAt = 0;
      }
      schedule(info && info.lines || wait ? 0 : fast ? 150 : 600);
    } else if (r.status === 409) {
      const d = json(r);
      S.scr.err = d.error || 'not_streaming', S.scr.why = d.why || '';
      schedule(5000);
    } else {
      S.listErr = r.status ? listError(r) : S.listErr;
      schedule(r.status ? 3000 : 2000);
    }
    if (before !== S.scr.err + S.scr.why + S.scr.starting) app.refresh();
  });
}

// What the device adds to the delay, in the trace every 15 s while a terminal is open (log()): frames drawn and their
// age (the Mac's "at" -> drawn), key round trips, first frame after a key, polls.
const ST = {frames: 0, polls: 0, age: [], keys: [], effect: [], keyAt: 0};
const avg = (a) => a.length ? Math.round(a.reduce((x, y) => x + y, 0) / a.length) : '-';
timer.every(15000, () => {
  if (S.view !== 'term' || !(ST.frames || ST.keys.length)) return;
  log('latency: ' + ST.frames + ' frames' + (ST.age.length ? ', age ' + avg(ST.age) + ' ms (max ' + Math.max(...ST.age) + ')' : '') +
    ', keys ' + ST.keys.length + ' rtt ' + avg(ST.keys) + ' ms, key to new lines ' + avg(ST.effect) + ' ms, ' + ST.polls + ' polls, font ' + S.font);
  ST.frames = ST.polls = 0, ST.age = [], ST.keys = [], ST.effect = [];
  return false;
});

// ---------------------------------------------------------------- sending (always from a tap)
function note(text, tone, ms) {
  if (app.kb > 0) return toast(text);  // the keyboard is up: no room for a line, a toast sits over the keys
  S.note = {text, tone: tone || 'ok', until: Date.now() + (ms || 4000)};
  timer.after((ms || 4000) + 50, () => { app.refresh(); return false; });
}

const SENT = () => ({done: t('Sent'), sent: t('Sent to the Mac'), confirm_mac: t('Waiting for you to confirm it on the Mac (60 s)')});
const NOT_ALLOWED = () => t('The Mac does not take commands from this device yet. On the Mac: EspOS Desktop > Settings > Devices, turn this device on.');
const REFUSED = () => ({
  desktop_offline: t('The Mac is offline. Nothing was sent.'), command_off: t('This is turned off in EspOS Desktop on the Mac.'),
  view_only: t('The Mac is in view-only mode. Nothing was sent.'), desktop_view_only: t('The Mac is in view-only mode. Nothing was sent.'),
  not_waiting: t('The session is no longer waiting.'), req_gone: t('That question was already answered or has expired.'),
  rate: t('Too many in a row. Wait a moment.'), sender_not_allowed: NOT_ALLOWED(),
});

// What the Mac lets this device do. 'denied': nothing is sent (the Mac's list of devices that may send commands says no:
// macs[].allowed false, or a "sender_not_allowed" seen here); 'view': the Mac's view-only mode; '' when it may send.
const denied = {};  // mac id -> true after a "sender_not_allowed"; ends when the server says allowed, or "Try again"
function lockOf(mac) {
  if (!mac) return '';
  if (mac.allowed === false || denied[mac.id]) return 'denied';
  return mac.mode === 'view' ? 'view' : '';
}
// Any answer that is not a success, in words: never silent, never a bare status. A "sender_not_allowed" locks the keys
// and Approve/Deny for that Mac (no retries), drops the keys still waiting, and the reason takes the prompt's place
// (the list: under the Mac's name), where it stays.
function notSent(r, d, mac) {
  const code = d.err || d.error || '';
  log('refused ' + (r.status || 0) + ' ' + (code || '-'));  // on the serial line too
  if (code === 'sender_not_allowed' && mac) {
    denied[mac.id] = true, KQ.q = [], S.sig = '';
    if (app.kb > 0) app.closeKeyboard();
    return;
  }
  note(!r.status ? t('No connection. Nothing was sent.') : REFUSED()[code] || (code || d.state ? t('Not sent: {why}', {why: code || d.state})
    : t('Not sent ({code}).', {code: r.status})), 'bad', 8000);
}

// kind: text | key | approve | deny. ack: the person already said yes to a risky one.
function send(kind, extra, ack, n) {
  const s = extra.session || cur();
  if (!s || S.sending) return;
  if (lockOf(macOf(s.mac)) === 'denied') return note(NOT_ALLOWED(), 'bad', 8000), app.refresh();
  const body = Object.assign({mac: s.mac, s: s.id, nonce: n || nonce(), kind}, extra.body);
  if (ack) body.ack_risky = true;
  S.sending = true;
  app.refresh();
  call('POST', '/send', body).then((r) => {
    S.sending = false;
    const d = json(r);
    if (r.status === 428) {  // the server's own check: ask once, then the same request with ack_risky
      S.sheet = {kind: 'risky', why: d.why || 'shell', what: extra.what, go: () => send(kind, extra, true)};
    } else if (r.status === 200 || r.status === 409) {
      const st = d.state || (r.status === 200 ? 'sent' : 'refused');
      if (st === 'refused' || st === 'failed') notSent(r, Object.assign({state: st}, d), macOf(s.mac));
      else {
        note(d.text || SENT()[st] || t('Sent'), st === 'confirm_mac' ? 'wait' : 'ok', st === 'confirm_mac' ? 60000 : 3000);
        if (extra.after) extra.after();
      }
      schedule(300);  // the session's state moves on: hear it soon
    } else if (!r.status && r.sent && !n) {
      send(kind, extra, ack, body.nonce);  // it may have arrived: the same nonce again is never typed twice
      return;
    } else if (r.status === 402) {
      note(t('This needs EspOS AI.'), 'bad', 6000);
    } else {
      notSent(r, d, macOf(s.mac));
    }
    app.refresh();
  });
}

function sendText(text) {
  text = String(text || '').trim();
  if (!text) return;
  S.touchAt = Date.now();
  const go = (ack) => send('text', {body: {text: text.slice(0, 2000)}, what: text, after: () => {
    S.text = '', S.clearField = true;
    S.fastUntil = Date.now() + 4000, ST.keyAt = Date.now();
  }}, ack);
  const why = riskyWhy(text);
  if (why) S.sheet = {kind: 'risky', why, what: text, go: () => go(true)};
  else go(false);
  app.refresh();
}

// Keys: a tap never waits for the one before. Taps made while a send is out go together as one kind "keys" (the
// Mac types them in order); a server that does not know "keys" yet gets them one by one. No redraw unless it failed.
// Esc and Ctrl+C stop Claude: they go only after a 1 s hold (holdDown / holdUp), with ack_risky.
const KQ = {q: [], busy: false, single: false};
function pressKey(k, ack) {
  const s = cur(), mac = s && macOf(s.mac);
  if (!s || !mac || !mac.online) return toast(t('Nothing can be sent while the Mac is offline'));
  const lock = lockOf(mac);
  if (lock === 'denied') return toast(NOT_ALLOWED());
  if (lock === 'view') return toast(t('View only: answer it in iTerm on the Mac.'));
  S.touchAt = Date.now();
  if (s.state === 'perm' && s.req) {  // Claude's own 1/2/3 dialog: the request's path, never "don't ask again"
    if (k === '1') return answer(s, true);
    if (k === '3') return answer(s, false);
    if (k === '2' || k === 'enter' || k === 'tab') return toast(t('Use Approve or Deny for this request'));
  }
  KQ.q.push({k, ack: !!ack});
  flushKeys();
}
function flushKeys(again) {
  const s = cur();
  if (KQ.busy || !KQ.q.length || !s) return;
  const m = macOf(s.mac), many = !KQ.single && !!(m && m.cmds && m.cmds['claude.keys']);  // cmds: {name: policy}
  const batch = again || KQ.q.splice(0, many ? 20 : 1);
  const keys = batch.map((x) => x.k);
  const body = batch.body || Object.assign({mac: s.mac, s: s.id, nonce: nonce()},
    keys.length === 1 ? {kind: 'key', key: keys[0]} : {kind: 'keys', keys});
  if (batch.some((x) => x.ack)) body.ack_risky = true;
  batch.body = body;
  KQ.busy = true;
  const t0 = Date.now();
  call('POST', '/send', body).then((r) => {
    KQ.busy = false;
    const d = json(r);
    if (keys.length > 1 && (r.status === 400 || r.status === 422 || d.err === 'command_off')) {  // an older server or Mac app
      KQ.single = true, KQ.q = batch.concat(KQ.q);
      return flushKeys();
    }
    if (!r.status && r.sent && !batch.retried) {  // it may have arrived: the same nonce is never typed twice
      batch.retried = true;
      return flushKeys(batch);
    }
    ST.keys.push(Date.now() - t0), ST.keyAt = Date.now();
    if (r.status === 409 && d.error === 'use_answer') KQ.q = [], toast(t('Use Approve or Deny for this request'));
    else if (r.status === 428) toast(t('Hold {key} for a second to stop Claude', {key: KEYS_LABEL[keys.find((k) => k === 'esc' || k === 'ctrl_c')] || 'Esc'}));
    else if (r.status === 200 || r.status === 409) {
      const st = d.state || (r.status === 200 ? 'sent' : 'refused');
      if (st === 'refused' || st === 'failed') KQ.q = [], notSent(r, Object.assign({state: st}, d), m), app.refresh();
    } else KQ.q = [], notSent(r, d, m), app.refresh();
    S.fastUntil = Date.now() + 4000;
    if (!polling && S.view === 'term') schedule(0);  // its effect, as soon as the Mac draws it
    flushKeys();
  });
}
let holdT = 0;
function holdDown(k) {
  S.touchAt = Date.now();
  if (holdT) timer.cancel(holdT);
  holdT = timer.after(1000, () => { holdT = 0; pressKey(k, true); toast(t('Sent {key}', {key: KEYS_LABEL[k]})); return false; });
}
function holdUp(k) {
  if (!holdT) return;
  timer.cancel(holdT), holdT = 0;
  toast(t('Hold {key} for a second to stop Claude', {key: KEYS_LABEL[k]}));
}

function answer(s, approve) {
  const req = s.req;
  if (!req) return;
  const go = (ack) => send(approve ? 'approve' : 'deny', {session: s, body: {req: req.id}, what: req.cmd || req.file || ''}, ack);
  const why = approve && (req.risky || riskyWhy(req.cmd)) ? (riskyWhy(req.cmd) || 'grant') : '';
  if (why) S.sheet = {kind: 'approve', why, req, go: () => go(true)};
  else go(false);
  app.refresh();
}

// ---------------------------------------------------------------- AI: suggestions, compose, dictation
function compose() {
  const s = cur();
  const rough = S.text.trim();
  if (!s || !rough) return note(t('Type or say a few words first.'), 'wait');
  S.sheet = {kind: 'compose', rough, text: '', state: 'loading'};
  app.refresh();
  call('POST', '/compose', {mac: s.mac, s: s.id, rough, seq: S.scr.seq}).then((r) => {
    if (!S.sheet || S.sheet.kind !== 'compose') return;
    if (r.status === 200) S.sheet.text = json(r).text || '', S.sheet.state = 'ok';
    else S.sheet.state = r.status === 402 ? 'locked' : 'failed';
    app.refresh();
  });
}

const canTalk = typeof voice === 'object';
function talkDown() {
  if (!canTalk || S.dict) return;
  S.dict = 'rec';
  app.refresh();
  voice.dictate({lang: app.lang, max_ms: 30000}).then((r) => {
    S.dict = '';
    const said = (r && r.text || '').trim();
    if (said) {
      S.text = (S.text ? S.text.replace(/\s+$/, '') + ' ' : '') + said, S.clearField = true;
      if (setting('voicesend', true) !== false) askVoiceSend(S.text, 0);
    } else note(t('Nothing was heard.'), 'wait');
    app.refresh();
  }, (e) => {
    S.dict = '';
    if (e && e.code !== 'cancelled') note(e && e.code === 'locked' ? t('This needs EspOS AI.') : t('Dictation did not work ({code}).', {code: e && e.code || '?'}), 'bad');
    app.refresh();
  });
}
// Say OK to send (desktop-protocol.md §11 voice confirm): the dictated text is read back on screen and the OS listens
// once for a yes or a no. Only "confirm" sends; "unclear" asks once more; anything else keeps the text in the field.
// A risky line is never sent by voice: it waits for its taps.
function askVoiceSend(text, tries) {
  if (!canTalk || riskyWhy(text)) return;
  S.sheet = {kind: 'voicesend', text};
  voice.dictate({lang: app.lang, max_ms: 5000, expect: 'yesno', about: text, until_quiet: true}).then((r) => {
    if (!S.sheet || S.sheet.kind !== 'voicesend') return;
    const yn = r && r.yesno;
    if (yn === 'unclear' && tries < 1) return askVoiceSend(text, tries + 1);
    S.sheet = null;
    if (yn === 'confirm') sendText(text);
    else note(yn === 'cancel' ? t('Not sent.') : t('Not sent. Tap Send when it is right.'), 'wait');
    app.refresh();
  }, () => {
    if (S.sheet && S.sheet.kind === 'voicesend') S.sheet = null;
    app.refresh();
  });
}

function talkUp() { if (canTalk && S.dict === 'rec') S.dict = 'wait', voice.stop(), app.refresh(); }

// ---------------------------------------------------------------- navigation
function openSession(s) {
  S.view = 'term', S.cur = {mac: s.mac, s: s.id};
  S.scr = {seq: 0, err: '', why: '', starting: false};
  S.text = '', S.sheet = null, S.sig = '', S.touchAt = Date.now();
  term.clear('t');
  schedule(50);
  app.refresh();
}
function back() {
  if (S.sheet) S.sheet = null;
  else if (S.view === 'term') { S.view = 'list', S.cur = null, S.sig = ''; term.clear('t'); schedule(50); }
  else return false;
  app.refresh();
  return true;
}
app.onBack = back;

function zoom(d) {
  const i = Math.max(0, Math.min(SIZES.length - 1, SIZES.indexOf(S.font) + d));
  S.font = SIZES[i < 0 ? 0 : i];
  store.set(SMALL ? 'size28' : 'size35', S.font);
  toast(t('Text size {px}', {px: S.font}));
}

// ---------------------------------------------------------------- screens: pieces
const small = (label, fn, o) => ui.button(label, fn, Object.assign({h: 30, bg: C.chip, color: C.chipFg}, o));
const chip = (label, fn, o) => ui.button(label, fn, Object.assign({h: 28, bg: C.chip, color: C.chipFg}, o));

function noteLine() {
  if (!S.note || Date.now() > S.note.until) return null;
  const col = {ok: C.ok, bad: C.bad, wait: C.wait}[S.note.tone] || C.dim;
  return ui.card([ui.text(S.note.text, {size: 13, color: col})], {bg: S.note.tone === 'wait' ? C.waitBg : C.card2, gap: 2});
}

// The bar over the terminal: back, the session, its state, text size. Nothing else: the screen is the terminal.
function topBar(s) {
  const kids = [small('', back, {icon: 'back', w: 32, h: 28}), ui.text(s ? s.name : '', {bold: true, size: 15})];
  if (s) kids.push(stateLine(s, true));
  kids.push(small('A−', () => zoom(-1), {h: 28, w: 36}), small('A+', () => zoom(1), {h: 28, w: 36}));
  return ui.row(kids, {gap: 6, pad: 2});
}

// Claude asks: the request in one strip over the prompt (Claude's own dialog stays readable in the terminal above).
function permStrip(s, mac) {
  const req = s.req || {};
  const what = req.cmd || req.file || req.desc || '';
  const title = ui.text((req.file && !req.cmd ? t('Claude wants to edit a file') : t('Claude wants to run a command')) + ': ' + what, {size: 13, bold: true});
  if (lockOf(mac)) return ui.column([title], {gap: 3, pad: 6});  // the prompt's line says why nothing can be answered
  const deny = ui.button(t('Deny'), () => answer(s, false), {bg: C.chip, color: C.bad, h: 34, fill: !LAND, w: LAND ? 88 : 0});
  const ok = ui.button(t('Approve'), () => answer(s, true), {bg: C.ok, h: 34, fill: !LAND, w: LAND ? 104 : 0});
  if (LAND) return ui.row([deny, ok, ui.column([title], {fill: true, gap: 0})], {gap: 8, pad: 5});  // fill goes last (it takes what is left)
  return ui.column([title, ui.row([deny, ok], {gap: 8})], {gap: 5, pad: 6});
}

// The terminal keys, on top of the OS keyboard while the prompt has it (ui.input "keys"). Arrows are the package's
// icons; shift+tab cycles Claude Code's mode, so it says so. Esc and Ctrl+C: hold 1 s.
const KEYS_LABEL = {esc: 'Esc', up: '', down: '', left: '', right: '', enter: 'Enter', tab: 'Tab', shift_tab: 'Mode', 1: '1', 2: '2', 3: '3', ctrl_c: 'Ctrl+C'};
function keyBtn(k, w) {
  const stop = k === 'esc' || k === 'ctrl_c';
  const o = {h: 34, w, bg: '#2b323d', color: stop ? '#ff8a80' : '#eef1f5', icon: KEYS_LABEL[k] ? '' : 'icons/' + k + '.a8'};
  const label = k === 'shift_tab' ? t('Mode') : KEYS_LABEL[k];
  if (stop) return ui.button(label, null, Object.assign(o, {onDown: () => holdDown(k), onUp: () => holdUp(k)}));
  return ui.button(label, () => pressKey(k), Object.assign(o, {quiet: true}));  // a key is not a redraw
}
function keysRow() {
  if (LAND) return ui.row([keyBtn('esc', 40), keyBtn('tab', 40), keyBtn('shift_tab', 50), keyBtn('left', 34), keyBtn('up', 34),
    keyBtn('down', 34), keyBtn('right', 34), keyBtn('enter', 50), keyBtn('ctrl_c', 56), keyBtn('1', 26), keyBtn('2', 26), keyBtn('3', 26)], {gap: 2});
  return ui.column([
    ui.row([keyBtn('esc', 44), keyBtn('tab', 44), keyBtn('shift_tab', 54), keyBtn('ctrl_c', 62), keyBtn('1', 30), keyBtn('2', 30), keyBtn('3', 30)], {gap: 3}),
    ui.row([keyBtn('left', 52), keyBtn('up', 52), keyBtn('down', 52), keyBtn('right', 52), keyBtn('enter', 94)], {gap: 3}),
  ], {gap: 4});
}

// The prompt, like Claude Code's own: type, Enter on the keyboard sends; the terminal keys sit on the keyboard.
function promptRow(s, mac) {
  const lock = lockOf(mac);
  if (lock === 'view') return ui.column([ui.text(t('View only. Turned on in EspOS Desktop on the Mac: you can watch, not type or approve.'), {size: 12, dim: true, center: true})], {pad: 6});
  if (lock === 'denied') {  // from the start when the server says so; after a refusal, "Try again" once it is turned on
    const why = ui.text(NOT_ALLOWED(), {size: 12, color: C.wait});
    if (mac.allowed === false) return ui.column([why], {pad: 6});
    return ui.row([ui.button(t('Try again'), () => { delete denied[mac.id]; S.sig = ''; app.refresh(); }, {h: 32, bg: C.chip, color: C.chipFg}),
      ui.column([why], {fill: true, gap: 0})], {gap: 8, pad: 5});  // fill goes last (it takes what is left)
  }
  const off = !mac || !mac.online;
  const clear = S.clearField;
  S.clearField = false;
  return ui.row([
    // Two lines when the app puts a long text in (dictation, AI): the field shows it whole.
    ui.input(S.text, {key: 'reply', hint: off ? t('Nothing can be sent while the Mac is offline') : t('Message Claude'),
      lines: S.text.length > (LAND ? 44 : 24) ? 2 : 1, max: 2000,
      mode: 'chat', ok: t('Send'), force: clear, quiet: true, bg: C.card, border: C.faint, keys: off ? null : keysRow(), keysH: LAND ? 38 : 76,
      onFocus: () => { S.touchAt = Date.now(); }, onChange: (v) => { S.text = v; S.touchAt = Date.now(); },
      onSubmit: (v) => { if (!off) sendText(v); }, fill: true}),
    canTalk ? ui.button('', null, {h: 36, w: 40, bg: S.dict === 'rec' ? C.bad : C.chip, color: S.dict === 'rec' ? '#ffffff' : C.chipFg, icon: 'icons/mic.a8', onDown: off ? null : talkDown, onUp: talkUp}) : null,
    ui.button('AI', () => { if (!off) compose(); }, {h: 36, w: 40, bg: C.ai}),
  ].filter(Boolean), {gap: 5, pad: 4});
}

// ---------------------------------------------------------------- sheets
function sheet() {
  const sh = S.sheet;
  if (!sh) return null;
  const close = () => { S.sheet = null; };
  if (sh.kind === 'risky' || sh.kind === 'approve') {
    const title = sh.stop ? t('Stop Claude?') : sh.kind === 'approve'
      ? (sh.why === 'delete' ? t('Approve deleting files?') : sh.why === 'grant' ? t('Approve this?') : t('Approve a risky command?'))
      : sh.why === 'shell' ? t('This goes straight to the shell on your Mac') : t('Send a risky line?');
    const what = sh.kind === 'approve' ? (sh.req.cmd || sh.req.file || '') : sh.what;
    return ui.sheet([
      ui.title(title, {size: 17, color: sh.stop ? C.fg : C.bad}),
      what ? ui.card([ui.text(what, {size: 13})], {bg: C.cmd}) : null,
      ui.text(WHY()[sh.why] || '', {size: 13}),
      ui.row([
        ui.button(t('Cancel'), close, {bg: C.chip, color: C.chipFg, fill: true}),
        ui.button(sh.stop ? t('Stop Claude') : sh.kind === 'approve' ? (sh.why === 'delete' ? t('Approve delete') : t('Approve')) : t('Send anyway'),
          () => { S.sheet = null; sh.go(); }, {bg: C.bad, fill: true}),
      ], {gap: 8}),
    ].filter(Boolean), {onClose: close});
  }
  if (sh.kind === 'compose') {
    const kids = [ui.text(t('Your words'), {size: 12, bold: true, dim: true}), ui.card([ui.text(sh.rough, {size: 13, dim: true})], {bg: C.card2})];
    if (sh.state === 'loading') kids.push(ui.text(t('AI is writing a clear reply'), {size: 13, color: C.ai}));
    else if (sh.state === 'locked') kids.push(ui.text(t('This needs EspOS AI.'), {size: 13, color: C.wait}));
    else if (sh.state === 'failed') kids.push(ui.text(t('AI could not write it now. Try again.'), {size: 13, color: C.bad}));
    else {
      kids.push(ui.text('AI · ' + t('Clear reply'), {size: 12, bold: true, color: C.ai}));
      kids.push(ui.input(sh.text, {key: 'composed', lines: 3, max: 2000, mode: 'chat', onChange: (v) => { sh.text = v; }}));
      kids.push(ui.text(t('Written by AI from your words and the end of this session. Check it before you send.'), {size: 12, dim: true}));
    }
    kids.push(ui.row([
      ui.button(t('Use mine'), () => { S.sheet = null; sendText(sh.rough); }, {bg: C.chip, color: C.chipFg, fill: true, h: 40}),
      ui.button(t('Try again'), () => { S.text = sh.rough; compose(); }, {bg: C.chip, color: C.chipFg, fill: true, h: 40}),
      sh.state === 'ok' ? ui.button(t('Send this'), () => { S.sheet = null; sendText(sh.text); }, {bg: C.run, fill: true, h: 40}) : null,
    ].filter(Boolean), {gap: 6}));
    return ui.sheet(kids, {onClose: close});
  }
  if (sh.kind === 'voicesend') {
    const stopListening = () => { try { voice.stop(true); } catch (e) {} };
    return ui.sheet([
      ui.title(t('Send this?'), {size: 17}),
      ui.card([ui.text(sh.text, {size: 14})], {bg: C.card2}),
      ui.text(t('Say OK to send it, or tap Send.'), {size: 13, dim: true}),
      ui.row([
        ui.button(t('Cancel'), () => { stopListening(); S.sheet = null; }, {bg: C.chip, color: C.chipFg, fill: true}),
        ui.button(t('Send'), () => { stopListening(); S.sheet = null; sendText(sh.text); }, {bg: C.run, fill: true}),
      ], {gap: 8}),
    ], {onClose: () => { stopListening(); S.sheet = null; }});
  }
  return null;
}

// ---------------------------------------------------------------- screens
function emptyState(title, body, button) {
  return ui.column([
    ui.spacer(LAND ? 4 : 40),
    ui.title(title, {center: true, size: 18}),
    ...[].concat(body).map((b) => ui.text(b, {center: true, size: 14, dim: true})),
    button || null,
  ].filter(Boolean), {gap: LAND ? 7 : 10, pad: 18, center: true, scroll: true, fill: true});
}

function listBody() {
  if (!S.loaded) return emptyState(t('Looking for your Mac'), t('One moment.'));
  const e = S.listErr;
  if (e === 'not_paired') return emptyState(t('Connect your Mac'), [
    t('1. Install EspOS Desktop from espos.dev/desktop.'), t('2. Open it: it shows a 6-digit code.'),
    t('3. On espos.dev, open Devices, tap Add and type the code.'), t('Your Claude Code sessions then show up here.')],
    ui.button(t('Check again'), () => schedule(50), {w: 200}));
  if (e === 'disabled') return emptyState(t('Sessions are paused'), t('EspOS has paused computer control for everyone for a while. Your Mac and Claude keep working as usual. Try again later.'));
  if (e === 'auth') return emptyState(t('This device is not linked'), t('Link it to your EspOS account on espos.dev, then open Sessions again.'));
  if (e === 'not_installed') return emptyState(t('Getting this app ready'), t('EspOS has not seen Sessions on this device yet. It takes up to a minute after installing.'));
  const parts = [];
  if (e && !S.macs.length) return emptyState({offline_s3: t('No internet connection.'), network: t('Cannot reach EspOS right now.'),
    rate: t('Too many requests. Trying again soon.'), server: t('EspOS had a problem. Trying again.')}[e] || e, t('One moment.'));
  if (e) parts.push(ui.card([ui.text({offline_s3: t('No internet connection.'), network: t('Cannot reach EspOS right now.'), rate: t('Too many requests. Trying again soon.'), server: t('EspOS had a problem. Trying again.')}[e] || e, {size: 13, color: C.wait})], {bg: C.waitBg}));
  if (!S.macs.length) return emptyState(t('Connect your Mac'), t('No computer is paired with this account yet. Install EspOS Desktop from espos.dev/desktop.'));
  for (const m of S.macs) {
    const mine = sorted().filter((x) => x.mac === m.id);
    const lock = lockOf(m);
    const status = m.mode === 'off' ? t('emergency stop') : !m.online ? t('offline since {t}', {t: ago(m.seen_ms)}) : lock ? t('view only') : t('online');
    parts.push(ui.text(m.name + ' · ' + status, {size: 12, bold: true, color: !m.online || m.mode === 'off' ? C.faint : lock ? C.wait : C.ok}));
    if (m.mode === 'off') {
      parts.push(ui.card([ui.text(t('Emergency stop on {mac}', {mac: m.name}), {bold: true, color: C.bad}),
        ui.text(t('EspOS Desktop disconnected itself. Nothing is shown or sent until someone turns it back on at the Mac.'), {size: 13})]));
      continue;
    }
    if (!m.online) parts.push(ui.text(t('Your Mac is offline. This is what it sent last; nothing can be sent until it is back.'), {size: 12, dim: true}));
    else if (lock === 'denied') parts.push(ui.text(NOT_ALLOWED(), {size: 12, color: C.wait}));
    if (!mine.length) {
      parts.push(ui.card([ui.text(t('No Claude sessions right now'), {bold: true}),
        ui.text(t('Start claude in iTerm2 on your Mac and it shows up here within seconds.'), {size: 13, dim: true})], {half: true}));
      continue;
    }
    const cards = mine.map((x) => {
      const kids = [ui.row([ui.text(x.name, {bold: true, size: 15}), stateLine(x)], {gap: 8, top: true}), x.cwd ? ui.text(x.cwd, {size: 11, dim: true}) : null];
      // Claude's own report on itself (claude.status, desktop-protocol.md §10) when there is one, else its last line
      if (x.report) kids.push(ui.text(x.report, {size: 13}));
      else if (x.last) kids.push(ui.text(x.last, {size: 13}));
      if (x.state === 'perm' && x.req && m.online) {
        kids.push(ui.card([ui.text(x.req.cmd || x.req.file || x.req.desc || '', {size: 13})], {bg: C.cmd, gap: 2}));
        if (!lock) kids.push(ui.row([
          ui.button(t('Deny'), () => answer(x, false), {bg: C.chip, color: C.bad, h: 32, fill: true}),
          ui.button(t('Approve'), () => answer(x, true), {bg: C.ok, h: 32, fill: true}),
        ], {gap: 8}));
      }
      return ui.card(kids.filter(Boolean), {half: true, gap: 4, onTap: () => openSession(x)});
    });
    parts.push(ui.row(cards, {gap: 7, top: true}));
  }
  return ui.column(parts, {gap: 7});
}

function listScreen() {
  return ui.column([
    ui.row([ui.title(t('Sessions'), {size: 22})], {pad: 8}),
    noteLine(),
    ui.column([listBody()], {scroll: true, fill: true, pad: 8, gap: 8}),
    sheet(),
  ].filter(Boolean), {fill: true, gap: 0});
}

function termScreen() {
  const s = cur();
  if (!s) {  // it ended, or the list has not come yet
    return ui.column([topBar(null), emptyState(S.loaded ? t('This session has ended') : t('Opening the session'), S.loaded ? t('It is no longer on the Mac.') : t('One moment.'))], {fill: true, gap: 0});
  }
  const mac = macOf(s.mac);
  // Keyboard up: the OS made the screen that much shorter (app.kb): the terminal, a request's strip and the prompt
  // stay; back (or a tap on the terminal) puts the keys away.
  const kbUp = app.kb > 0, tight = kbUp && LAND, perm = s.state === 'perm' && s.req;
  const kids = kbUp ? [] : [topBar(s), noteLine()];
  if (mac && !mac.online && !kbUp) kids.push(ui.text(t('Mac offline since {t}. This is the last screen it sent.', {t: ago(mac.seen_ms)}), {size: 12, dim: true}));
  if (mac && mac.stream === false || S.scr.err) {
    const why = (mac && mac.stream_why) || S.scr.why;
    kids.push(ui.column([
      ui.card([ui.text(why === 'view' ? t('The Mac is in view-only mode') : why === 'not_iterm' ? t('This session is not in iTerm2') : why === 'ended' ? t('This session has ended') : t('Terminal sharing is off on this Mac'), {bold: true}),
        ui.text(t('You still see the state, the last line and permission requests. To see the terminal, turn on "Share terminal text" in EspOS Desktop, under Data sent.'), {size: 13, dim: true})]),
      s.last ? ui.card([ui.text(t('Last line'), {size: 12, bold: true, dim: true}), ui.text(s.last, {size: 13})]) : null,
    ].filter(Boolean), {scroll: true, fill: true, pad: 8, gap: 8}));
  } else if (!(tight && perm)) {  // sideways with the keyboard and a request: the strip and the prompt fill the room
    kids.push(ui.term({key: 't', font: S.font, wrap: 'fit', fill: true, pill: t('↓ {n} new lines'),
      onTap: () => { S.touchAt = Date.now(); if (kbUp) app.closeKeyboard(); }}));  // a tap on the terminal puts the keys away
  }
  if (perm) kids.push(permStrip(s, mac));
  kids.push(promptRow(s, mac), kbUp ? null : ui.spacer(6), sheet());  // the spacer: room for the OS home bar
  return ui.column(kids.filter(Boolean), {fill: true, gap: 0});
}

app.full = true;
app.selfDraw = true;  // answers do not redraw: pollList / pollScreen / send call app.refresh() when the screen changed
app.title = t('Sessions');
app.render = () => (S.view === 'term' ? termScreen() : listScreen());

// Opened from a notification: "<mac>:<session>[:<req>]" opens that session.
if (app.arg) {
  const [m, s] = String(app.arg).split(':');
  if (m && s) S.view = 'term', S.cur = {mac: +m, s}, S.touchAt = Date.now();
}
timer.every(30000, () => app.refresh());  // "2 min" and the countdowns move on
poll();
