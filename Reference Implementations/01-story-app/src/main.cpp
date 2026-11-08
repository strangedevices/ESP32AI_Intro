// Stage 01 - a 28.9M-parameter language model running entirely on the CoreS3.
//
// Port of slvDev/esp32-ai "esp32_tinystories" (MIT) to the M5Stack CoreS3 / CoreS3 SE
// with PlatformIO + M5Unified. The inference core (llm.h) is unchanged; what is new:
//   - M5GFX scrolling story view with a status bar (tokens/s)
//   - tap the screen to stop the current story and start a new one
//   - temperature + top-k sampling so every story is different
//   - prompts cycle through a small list; serial 'n' also starts a new story
//
// Memory placement (from the original):
//   flash   PLE table + token embedding, memory-mapped from the "model" partition
//   PSRAM   staged int8 core + head, KV cache, logits
//   SRAM    scratch + norm vectors

#include <M5Unified.h>
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_random.h"

#define LLM_INT8_ACT 1
#define LLM_PROFILE 1
#define LLM_PROFILE_NOW() esp_timer_get_time()
#include "llm.h"
#include "vocab.h"

// ---- tunables --------------------------------------------------------------
static const int   N_GENERATE  = 200;   // tokens per story
static const float TEMPERATURE = 0.8f;  // 0 = greedy (same story every time)
static const int   TOP_K       = 40;

// Token ids for each starting prompt (generated from tokenizer.json).
static const int P0[] = { 433, 447, 259, 405 };                       // "Once upon a time"
static const int P1[] = { 427, 358, 12, 259, 403, 450, 502, 360 };    // "One day, a little girl named Lily"
static const int P2[] = { 382, 403, 2463, 282 };                      // "The little robot was"
static const int P3[] = { 784, 265, 352, 635, 472, 266, 263, 568 };   // "Tom and his dog went to the park"
static const int P4[] = { 892, 282, 259, 1395, 265, 11903, 1178 };    // "It was a dark and stormy night"
static const int P5[] = { 1396, 364, 259, 410, 921, 678 };            // "Sam had a big red ball"
struct Prompt { const int *ids; int n; };
static const Prompt PROMPTS[] = {
  { P0, 4 }, { P1, 8 }, { P2, 4 }, { P3, 8 }, { P4, 7 }, { P5, 6 },
};
static const int N_PROMPTS = sizeof(PROMPTS) / sizeof(PROMPTS[0]);

// ---- model state -----------------------------------------------------------
static Model model;
static Scratch s;
static bool modelReady = false;
static size_t psram_used = 0, sram_used = 0;
#define STATIC_SRAM_BYTES (2 * LLM_Q8_MAX_INPUT)

static void *ps(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
  if (p) psram_used += n;
  return p;
}
static void fatal(const char *msg) {
  Serial.printf("FATAL: %s\n", msg);
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(4, 40);
  M5.Display.printf("FATAL: %s", msg);
  while (1) delay(1000);
}
static void *ps_or_die(size_t n, const char *what) {
  void *p = ps(n);
  if (!p) fatal(what);
  return p;
}
static void *sram_or_die(size_t n, const char *what) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!p) fatal(what);
  sram_used += n;
  return p;
}

// ---- dual-core int8 matvec (unchanged from the original) -------------------
static TaskHandle_t worker_h, main_h;
static const QT *job_t;
static const int8_t *job_xq;
static float job_xs;
static float *job_y;
static int job_split;

static void worker_main(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    matvec_i8_range(job_t, job_xq, job_xs, job_y, 0, job_split);
    xTaskNotifyGive(main_h);
  }
}

static void matvec_par(const QT *t, const float *x, float *y) {
  static int8_t xq[LLM_Q8_MAX_INPUT];
  float xs;
  if (t->w8 == NULL || t->rows < 128) { MATVEC(t, x, y); return; }
  quantize_act(x, t->cols, xq, &xs);
  job_t = t; job_xq = xq; job_xs = xs; job_y = y; job_split = t->rows / 2;
  xTaskNotifyGive(worker_h);
  matvec_i8_range(t, xq, xs, y, job_split, t->rows);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

static void copy_norms_to_sram() {
  Cfg *c = &model.c;
  int D = c->dim, L = c->n_layers, P = c->ple_dim;
  const float **vecs[3 * 32 + 2];
  int sizes[3 * 32 + 2], n_vec = 0;
  vecs[n_vec] = &model.ple_proj_norm; sizes[n_vec++] = P;
  for (int l = 0; l < L; l++) {
    vecs[n_vec] = &model.attn_norm[l]; sizes[n_vec++] = D;
    vecs[n_vec] = &model.ffn_norm[l];  sizes[n_vec++] = D;
    vecs[n_vec] = &model.ple_norm[l];  sizes[n_vec++] = D;
  }
  vecs[n_vec] = &model.out_norm; sizes[n_vec++] = D;
  for (int i = 0; i < n_vec; i++) {
    size_t bytes = (size_t)sizes[i] * sizeof(float);
    void *dst = sram_or_die(bytes, "norm vector");
    memcpy(dst, *vecs[i], bytes);
    *vecs[i] = (const float *)dst;
  }
}

static void alloc_scratch() {
  Cfg *c = &model.c;
  int D = c->dim, L = c->n_layers, P = c->ple_dim, F = c->ffn, S = c->seq_len;
  s.x      = (float *)sram_or_die(D * 4, "x");
  s.h      = (float *)sram_or_die((F > D ? F : D) * 4, "h");
  s.qkv    = (float *)sram_or_die(3 * D * 4, "qkv");
  s.att    = (float *)sram_or_die(D * 4, "att");
  s.g1     = (float *)sram_or_die(F * 4, "g1");
  s.g2     = (float *)sram_or_die((P > F ? P : F) * 4, "g2");
  s.ple    = (float *)sram_or_die(L * P * 4, "ple");
  s.tmpP   = (float *)sram_or_die(L * P * 4, "tmpP");
  s.trow   = (float *)sram_or_die(L * P * 4, "trow");
  s.scores = (float *)sram_or_die(S * 4, "scores");
  s.logits = (float *)ps_or_die((size_t)model.out_vocab * 4, "logits");
  s.kcache = (float *)ps_or_die((size_t)L * S * D * 4, "kcache");
  s.vcache = (float *)ps_or_die((size_t)L * S * D * 4, "vcache");
}

// ---- display ---------------------------------------------------------------
// Status bar drawn straight on the panel; the story lives in an off-screen
// canvas (sprite) with text scrolling enabled, pushed to the panel after each
// token. Drawing text directly on the panel wraps back to the top when it
// reaches the bottom; a canvas with setTextScroll scrolls instead.
static const int BAR_H = 24;
static int screenW, screenH;
static M5Canvas story(&M5.Display);

static void drawStatus(const char *msg) {
  M5.Display.fillRect(0, 0, screenW, BAR_H, TFT_NAVY);
  M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(6, 4);
  M5.Display.print(msg);
}

static void initStoryCanvas() {
  story.setPsram(true);
  story.setColorDepth(8);
  story.createSprite(screenW, screenH - BAR_H);
  story.setTextSize(2);
  story.setTextWrap(true, true);
  story.setTextScroll(true);
  story.setScrollRect(0, 0, screenW, screenH - BAR_H, TFT_BLACK);
}

static void clearStory() {
  story.fillScreen(TFT_BLACK);
  story.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
  story.setCursor(4, 4);
  story.pushSprite(0, BAR_H);
}

static void emit(int tok) {
  if (tok >= VOCAB_N) return;
  const unsigned char *bytes = VOCAB_BLOB + VOCAB_OFF[tok];
  int len = VOCAB_OFF[tok + 1] - VOCAB_OFF[tok];
  if ((int)Serial.availableForWrite() >= len) Serial.write(bytes, len);
  // Wrap whole tokens: if this word will not fit, start a new line first.
  int w = story.textWidth(String((const char *)bytes, len));
  if (story.getCursorX() + w > screenW - 4) story.print('\n');
  for (int i = 0; i < len; i++) {
    char c = (char)bytes[i];
    if (c == '\n' || (c >= 32 && c < 127)) story.print(c);
  }
  story.pushSprite(0, BAR_H);
}

// ---- sampling --------------------------------------------------------------
static int sample(const float *logits, int n) {
  if (TEMPERATURE <= 0.f) {
    int best = 0; float bv = -1e30f;
    for (int v = 0; v < n; v++) if (logits[v] > bv) { bv = logits[v]; best = v; }
    return best;
  }
  // top-k by partial selection (k is small)
  static int   idx[64];
  static float val[64];
  int k = TOP_K > 64 ? 64 : TOP_K;
  int cnt = 0;
  for (int v = 0; v < n; v++) {
    float x = logits[v];
    if (cnt < k) {
      int i = cnt++;
      while (i > 0 && val[i - 1] < x) { val[i] = val[i - 1]; idx[i] = idx[i - 1]; i--; }
      val[i] = x; idx[i] = v;
    } else if (x > val[k - 1]) {
      int i = k - 1;
      while (i > 0 && val[i - 1] < x) { val[i] = val[i - 1]; idx[i] = idx[i - 1]; i--; }
      val[i] = x; idx[i] = v;
    }
  }
  float mx = val[0], sum = 0.f;
  static float pr[64];
  for (int i = 0; i < cnt; i++) { pr[i] = expf((val[i] - mx) / TEMPERATURE); sum += pr[i]; }
  float r = (esp_random() / 4294967296.0f) * sum;
  for (int i = 0; i < cnt; i++) { r -= pr[i]; if (r <= 0.f) return idx[i]; }
  return idx[cnt - 1];
}

// ---- generation state machine ----------------------------------------------
static int  promptIdx = 0;
static int  pos = 0, step = 0, tok = 0;
static bool generating = false;
static int64_t t_start = 0, decode_us = 0;
static int decoded = 0;

static void startStory() {
  const Prompt &p = PROMPTS[promptIdx];
  promptIdx = (promptIdx + 1) % N_PROMPTS;
  clearStory();
  drawStatus("thinking...");
  Serial.print("\n>>> ");
  pos = 0; step = 0; decoded = 0; decode_us = 0;
  for (int i = 0; i < p.n; i++) {
    tok = p.ids[i];
    emit(tok);
    llm_forward(&model, tok, pos++, &s);
  }
  llm_profile_reset(&s);
  t_start = esp_timer_get_time();
  generating = true;
}

static void finishStory() {
  generating = false;
  int64_t total_us = esp_timer_get_time() - t_start;
  float toks = decoded * 1e6f / total_us;
  Serial.printf("\n\n--- %d tokens in %.2f s, %.2f tok/s (%.1f ms/token) ---\n",
                decoded, total_us / 1e6, toks, decode_us / 1000.0 / decoded);
  char buf[48];
  snprintf(buf, sizeof buf, "%.1f tok/s  tap=new", toks);
  drawStatus(buf);
}

static void stepStory() {
  tok = sample(s.logits, model.out_vocab);
  emit(tok);
  int64_t d0 = esp_timer_get_time();
  llm_forward(&model, tok, pos++, &s);
  decode_us += esp_timer_get_time() - d0;
  decoded++;
  step++;
  if ((step & 7) == 0) {
    char buf[48];
    snprintf(buf, sizeof buf, "%.1f tok/s  tap=new", decoded * 1e6f / (esp_timer_get_time() - t_start));
    drawStatus(buf);
  }
  if (step >= N_GENERATE || pos >= model.c.seq_len) finishStory();
}

// ---- setup -----------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  screenW = M5.Display.width();
  screenH = M5.Display.height();
  M5.Display.fillScreen(TFT_BLACK);
  drawStatus("loading model...");
  initStoryCanvas();
  clearStory();
  Serial.println("\n=== CoreS3 TinyStories LM ===");

  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "model");
  if (!part) fatal("model partition not found (flash with: pio run -t upload_model)");
  const void *base;
  esp_partition_mmap_handle_t h;
  if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &base, &h) != ESP_OK)
    fatal("mmap failed");
  if (llm_load((const uint8_t *)base, &model))
    fatal("bad model magic - run: pio run -t upload_model");
  Cfg *c = &model.c;
  Serial.printf("model: Vin=%d Vout=%d D=%d L=%d H=%d F=%d P=%d (mapped %.1f MB)\n",
                c->vocab, model.out_vocab, c->dim, c->n_layers, c->n_heads,
                c->ffn, c->ple_dim, part->size / 1e6);
  if (VOCAB_N != model.out_vocab) fatal("tokenizer/model mismatch");

  alloc_scratch();
  copy_norms_to_sram();
  int want = llm_core_stage_count(&model);
  int staged = llm_stage_core_int8_alloc(&model, ps);
  if (staged != want) fatal("staging core tensors failed");
  {
    void *b = ps_or_die(llm_stage_int8_bytes(&model.out_head), "staged head");
    llm_stage_int8(&model.out_head, b);
    ++staged;
  }
  Serial.printf("weights-> PSRAM %d tensors int8, %.2f MB; SRAM hot set %u B\n",
                staged, psram_used / 1048576.0, (unsigned)(sram_used + STATIC_SRAM_BYTES));

  main_h = xTaskGetCurrentTaskHandle();
  if (xTaskCreatePinnedToCore(worker_main, "mv", 4096, NULL, 2, &worker_h, 0) == pdPASS) {
    model.layer_matvec = matvec_par;
    model.head_matvec  = matvec_par;
  } else {
    Serial.println("dual-core worker failed; running single core");
  }
  Serial.printf("free: sram %.0f KB | psram %.2f MB\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024.0,
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576.0);
  modelReady = true;
  startStory();
}

void loop() {
  M5.update();
  bool tapped = M5.Touch.getDetail().wasPressed();
  bool key = Serial.available() && (Serial.read() == 'n');
  if (tapped || key) { startStory(); return; }
  if (generating) stepStory();
  else delay(20);
}
