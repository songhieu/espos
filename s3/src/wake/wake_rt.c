#include "wake_rt.h"

#include <math.h>
#include <string.h>

float ewm_last_logits[EWM_MAX_OUT];

static uint32_t crc32_update(uint32_t c, const uint8_t* p, uint32_t n) {
  c = ~c;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static uint32_t align4(uint32_t x) { return (x + 3u) & ~3u; }

static int ring_len(const ewm_hdr_t* h, int b) { return (h->k - 1) * h->dil[b] + 1; }

uint32_t ewm_state_size(const void* model) {
  const ewm_hdr_t* h = (const ewm_hdr_t*)model;
  uint32_t n = align4((uint32_t)h->k0 * h->bands);
  for (int b = 0; b < h->blocks; b++) n += align4((uint32_t)ring_len(h, b) * h->ch);
  n += align4(h->ch) * 3 + align4(h->hc);
  return n;
}

// Walk the tensors; returns the bytes used, or 0 if they do not fit in `size`.
static uint32_t layer(const uint8_t* base, uint32_t off, uint32_t size, uint32_t nw, uint32_t nch, ewm_layer_t* L) {
  off = align4(off);
  const uint32_t need = align4(nw) + nch * 8u;
  if (off + need > size) return 0;
  L->w = (const int8_t*)(base + off);
  L->m = (const float*)(base + off + align4(nw));
  L->b = L->m + nch;
  return off + need;
}

int ewm_init(ewm_t* m, const void* model, uint32_t size, void* mem) {
  const ewm_hdr_t* h = (const ewm_hdr_t*)model;
  const uint8_t* base = (const uint8_t*)model;
  memset(m, 0, sizeof(*m));
  if (size < sizeof(ewm_hdr_t) || h->magic != EWM_MAGIC || h->version != 1 || h->hdr_size != sizeof(ewm_hdr_t)) return -1;
  if (h->total_size != size) return -2;
  if (crc32_update(0, base + h->hdr_size, size - h->hdr_size) != h->crc32) return -3;
  if (h->bands != 40 || h->blocks > EWM_MAX_BLOCKS || h->nout > EWM_MAX_OUT || h->nout == 0 || h->smooth > 8 ||
      h->smooth == 0 || h->stride == 0 || h->k0 == 0 || h->k == 0 || h->ch == 0 || h->hc == 0)
    return -4;
  m->h = h;
  uint32_t off = h->hdr_size;
  const uint32_t ch = h->ch;
  if (!(off = layer(base, off, size, ch * h->k0 * h->bands, ch, &m->stem))) return -2;
  for (int b = 0; b < h->blocks; b++) {
    if (!(off = layer(base, off, size, ch * h->k, ch, &m->dw[b]))) return -2;
    if (!(off = layer(base, off, size, ch * ch, ch, &m->pw[b]))) return -2;
    if (off + 4 > size) return -2;
    memcpy(&m->r[b], base + off, 4);
    off += 4;
  }
  if (!(off = layer(base, off, size, h->hc * ch, h->hc, &m->head))) return -2;
  if (!(off = layer(base, off, size, (uint32_t)h->nout * h->hc, h->nout, &m->out))) return -2;
  if (align4(off) != size) return -2;
  uint8_t* p = (uint8_t*)mem;
  m->fring = (int8_t*)p;
  p += align4((uint32_t)h->k0 * h->bands);
  for (int b = 0; b < h->blocks; b++) {
    m->ring[b] = p;
    m->rlen[b] = ring_len(h, b);
    p += align4((uint32_t)m->rlen[b] * ch);
  }
  m->a = p;
  p += align4(ch);
  m->z = p;
  p += align4(ch);
  m->o = p;
  p += align4(ch);
  m->y = p;
  ewm_reset(m);
  return 0;
}

void ewm_reset(ewm_t* m) {
  const ewm_hdr_t* h = m->h;
  memset(m->fring, 0, (size_t)h->k0 * h->bands);
  for (int b = 0; b < h->blocks; b++) {
    memset(m->ring[b], 0, (size_t)m->rlen[b] * h->ch);
    m->rpos[b] = 0;
  }
  m->frames = 0;
  memset(m->hist, 0, sizeof(m->hist));
  memset(m->prob, 0, sizeof(m->prob));
  m->hpos = 0;
  m->quiet = 0;
}

static inline uint8_t q8(float y) {
  if (y <= 0) return 0;  // ReLU
  y += 0.5f;
  return y >= 255.0f ? 255 : (uint8_t)y;
}

static int32_t dot_u8_s8(const uint8_t* a, const int8_t* w, int n) {
  int32_t s = 0;
  for (int i = 0; i < n; i++) s += (int32_t)a[i] * w[i];
  return s;
}

int ewm_push(ewm_t* m, const int8_t* frame, const float* thr) {
  const ewm_hdr_t* h = m->h;
  const int ch = h->ch, bands = h->bands, k0 = h->k0;
  // feature ring: oldest first
  memmove(m->fring, m->fring + bands, (size_t)(k0 - 1) * bands);
  memcpy(m->fring + (k0 - 1) * bands, frame, (size_t)bands);
  const uint32_t f = m->frames++;
  if (f % h->stride) return -1;
  // stem: int8 features x int8 weights
  uint8_t* a = m->a;
  for (int c = 0; c < ch; c++) {
    const int8_t* w = m->stem.w + c * k0 * bands;
    int32_t s = 0;
    for (int i = 0; i < k0 * bands; i++) s += (int32_t)m->fring[i] * w[i];
    a[c] = q8((float)s * m->stem.m[c] + m->stem.b[c]);
  }
  for (int b = 0; b < h->blocks; b++) {
    const int len = m->rlen[b], d = h->dil[b], k = h->k;
    uint8_t* ring = m->ring[b];
    int pos = m->rpos[b];
    memcpy(ring + pos * ch, a, (size_t)ch);  // newest input at pos
    // depthwise: tap i (0 = oldest) reads the input (k-1-i)*d steps back
    uint8_t* z = m->z;
    for (int c = 0; c < ch; c++) {
      const int8_t* w = m->dw[b].w + c * k;
      int32_t s = 0;
      for (int i = 0; i < k; i++) {
        int p = pos - (k - 1 - i) * d;
        if (p < 0) p += len;
        s += (int32_t)ring[p * ch + c] * w[i];
      }
      z[c] = q8((float)s * m->dw[b].m[c] + m->dw[b].b[c]);
    }
    m->rpos[b] = pos + 1 == len ? 0 : pos + 1;
    // pointwise + residual (the block's input, a)
    uint8_t* o = m->o;
    for (int c = 0; c < ch; c++) {
      const int32_t s = dot_u8_s8(z, m->pw[b].w + c * ch, ch);
      o[c] = q8((float)s * m->pw[b].m[c] + m->pw[b].b[c] + (float)a[c] * m->r[b]);
    }
    memcpy(a, o, (size_t)ch);
  }
  uint8_t* y = m->y;
  for (int c = 0; c < h->hc; c++) y[c] = q8((float)dot_u8_s8(a, m->head.w + c * ch, ch) * m->head.m[c] + m->head.b[c]);
  int fired = 0;
  const int sm = h->smooth;
  for (int o = 0; o < h->nout; o++) {
    const float lg = (float)dot_u8_s8(y, m->out.w + o * h->hc, h->hc) * m->out.m[o] + m->out.b[o];
    ewm_last_logits[o] = lg;
    m->hist[o][m->hpos % sm] = 1.0f / (1.0f + expf(-lg));
    float avg = 0;
    for (int i = 0; i < sm; i++) avg += m->hist[o][i];
    m->prob[o] = avg / (float)sm;
    const float t = thr ? thr[o] : h->thr[o];
    if (m->prob[o] >= t) fired |= 1 << o;
  }
  m->hpos++;
  if (m->quiet > 0) {
    m->quiet--;
    return 0;
  }
  if (fired) m->quiet = h->refractory;
  return fired;
}
