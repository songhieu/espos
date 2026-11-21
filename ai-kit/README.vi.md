# Bộ AI tiếng Việt chạy tại chỗ trên Mac: giọng đọc + mô hình trả lời + nghe giọng

Một API duy nhất có API key, chạy hết trên máy (không gọi dịch vụ AI nào bên ngoài):

- **Giọng đọc (TTS):** VieNeu-TTS Turbo/Nano, Piper. `POST /v1/tts`, `POST /v1/audio/speech`.
- **Mô hình trả lời (LLM):** Gemma 4 (E2B hoặc E4B) chạy bằng GPU qua `bin/llm-server` (C++ viết riêng trên thư viện
  llama.cpp, không dùng Ollama hay llama-server). `POST /v1/chat/completions` kiểu OpenAI.
- **Nghe giọng (STT):** Whisper large-v3-turbo qua `bin/stt-server` (C++ trên whisper.cpp). `POST /v1/audio/transcriptions`
  kiểu OpenAI.

Đủ để chạy trợ lý chó của EspOS (máy để bàn S3, EspOS Bot) không cần Gemini: nghe, trả lời, đọc báo, đặt báo thức, ghi
chú, nhắc việc... (xem [mục 8](#8-nối-vào-máy-chủ-espos-trợ-lý-chó) và [BENCHMARK.md](BENCHMARK.md)).
Có sẵn Cloudflare Tunnel nên máy ở nhà IP động vẫn gọi được từ bên ngoài.

```
ESP32 / EspOS / app / curl ──HTTPS──▶ Cloudflare ──tunnel──▶ Mac ở nhà
                                                            │
                                         tts-gateway (Go, cổng 8080)
                                         • kiểm tra API key
                                         • chọn model, xếp hàng, giới hạn luồng
                                         • tự bật lại worker / tunnel khi chết
                                                            │ 127.0.0.1
        ┌───────────────┬───────────────┬───────────────────┼────────────────────┐
  vieneu-turbo ×N   vieneu-nano ×N   piper ×N        bin/llm-server        bin/stt-server
  (Python, CPU)     (Python, CPU)    (Python, CPU)   Gemma 4, GPU Metal    Whisper, GPU Metal
```

## 1. Cài trên máy Mac mới

Yêu cầu: Mac Apple Silicon (M1–M4), có [Homebrew](https://brew.sh) và Xcode Command Line Tools (`xcode-select --install`).

```bash
# copy cả thư mục tts-server sang máy mới (không cần bin/, build/, third_party/, worker/.venv/, models/)
cd tts-server
./install.sh      # xem máy (chip, RAM, GPU, ổ đĩa), gợi ý bộ phù hợp, hỏi rồi tải + build + tự kiểm tra
./run.sh          # chạy
```

`install.sh` hỏi 3 câu (Enter = theo gợi ý):

| Câu hỏi | Lựa chọn | Dung lượng |
|---|---|---|
| Mô hình trả lời | `e2b` Gemma 4 E2B (nhẹ, nhanh) / `e4b` Gemma 4 E4B (khôn hơn, chậm hơn, chưa đo) / `none` | 2,6 GB / 4,2 GB |
| Nghe giọng | `turbo` Whisper large-v3-turbo (nghe tiếng Việt tốt) / `small` (nhẹ, sai nhiều hơn) / `none` | 0,57 GB / 0,19 GB |
| Giọng đọc | `turbo`, `nano`, `piper` (chọn nhiều, cách nhau dấu phẩy) / `none` | ~1–2 GB (Python + model) |

Gợi ý theo máy: 8 GB RAM → E2B + Whisper small + Piper; 16 GB → E2B + Whisper turbo + VieNeu Turbo + Piper, 2 câu hỏi
cùng lúc; 24 GB trở lên (và ổ còn ≥ 9 GB) → E4B, 4 câu hỏi cùng lúc. Mac Intel: chỉ nên cài Piper.
Bộ cài kiểm tra ổ đĩa trước khi tải (chừa lại 1 GB), tải tiếp được khi bị ngắt, build `llm-server`/`stt-server` từ
mã nguồn llama.cpp/whisper.cpp đúng phiên bản đã thử rồi xóa thư mục build, ghi `config.yaml` (tạo API key nếu chưa
có; nếu đã có thì chỉ thay danh sách `models:`, giữ key và tunnel, bản cũ ở `config.yaml.bak`), rồi chạy thử từng
model và in thời gian.

Không hỏi gì (theo gợi ý), hay chọn sẵn:

```bash
./install.sh --yes
./install.sh --llm e2b --stt turbo --tts turbo,piper --yes
./install.sh --check          # chỉ xem máy và gợi ý
./setup.sh                    # như trước: chỉ giọng đọc (VieNeu Turbo + Piper)
```

Kiểm tra:

```bash
curl http://127.0.0.1:8080/health
curl -H "Authorization: Bearer <KEY>" http://127.0.0.1:8080/v1/models
KEY=<KEY> python3 bench/quick_test.py --gateway http://127.0.0.1:8080
```

## 2. Cấu hình (`config.yaml`)

Mọi thứ nằm trong một file này. Sửa xong thì chạy lại `./run.sh`, hoặc `./service.sh restart` nếu đang chạy nền.

| Mục | Ý nghĩa |
|---|---|
| `api_keys` | Danh sách key. Mỗi thiết bị một key để lỡ lộ thì xóa đúng key đó. Tạo key: `openssl rand -hex 24` |
| `models[].workers` | Số request **chạy cùng lúc** của model đó. `0` = tắt model |
| `models[].threads` | Số nhân CPU mỗi worker dùng |
| `queue_timeout` | Hết worker rảnh thì request chờ tối đa bấy lâu, quá thì trả `429` |
| `listen` | `0.0.0.0:8080` = máy khác trong mạng nhà gọi được. `127.0.0.1:8080` = chỉ qua tunnel |
| `tunnel` | Xem mục 4 |

Số đo trên M1 Pro (chưa đo trên M4):

| Model | Chất lượng | Tốc độ (RTF, càng nhỏ càng nhanh) | Có tiếng sau | RAM / worker |
|---|---|---|---|---|
| `vieneu-turbo` | tốt nhất, 48 kHz | ~0.35 | ~0.3s (stream thật) | ~1.4GB |
| `vieneu-nano` | kém hơn, 24 kHz | ~0.21 | ~1.2s (đợi xong câu) | ~0.5GB |
| `piper` | nghe "máy" hơn, 22 kHz | ~0.03 | ~0.2–0.5s | ~0.3GB |

- **Turbo:** trên M1 Pro chạy được khoảng 4 worker cùng lúc mà vẫn kịp real-time.
- **Nano:** không nên tăng nhiều worker. Mỗi luồng Nano đã chiếm gần hết CPU, thêm worker là chậm đều.
- **Piper:** chịu 20 luồng cùng lúc vẫn kịp.

Config mặc định là turbo 2 + nano 1 + piper 4, tốn khoảng 4.5GB RAM, hợp với cỡ "nửa con M4 24GB".

Mô hình trả lời và nghe giọng (đo trên M1 Pro 16 GB, chi tiết ở [BENCHMARK.md](BENCHMARK.md)):

| Model | RAM | Tốc độ |
|---|---|---|
| `gemma-4-e2b` (llm) | ~2,6 GB với 2 slot (1,8 GB phần model đang dùng + 0,8 GB cache) | đọc prompt 370–720 token/s, viết ~70 token/s (4 câu cùng lúc: ~150 token/s tổng) |
| `whisper` turbo (stt) | ~0,7 GB | câu nói 2–4 s → ~0,5 s |

- `slots` của llm: số câu hỏi trả lời cùng lúc trong 1 tiến trình (chung 1 lần nạp model). Đừng tăng `workers` của
  llm (mỗi worker nạp model thêm một lần, GPU vẫn chỉ có một).
- llm nhớ phần đầu của prompt vừa đọc (prefix cache): câu hỏi sau có cùng phần đầu chỉ phải đọc phần mới.

## 3. Dùng API

Mọi request (trừ `/health`) cần header `Authorization: Bearer <KEY>` hoặc `X-API-Key: <KEY>`.

### `GET /v1/models`: danh sách model và giọng

Mỗi giọng có `id` (tên có dấu, như `Hải Đăng`) và `slug` (không dấu, như `hai-dang`). Gửi tên nào cũng được.

### `POST /v1/tts`: chữ → giọng nói

```json
{
  "model": "vieneu-turbo",
  "voice": "hai-dang",
  "text": "Xin chào, hôm nay trời đẹp quá.",
  "format": "wav",
  "sample_rate": 16000,
  "stream": true
}
```

| Trường | Mặc định | Ghi chú |
|---|---|---|
| `model` | model đầu tiên trong config | `vieneu-turbo` / `vieneu-nano` / `piper` (theo `id` trong config) |
| `voice` | giọng mặc định của model | `id` hoặc `slug` |
| `text` | (bắt buộc) | tối đa `max_chars` ký tự |
| `format` | `wav` | `wav` hoặc `pcm` (16-bit little-endian, mono, không header) |
| `sample_rate` | tần số gốc của model | 8000–48000, tự đổi tần số. ESP32 nên dùng `16000` |
| `stream` | `true` | `true`: gửi dần từng đoạn, có tiếng sớm. `false`: trả nguyên file, có `Content-Length` |

Header trả về: `X-Sample-Rate`, `X-Voice` (slug), `X-Voice-Name` (tên có dấu, dạng mã hóa URL), `X-Model`.

Ví dụ:

```bash
KEY=...; URL=http://127.0.0.1:8080        # hoặc https://tts.tenmien.com

# Lưu file wav
curl -H "Authorization: Bearer $KEY" -X POST $URL/v1/tts \
  -d '{"model":"vieneu-turbo","voice":"mai-anh","text":"Xin chào các bạn."}' -o out.wav && afplay out.wav

# PCM 16 kHz cho ESP32
curl -H "Authorization: Bearer $KEY" -X POST $URL/v1/tts \
  -d '{"model":"piper","text":"Đã bật đèn phòng khách.","format":"pcm","sample_rate":16000}' -o out.pcm
```

### `POST /v1/audio/speech`: tương thích OpenAI

Dùng được với các thư viện gọi OpenAI TTS: `{"model","input","voice","response_format":"wav|pcm"}`.
Riêng `pcm` mặc định là 24 kHz, giống OpenAI.

### `POST /v1/chat/completions`: mô hình trả lời (kiểu OpenAI)

```bash
curl -H "Authorization: Bearer $KEY" $URL/v1/chat/completions -d '{
  "model": "gemma-4-e2b",
  "messages": [{"role": "system", "content": "Bạn là chú chó Boop."}, {"role": "user", "content": "Mấy giờ rồi?"}],
  "max_tokens": 200, "temperature": 0.7,
  "response_format": {"type": "json_object"},
  "stream": false}'
```

| Trường | Ghi chú |
|---|---|
| `messages` | `system`, `user`, `assistant`, chỉ chữ (ảnh, âm thanh: trả 400; âm thanh thì gửi `/v1/audio/transcriptions` trước) |
| `response_format` | `{"type":"json_object"}`: ép trả đúng một đối tượng JSON (grammar), dừng ngay khi đóng `}` |
| `stream` | `true`: Server-Sent Events như OpenAI (`data: {...}` ... `data: [DONE]`) |
| `max_tokens`, `temperature`, `top_p`, `top_k`, `min_p`, `stop`, `seed` | như OpenAI; mặc định 1024, 0.7, 0.95, 64 |
| `chat_template_kwargs.enable_thinking` | `true`: Gemma "nghĩ" trước khi trả lời (trả về `reasoning_content`), chậm hơn |

Trả về như OpenAI, thêm `usage.prompt_tokens_details.cached_tokens` (số token lấy từ cache) và `timings`
(`ttft_ms`, `prompt_per_second`, `predicted_per_second`...). Tên `model` gửi lên không có thì dùng model llm đầu tiên.

### `POST /v1/audio/transcriptions`: nghe giọng (kiểu OpenAI)

```bash
curl -H "Authorization: Bearer $KEY" $URL/v1/audio/transcriptions \
  -F file=@cau-hoi.wav -F model=whisper -F language=vi -F response_format=json
# {"text":"Đặt báo thức 6 giờ sáng mai."}
```

WAV 8/16/24/32-bit hoặc float, tần số nào cũng được, mono/stereo. `response_format`: `json` (mặc định), `text`,
`verbose_json` (thêm `segments`, `duration`, `timings`). Đoạn chỉ có tiếng ồn trả `""` (VAD bỏ im lặng; câu Whisper
hay "bịa" như "Hãy subscribe cho kênh..." bị lọc).

### Mã lỗi

| Mã | Ý nghĩa |
|---|---|
| `401` | sai hoặc thiếu key |
| `400` | sai tham số. Body JSON có `error`; nếu sai tên giọng thì có thêm danh sách `voices` |
| `429` | model đang bận quá lâu (`queue_timeout`) hoặc hàng chờ đã đầy (`max_waiting`). Đợi theo `Retry-After` rồi gửi lại |
| `502` | worker vừa chết. Gateway tự bật lại worker, gửi lại sau vài giây |

### Đọc PCM đúng cách (nếu không sẽ chỉ nghe "rè rè")

PCM là các mẫu 16-bit, mỗi mẫu 2 byte. Khi đi qua Internet hoặc Cloudflare, mỗi lần `read()` có thể trả về **số byte lẻ** (đo được: khoảng 60–80% số lần đọc trả về 1369, 1301… byte).
Nếu đẩy thẳng từng lần đọc ra loa, một mẫu sẽ bị cắt đôi. Từ đó mọi byte phía sau lệch một nấc và tiếng biến thành nhiễu trắng.

**Cách sửa:** giữ byte lẻ lại, ghép vào đầu lần đọc sau.

```cpp
// ESP32 (Arduino): stream là WiFiClient / HTTPClient::getStreamPtr()
uint8_t buf[2048 + 1]; size_t carry = 0;
while (http.connected() || stream->available()) {
  int n = stream->read(buf + carry, 2048);
  if (n <= 0) { delay(1); continue; }
  size_t total = carry + n, even = total & ~1u;
  size_t written; i2s_write(I2S_NUM_0, buf, even, &written, portMAX_DELAY);
  carry = total - even;                 // 0 hoặc 1 byte
  if (carry) buf[0] = buf[even];        // đưa byte lẻ lên đầu cho lần sau
}
```

```python
# Python
carry = b""
for chunk in resp.iter_content(4096):
    data = carry + chunk
    even = len(data) & ~1
    play(data[:even])          # int16 little-endian, mono
    carry = data[even:]
```

Dùng `"format":"wav"` cũng vậy: sau 44 byte header là PCM, xử lý y như trên.

### Gợi ý cho ESP32-S3

- Gửi `"format":"pcm","sample_rate":16000`, đọc body theo từng đoạn rồi đẩy thẳng ra I2S (MAX98357A…).
- Mỗi luồng 16 kHz chỉ khoảng 32 KB/s.
- Câu trả lời dài thì cắt thành từng câu, gửi lần lượt để câu đầu có tiếng sớm.
- Ra Internet thì dùng HTTPS (`WiFiClientSecure` / `esp_http_client`). Cloudflare lo phần chứng chỉ.

## 4. Truy cập từ ngoài: Cloudflare Tunnel

Không cần IP tĩnh, không cần mở port trên router (chạy được cả khi nhà mạng dùng CGNAT).
Gateway tự chạy `cloudflared`, tự kết nối lại khi rớt mạng.

### Cách A: `quick` (thử nhanh, không cần tài khoản)

```yaml
tunnel:
  enabled: true
  mode: quick
```

Log sẽ in `PUBLIC URL: https://xxxx.trycloudflare.com`. **Link này đổi mỗi lần chạy lại**, nên chỉ dùng để thử.

### Cách B: `token` (link cố định trên tên miền của bạn, dùng thật)

Cần một tên miền đã thêm vào Cloudflare (gói miễn phí là đủ).

1. Vào Cloudflare dashboard → **Zero Trust** → **Networks** → **Tunnels** → **Create a tunnel** → chọn **Cloudflared**, đặt tên (ví dụ `espos-ai`).
2. Ở bước cài connector, copy **token**: chuỗi dài sau `--token` trong lệnh mẫu. Không cần chạy lệnh đó, gateway tự chạy.
3. Ở bước **Public Hostname**: subdomain `tts`, domain `tenmien.com`, Service **HTTP** → `localhost:8080`.
4. Sửa `config.yaml`:

```yaml
tunnel:
  enabled: true
  mode: token
  token: "eyJhIjoi..."           # hoặc ${CLOUDFLARE_TUNNEL_TOKEN}
  public_url: "https://tts.tenmien.com"
```

5. Chạy `./run.sh`. Log hiện `[tunnel] connected to Cloudflare` là xong. Gọi `https://tts.tenmien.com/v1/tts` từ bất cứ đâu.

Giao diện Cloudflare thỉnh thoảng đổi tên menu, nhưng các bước vẫn là: tạo tunnel, lấy token, trỏ hostname về `http://localhost:8080`.

**Hoặc làm bằng dòng lệnh**:

```bash
cloudflared tunnel login                              # mở trình duyệt → chọn domain → Authorize
cloudflared tunnel create espos-ai                    # tạo tunnel
cloudflared tunnel route dns espos-ai tts.tenmien.com # tạo CNAME trỏ vào tunnel
cloudflared tunnel token espos-ai                     # in token → dán vào tunnel.token trong config.yaml
```

Chuyển sang máy khác thì chỉ cần mang **token** theo (copy cả `config.yaml` là đủ), không phải tạo lại tunnel.
Đừng chạy cùng một token trên 2 máy cùng lúc: Cloudflare sẽ chia request ngẫu nhiên giữa 2 máy.

Khi đã dùng tunnel mà không cần gọi trực tiếp trong mạng nhà, có thể đổi `listen: "127.0.0.1:8080"`.

## 5. Chạy nền và tự khởi động

```bash
./service.sh install     # tự chạy khi đăng nhập Mac, tự bật lại nếu bị tắt
./service.sh logs        # xem log
./service.sh restart     # sau khi sửa config.yaml
./service.sh uninstall
```

Để máy Mac ở nhà chạy ổn định:

- Cắm sạc. `keep_awake: true` sẽ chạy `caffeinate` để Mac không ngủ khi gateway đang chạy.
- System Settings → **Users & Groups**: bật tự đăng nhập, để mất điện có lại thì service tự lên.
- System Settings → **Energy**: bật "Start up automatically after a power failure" nếu có.

## 6. Bảo mật

- Gateway **không chịu chạy** khi chưa có API key, hoặc key còn là `CHANGE_ME`.
- Worker Python chỉ nghe trên `127.0.0.1` và đòi một token nội bộ do gateway sinh ngẫu nhiên mỗi lần chạy.
- Token tunnel được truyền qua biến môi trường, không hiện trong `ps`.
- `config.yaml` chứa key và token: đừng commit, đừng gửi cho ai. `.gitignore` đã loại nó ra.
- Lộ một key thì xóa key đó khỏi `config.yaml` rồi restart.

## 7. Cấu trúc thư mục

```
tts-server/
├── config.example.yaml   mẫu cấu hình (có chú thích)
├── config.yaml           cấu hình thật (install.sh tạo, chứa key)
├── install.sh            cài đặt: xem máy, chọn model, tải, build, tự kiểm tra
├── setup.sh              lệnh cũ: chỉ giọng đọc (gọi install.sh)
├── run.sh                chạy
├── service.sh            chạy nền bằng launchd
├── gateway/              Go: API, API key, hàng đợi, giám sát worker, Cloudflare Tunnel; ai.go = route LLM/STT
├── worker/               Python: worker.py chạy VieNeu-TTS / Piper
├── llm/                  C++: llm-server.cpp (Gemma trên llama.cpp), check_template.py
├── stt/                  C++: stt-server.cpp (Whisper trên whisper.cpp)
├── bench/                đo: llm_bench.py, speech_bench.py, assist_bench.py + run_e2e.sh (trợ lý EspOS), quick_test.py
├── BENCHMARK.md          kết quả đo trên M1 Pro
├── HANDOFF.md            ghi chú bàn giao (đã làm gì, chưa làm gì, build/chạy/test)
├── third_party/          mã nguồn llama.cpp, whisper.cpp (install.sh tải, không cần mang theo)
└── models/               model đã tải: llm/, stt/, piper/
```

## 8. Nối vào máy chủ EspOS (trợ lý chó)

Máy chủ EspOS tự host (`espos/server`) gọi AI theo kiểu OpenAI. Trong file `.env` của nó (thay `<KEY>` bằng một API
key trong `config.yaml`; máy chủ EspOS chạy Docker thì dùng `http://host.docker.internal:8080`):

```sh
ESPOS_AI_URL=http://127.0.0.1:8080/v1
ESPOS_AI_KEY=<KEY>
ESPOS_AI_MODEL=gemma-4-e2b
ESPOS_AI_PROMPT=short          # prompt gọn, xếp cho cache của model chạy tại chỗ (nhanh gấp ~2, xem BENCHMARK.md)
ESPOS_AI_TIMEOUT=60            # lượt đầu sau khi bật máy phải đọc cả prompt: ~15 s
ESPOS_STT_URL=http://127.0.0.1:8080/v1
ESPOS_STT_KEY=<KEY>
ESPOS_STT_MODEL=whisper
ESPOS_STT_LANG=vi
ESPOS_TTS_API=espos
ESPOS_TTS_URL=http://127.0.0.1:8080/v1/tts
ESPOS_TTS_KEY=<KEY>
ESPOS_TTS_MODEL=vieneu-turbo
```

Một lượt nói trên M1 Pro (từ lúc gửi ghi âm tới lúc có tiếng): trung vị ~5,5 s (câu thường 2,5–3,5 s, câu ngay sau
khi đặt báo thức/ghi chú 5–8 s, lượt đầu sau khi bật ~15 s); câu trả lời đúng JSON 100%, làm đúng việc 24–27/28 câu
thử. Xem [BENCHMARK.md](BENCHMARK.md).

## Ghi chú

- Phiên bản: VieNeu-TTS `3.8.4` (Apache-2.0), piper-tts `1.8.0` (GPL-3.0), llama.cpp `v1.0.0` và whisper.cpp `v1.9.5`
  (MIT), Gemma 4 (Apache-2.0, bản lượng tử QAT của unsloth), Whisper (MIT), Silero VAD (MIT).
  Dùng cá nhân thì thoải mái. Nếu đóng gói bán kèm sản phẩm thì cần xem kỹ giấy phép GPL của Piper.
- VieNeu tự chèn watermark âm thanh (không nghe thấy) vào giọng sinh ra. Đây là mặc định của thư viện.
- Giọng Piper `25hours_single-low` và `vivos-x_low` bị mất ký hiệu thanh điệu với piper-tts 1.8 (log báo `Missing phoneme`), dễ đọc sai dấu. Config mặc định chỉ bật `vais1000`.
