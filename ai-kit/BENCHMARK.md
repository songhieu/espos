# Đo bộ AI tại chỗ trên M1 Pro (Gemma 4 E2B + Whisper + VieNeu)

Ngày 2026-11-21. Kết quả thô ở `bench/results/*.json`, cách chạy lại ở cuối trang.

## Tóm tắt

- **Trợ lý chó EspOS chạy được hoàn toàn trên máy.** 28 câu tiếng Việt (giờ, âm lịch, thời tiết, đọc báo, hỏi tin,
  giá vàng, đặt/sửa/tắt/xóa báo thức, ghi chú, nhắc việc, âm lượng, tên, trí nhớ, trò chuyện, từ chối giải toán,
  trò đoán số): **JSON đúng ngay lần đầu 100%** (grammar ép JSON), **làm đúng việc 24–27/28 câu** tùy lần chạy.
- **Prompt gọn (`ESPOS_AI_PROMPT=short`) nhanh gấp ~2 và không kém đúng:** một lượt gõ chữ trung vị **3,6–4,0 s**
  (p90 5,7–6,4 s) so với **8,5 s** (p90 19,7 s) của prompt đầy đủ; đúng 25–27/28 so với 25/28. Prompt đầy đủ với
  timeout 25 s mặc định thì **3/28 lượt hết giờ** ("Hết sức rồi…").
- **Một lượt nói** (gửi ghi âm → có tiếng): **trung vị 5,4–5,9 s**, nhanh nhất **2,5 s**, p90 **7,6–7,9 s**.
  Gồm Whisper ~0,6 s + Gemma 1,6–6,9 s + giọng đọc có tiếng sau ~0,2 s (+0,3 s máy chủ EspOS).
  Hosted Gemini: ~5 s. Câu đơn giản (giờ, thời tiết, giá vàng) nhanh hơn hosted; câu ngay sau khi đặt báo thức,
  ghi chú thì chậm hơn (5–8 s); lượt đầu sau khi bật ~15 s.
- **Mô hình:** Gemma 4 E2B QAT Q4 (2,6 GB, Apache-2.0). Đọc prompt 370–720 token/s, viết 68–76 token/s; 4 câu cùng
  lúc: tổng ~150 token/s. RAM: ~2,6 GB với 2 slot. Prefix cache: lượt có cùng phần đầu prompt chỉ mất **0,06 s** tới
  token đầu thay vì 14–23 s.
- **Nghe giọng:** Whisper large-v3-turbo + VAD: câu 2–4 s → **~0,5 s**, lỗi ký tự 4,7% (giọng tổng hợp; giọng thật
  qua micro sẽ kém hơn). Cửa sổ encoder vừa với câu nói: nhanh gấp 2, không sai hơn.
- **Giọng đọc:** VieNeu Turbo có tiếng sau **~0,2 s**, nhanh gấp ~3,7 lần thời gian thật (1 worker, CPU).
- **Một máy M1 Pro 16 GB** phục vụ thoải mái **2–3 người nói cùng lúc** (4 cùng lúc: mỗi câu ~5–6 s);
  ước **~600 lượt/giờ** khi vẫn giữ độ trễ dễ chịu, cỡ **50–100 hộ** dùng hằng ngày. Xem mục 6.
- **E4B chưa đo**: máy này không đủ chỗ trên ổ đĩa (cần 4,2 GB, ổ có lúc chỉ còn 1 GB).

## Máy và điều kiện đo

- Apple M1 Pro, CPU 8 nhân (6 hiệu năng + 2 tiết kiệm), GPU 14 nhân, RAM 16 GB, macOS 27.2.1.
- llama.cpp `v1.0.0`, whisper.cpp `v1.9.5`, Metal. Model: `gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf` (unsloth, 2,62 GB,
  4,63 tỷ tham số, ~2 tỷ "hiệu dụng"), `ggml-large-v3-turbo-q5_0.bin` (574 MB), `ggml-silero-v6.2.0.bin`.
- **Máy không rảnh**: lúc đo, máy còn đang build firmware (PlatformIO) và chạy trình duyệt. Load trung bình lúc đo
  6–34 (ghi trong từng file kết quả); một lần chạy giọng nói rơi vào lúc load 50–75 (`assist-short-voice-loaded.json`:
  vài lượt mất 30–235 s) nên không tính. Số dưới đây vì thế là **cận dưới** của tốc độ thật trên máy rảnh.
- Gateway với 3 model cùng chạy: `gemma-4-e2b` (1 worker, 2 slot, ctx 16384), `whisper` (1 slot), `vieneu-turbo`
  (1 worker, 4 luồng CPU). Máy chủ EspOS chạy tại chỗ, dữ liệu mới mỗi lần, máy S3 ở TP Hồ Chí Minh, tin VnExpress
  thật lấy về hôm đó (`bench/make_news_fixture.py`; bản trong repo là tin mẫu tự viết), thời tiết và giá thật (open-meteo, nguồn giá của EspOS).

## 1. Mô hình (bin/llm-server, `bench/llm_bench.py`)

### Đọc prompt và viết

| Thử | Token prompt | Tới token đầu | Đọc prompt |
|---|---|---|---|
| prompt nguội ~3 000 ký tự | 1 092 | 1,51 s | 721 token/s |
| prompt nguội ~12 000 ký tự | 4 083 | 7,96 s | 512 token/s |
| prompt nguội ~24 000 ký tự | 8 684 | 23,6 s | 368 token/s |

Viết: **76 token/s** (chữ tự do), **68 token/s** (JSON có grammar).
Tốc độ đọc giảm khi prompt dài vì 7 lớp attention toàn cục của Gemma 4 có head 512. Đổi `--ubatch` (512→2048),
`--batch`, tắt `swa_full` đều không nhanh hơn (đã thử, 324–348 token/s ở 8,7k token): bị giới hạn bởi GPU
(~1,5 TFLOPS thực). Cách nhanh hơn duy nhất là đọc ít hơn: prompt ngắn và cache.

### Prompt thật của trợ lý chó, có và không có prefix cache

Prompt lấy đúng từ máy chủ EspOS (`bench/fixtures/prompt_*.json`), câu hỏi mỗi lần một khác.

| Prompt | Token | Nguội: tới token đầu / xong | Có cache: tới token đầu / xong | Tắt cache |
|---|---|---|---|---|
| đầy đủ (hosted) | 8 175 | 22,8 s / 26,7 s | **0,06 s** / 5,3 s (trả lời 230–390 token vì lượt đó phải viết tóm tắt) | 28–32 s |
| gọn (`short`) | 5 771 | 14,2 s / 16,4 s | **0,06 s** / **2,4 s** (134–170 token) | 16–17 s |

### Nhiều câu cùng lúc (4 slot)

| Cùng lúc | Viết: mỗi câu | Viết: tổng | Trợ lý (prompt gọn, phần chung đã cache): mỗi câu xong sau |
|---|---|---|---|
| 1 | 67 token/s | 65 token/s | 2,8 s |
| 2 | 42 token/s | 83 token/s | 4,8–5,1 s |
| 4 | 39 token/s | **150 token/s** | 5,0–6,1 s (cả 4 xong trong 6,1 s) |

Các slot chép phần đầu prompt của nhau (không đọc lại). Đã sửa một lỗi tìm ra khi đo: slot chọn theo "prefix chung
dài nhất" kể cả khi chỉ 3 token (`<bos><|turn>system`) làm mất cache dài của slot đó; giờ chỉ dùng prefix ≥ 64 token,
không thì lấy slot trống/nhỏ nhất.

### Bộ nhớ

| | Model (trang đang dùng, mmap) | Cache + bộ đệm tính | Tổng |
|---|---|---|---|
| 1 slot × 16k token | ~1,8 GB | 0,44–0,55 GB | ~2,3 GB |
| 2 slot (trong gateway) | ~1,8 GB | 0,80 GB | ~2,6 GB |
| 4 slot | ~1,8 GB | 1,3–1,4 GB | ~3,2 GB |

KV cache của E2B chỉ ~15 KB/token (1 KV head, 20/35 lớp dùng chung KV), nên giữ cache dài cho nhiều slot rất rẻ.
Cả bộ (Gemma 2 slot + Whisper + VieNeu Turbo) ~4,8 GB RAM. Nạp model: ~10 s lần đầu, ~2 s khi file đã nằm trong RAM.

## 2. Trợ lý chó end-to-end qua máy chủ EspOS (`bench/run_e2e.sh`, `bench/assist_bench.py`)

28 câu trong `bench/fixtures/turns.json`, một cuộc trò chuyện liền. Sau mỗi câu, bench đọc bảng dữ liệu của máy chủ
(báo thức, ghi chú, nhắc việc, âm lượng, tên, trí nhớ) để xem việc có thật sự được làm. "Đúng việc" = kết quả đúng
(báo thức đúng giờ, đúng ngày, ghi chú có chữ, câu trả lời có tên thành phố, đọc báo ≥ 50 chữ...); "đúng chủ đề" =
nhãn `topic` đúng.

### Gõ chữ (web chat, `POST /v1/devices/:id/assist`)

| Lần chạy | Prompt | JSON đúng lần đầu | Đúng việc | Đúng chủ đề | Trung vị | p90 | Lâu nhất | Token prompt / đã cache (trung vị) |
|---|---|---|---|---|---|---|---|---|
| `full-text-1` | đầy đủ, timeout 25 s | 25/28 (**3 lượt hết giờ**) | – | – | 6,1 s | 20,3 s | 25 s | 7 696 / 7 133 |
| `full-text` | đầy đủ, timeout 60 s | 28/28 | 25/28 | 26/28 | **8,5 s** | 19,7 s | 23,4 s | 7 820 / 5 982 |
| `short-text` | gọn | 28/28 | **27/28** | 28/28 | **3,6 s** | 5,8 s | 9,3 s | 5 725 / 5 143 |
| `short` | gọn | 28/28 | 25/28 | 26/28 | 4,0 s | 6,4 s | 11,6 s | 6 148 / 5 231 |
| `final` (chữ) | gọn, bản cuối | 28/28 | 25/28 | 26/28 | 4,0 s | 5,7 s | 8,3 s | 6 187 / 5 238 |

Prompt đầy đủ chậm vì dòng giờ ("BÂY GIỜ: 15:04") nằm ngay đầu phần dữ liệu và đổi mỗi phút: hễ sang phút mới là
phải đọc lại ~4 000 token (~10–20 s). Prompt gọn đưa những thứ đổi theo lượt xuống cuối.

### Nói (thiết bị, `POST /v1/device/assist?stream=1`, ghi âm WAV 16 kHz)

Câu hỏi được VieNeu đọc thành WAV (giọng "Mai Anh") rồi gửi như máy S3 gửi; câu trả lời về dạng giọng nói.

| Lần chạy | JSON đúng lần đầu | Đúng việc | Có tiếng sau: trung vị | p90 | nhanh nhất | Load |
|---|---|---|---|---|---|---|
| `short-voice-2` | 28/28 | 25/28 | **5,4 s** | 7,6 s | 2,6 s | 14–24 |
| `final` (nói) | 28/28 | 24/28 | 5,9 s | 7,9 s | 2,5 s | 10–34 |

Chia ra (`short-voice-2`, trung vị): **Whisper 0,61 s**, **Gemma 4,2 s** (câu hỏi thường 1,6–2,1 s; câu ngay sau
một hành động hoặc khi lịch sử trượt 4,5–6,9 s), **còn lại 0,55 s** (máy chủ EspOS ~0,3 s + giọng đọc có tiếng
đầu ~0,2 s). Câu "đọc báo đi": có tiếng sau 3,5–4,9 s, đọc ~25 s.

### Câu sai (lần `final`) và vì sao

| Câu | Sai | Vì |
|---|---|---|
| "Đặt báo thức 6 giờ 30 sáng mai" | đặt lặp lại (thứ Bảy, Chủ nhật) thay vì một lần | E2B hiểu "mai" (Chủ nhật) thành ngày trong tuần |
| "7 giờ tối từ thứ Hai đến thứ Sáu" | sai ngày (1 lần) | E2B |
| "Xóa báo thức 6 giờ 45" | xóa nhầm (chữ) / nghe thành "6 giờ 40 năm" (nói) | E2B / Whisper |
| "Tắt báo thức 7 giờ tối" (nói) | xóa thay vì tắt | Whisper nghe "Cắt", E2B hiểu là xóa |
| "Tao có những báo thức nào" (nói) | trả lời lạc | Whisper nghe "Cao vào những bảo thức nào" |
| "Xong rồi, đánh dấu xong đi" (nói) | không đánh dấu | Whisper nghe "đánh xứ xong" |
| "Chơi đoán số đi" → "Số 5" | trò chơi không bắt đầu, "số 5" hiểu thành tin số 5 | E2B |

Lỗi khác hay gặp nhưng không làm hỏng việc: lặp câu cửa miệng ("Vui quá!"), lộ số bí mật của trò đoán số, nói
"đã nhớ" nhưng đôi khi quên gửi hành động `remember` / `name`. E2B không viết `summary` khi được yêu cầu trong
phiên nói (có viết trong phiên gõ), nên lịch sử trượt và mỗi lượt phải đọc lại ~1 100 token.

## 3. Nghe giọng (bin/stt-server, `bench/speech_bench.py`)

42 câu hỏi (28 câu, 2 giọng VieNeu, tổng 94,7 s tiếng nói), so với câu gốc bằng tỉ lệ lỗi ký tự (CER):

| Cửa sổ encoder | Trung vị | p90 | CER | Đúng y hệt |
|---|---|---|---|---|
| 30 s (mặc định Whisper) | 1 004 ms | 1 026 ms | 5,1% | 28/42 |
| 1 000 khung (20 s) | 652 ms | 673 ms | 4,7% | 27/42 |
| **768 khung (15 s), đang dùng** | **491 ms** | 521 ms | **4,7%** | 27/42 |
| vừa khít câu, tối thiểu 512 | 333 ms | 353 ms | 9,3% | 27/42 |

Phần lớn "lỗi" còn lại là cách viết số: "6h30" thay vì "6 giờ 30", "thứ 2" thay vì "thứ Hai" (Gemma vẫn hiểu).
Lỗi thật: "Tao" → "Cao", "Tắt" → "Cắt"/"Át", "Mấy giờ rồi" → "Mấy sờ sôi" (cửa sổ quá ngắn).
Ghi âm thật từ micro của máy để bàn (28–85 s, phần lớn im lặng và "gâu gâu"): 1,0–3,3 s nhờ VAD bỏ im lặng.
Trong lượt nói thật qua gateway (Whisper chạy cạnh Gemma trên cùng GPU): 0,5–0,75 s mỗi câu.

## 4. Giọng đọc (VieNeu Turbo, 1 worker, 16 kHz PCM stream)

| Độ dài câu | Có tiếng sau | Xong sau | Dài tiếng | RTF |
|---|---|---|---|---|
| 30 ký tự | 168 ms | 0,54 s | 1,8 s | 0,29 |
| 66–74 ký tự | 197–208 ms | 0,96–1,09 s | 3,5–4,1 s | 0,27 |
| 184 ký tự (tin tức) | 303 ms | 2,35 s | 8,7 s | 0,27 |

## 5. So với hosted

| | Hosted (Gemini, đo từ log thiết bị) | Tại chỗ (M1 Pro, prompt gọn) |
|---|---|---|
| Gửi ghi âm → có tiếng | ~5 s | trung vị 5,4–5,9 s; câu thường 2,5–3,5 s; sau hành động 5–8 s; lượt đầu sau khi bật ~15 s |
| Câu trả lời JSON đúng | gần như luôn | 100% (grammar) |
| Hiểu và làm đúng | tốt hơn (model lớn hơn) | 24–27/28 câu thử |
| Chi phí mỗi lượt | tiền API | điện; máy phải bật 24/7 |
| Riêng tư | ghi âm gửi lên Google | không ra khỏi nhà |

## 6. Một máy phục vụ được bao nhiêu người

Mỗi lượt nói tốn GPU: Whisper ~0,6 s + Gemma đọc phần prompt mới (~600–1 300 token, 1–2,5 s, không chạy song song
được) + viết ~60–110 token (gộp batch được: 4 câu cùng lúc ~150 token/s). Trung bình ~2,5–3 s GPU mỗi lượt khi
nhiều người nói xen kẽ, tức trần **~1 200 lượt/giờ** nếu GPU chạy 100%. Để độ trễ vẫn dễ chịu (< ~8 s) nên giữ dưới
~50%: **~600 lượt/giờ**.

- Đo trực tiếp: 4 câu cùng lúc (phần chung đã cache) xong trong ~6 s mỗi câu; 2 câu cùng lúc ~5 s.
- Giả sử một hộ hỏi ~20 câu/ngày, giờ cao điểm ~5 câu: một máy M1 Pro 16 GB đủ cho **~50–100 hộ**, miễn không quá
  2–3 người nói đúng cùng một lúc (`slots: 2`; muốn 4 thì `slots: 4`, tốn thêm ~0,6 GB RAM).
- Giọng đọc chạy CPU, RTF 0,27: một worker VieNeu Turbo đọc nhanh gấp ~3,7 lần thời gian thật nhưng mỗi lúc một câu;
  nhiều người nói cùng lúc thì tăng `workers` (mỗi worker ~1,5 GB RAM) hoặc dùng Piper cho bớt tải.
- Mỗi hộ có lịch sử và dữ liệu riêng: chỉ phần cố định (~4 500 token: chó, luật, hành động) dùng chung cache giữa
  các hộ; nhiều hộ hơn số slot thì phần riêng bị đọc lại (thêm ~1–2 s mỗi lượt).
- M4 / M4 Pro (GPU nhanh hơn 1,5–2,5 lần) sẽ chịu được tương ứng nhiều hơn; chưa đo.

## Chạy lại

```bash
cd ai-kit/bench
# mô hình (tự bật llm-server riêng ở cổng 9411; tắt gateway trước cho GPU rảnh)
python3 llm_bench.py --model ../models/llm/gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf --label e2b-m1pro
# nghe + đọc (cần gateway đang chạy có vieneu-turbo)
python3 speech_bench.py --gateway http://127.0.0.1:8080 --key-file key.txt \
  --stt-model ../models/stt/ggml-large-v3-turbo-q5_0.bin --vad ../models/stt/ggml-silero-v6.2.0.bin --label m1pro
# trợ lý end-to-end (build + chạy máy chủ EspOS riêng ở 127.0.0.1:8095)
GATEWAY=http://127.0.0.1:8080 KEY_FILE=key.txt GATEWAY_LOG=../logs/gateway.log ./run_e2e.sh mylabel short --voice
GATEWAY=... ./run_e2e.sh full-run full          # prompt đầy đủ để so
```

`key.txt` chứa một API key của gateway. Máy chủ EspOS chỉ cho 15 lượt/phút mỗi route: bench tự chờ (không tính vào
thời gian). Nhiệt độ lấy mẫu 0,7 (máy chủ EspOS đặt) nên kết quả đúng/sai đổi 1–3 câu giữa các lần chạy.
