// WorkTool 규칙 엔진 연결부: quickjs-ng를 문자열만 주고받는 작은 C 함수로 감싸요.
// Dart(FFI)에서 부르며, 파일·네트워크·OS 기능은 붙이지 않아요 (quickjs-libc 없이 빌드).
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "quickjs.h"

#if defined(_WIN32)
#  define WT_EXPORT __declspec(dllexport)
#else
#  define WT_EXPORT __attribute__((visibility("default")))
#endif


// ── SHA-512 (FIPS 180-4). 암호 엑셀(Agile 암호화)의 10만 번 해시 반복만 여기서 빠르게 해요.
typedef struct { uint64_t h[8]; uint8_t buf[128]; size_t len; uint64_t total; } Sha512;
static const uint64_t K512[80] = {
  0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,
  0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
  0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,
  0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
  0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,
  0x06ca6351e003826fULL,0x142929670a0e6e70ULL,0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
  0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,
  0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
  0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,
  0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
  0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,0xca273eceea26619cULL,0xd186b8c721c0c207ULL,
  0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
  0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,
  0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL};
#define ROR64(x,n) (((x) >> (n)) | ((x) << (64 - (n))))
static void sha512_block(Sha512 *s, const uint8_t *p) {
  uint64_t w[80], a, b, c, d, e, f, g, h;
  for (int i = 0; i < 16; i++) {
    w[i] = 0;
    for (int j = 0; j < 8; j++) w[i] = (w[i] << 8) | p[i * 8 + j];
  }
  for (int i = 16; i < 80; i++) {
    uint64_t s0 = ROR64(w[i-15], 1) ^ ROR64(w[i-15], 8) ^ (w[i-15] >> 7);
    uint64_t s1 = ROR64(w[i-2], 19) ^ ROR64(w[i-2], 61) ^ (w[i-2] >> 6);
    w[i] = w[i-16] + s0 + w[i-7] + s1;
  }
  a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
  for (int i = 0; i < 80; i++) {
    uint64_t S1 = ROR64(e, 14) ^ ROR64(e, 18) ^ ROR64(e, 41);
    uint64_t ch = (e & f) ^ (~e & g);
    uint64_t t1 = h + S1 + ch + K512[i] + w[i];
    uint64_t S0 = ROR64(a, 28) ^ ROR64(a, 34) ^ ROR64(a, 39);
    uint64_t mj = (a & b) ^ (a & c) ^ (b & c);
    uint64_t t2 = S0 + mj;
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha512_init(Sha512 *s) {
  static const uint64_t iv[8] = {0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,
                                 0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL};
  memcpy(s->h, iv, sizeof iv); s->len = 0; s->total = 0;
}
static void sha512_update(Sha512 *s, const uint8_t *p, size_t n) {
  s->total += n;
  while (n > 0) {
    size_t take = 128 - s->len < n ? 128 - s->len : n;
    memcpy(s->buf + s->len, p, take); s->len += take; p += take; n -= take;
    if (s->len == 128) { sha512_block(s, s->buf); s->len = 0; }
  }
}
static void sha512_final(Sha512 *s, uint8_t out[64]) {
  uint64_t bits = s->total * 8;
  uint8_t pad = 0x80, zero = 0, lenb[16] = {0};
  sha512_update(s, &pad, 1);
  while (s->len != 112) sha512_update(s, &zero, 1);
  for (int i = 0; i < 8; i++) lenb[15 - i] = (uint8_t)(bits >> (8 * i));
  sha512_update(s, lenb, 16);
  for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) out[i * 8 + j] = (uint8_t)(s->h[i] >> (56 - 8 * j));
}


// ── SHA-1 (FIPS 180-4). 일부 프로그램이 만든 암호 엑셀은 SHA-1을 써요.
typedef struct { uint32_t h[5]; uint8_t buf[64]; size_t len; uint64_t total; } Sha1;
#define ROL32(x,n) (((x) << (n)) | ((x) >> (32 - (n))))
static void sha1_block(Sha1 *s, const uint8_t *p) {
  uint32_t w[80], a, b, c, d, e;
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i*4] << 24 | (uint32_t)p[i*4+1] << 16 | (uint32_t)p[i*4+2] << 8 | p[i*4+3];
  for (int i = 16; i < 80; i++) w[i] = ROL32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
  a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
    else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
    else { f = b ^ c ^ d; k = 0xCA62C1D6; }
    uint32_t t = ROL32(a, 5) + f + e + k + w[i];
    e = d; d = c; c = ROL32(b, 30); b = a; a = t;
  }
  s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
static void sha1_init(Sha1 *s) {
  s->h[0] = 0x67452301; s->h[1] = 0xEFCDAB89; s->h[2] = 0x98BADCFE; s->h[3] = 0x10325476; s->h[4] = 0xC3D2E1F0;
  s->len = 0; s->total = 0;
}
static void sha1_update(Sha1 *s, const uint8_t *p, size_t n) {
  s->total += n;
  while (n > 0) {
    size_t take = 64 - s->len < n ? 64 - s->len : n;
    memcpy(s->buf + s->len, p, take); s->len += take; p += take; n -= take;
    if (s->len == 64) { sha1_block(s, s->buf); s->len = 0; }
  }
}
static void sha1_final(Sha1 *s, uint8_t out[20]) {
  uint64_t bits = s->total * 8;
  uint8_t pad = 0x80, zero = 0, lenb[8];
  sha1_update(s, &pad, 1);
  while (s->len != 56) sha1_update(s, &zero, 1);
  for (int i = 0; i < 8; i++) lenb[7 - i] = (uint8_t)(bits >> (8 * i));
  sha1_update(s, lenb, 8);
  for (int i = 0; i < 5; i++) for (int j = 0; j < 4; j++) out[i * 4 + j] = (uint8_t)(s->h[i] >> (24 - 8 * j));
}

// __nativeSpin(hash, count, sha1?): H = SHA(LE32(i) + H) 를 count번 반복해요. 세 번째 값이 true면 SHA-1, 아니면 SHA-512.
static JSValue js_native_spin(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  size_t n = 0;
  uint8_t *p = argc > 0 ? JS_GetUint8Array(ctx, &n, argv[0]) : NULL;
  int32_t count = 0;
  if (!p || n == 0 || n > 64 || argc < 2 || JS_ToInt32(ctx, &count, argv[1]) || count < 0 || count > 10000000)
    return JS_ThrowTypeError(ctx, "__nativeSpin: bad arguments");
  int use_sha1 = argc > 2 && JS_ToBool(ctx, argv[2]) > 0;
  uint8_t h[64];
  size_t hl = n;
  memcpy(h, p, n);
  for (int32_t i = 0; i < count; i++) {
    uint8_t it[4] = {(uint8_t)i, (uint8_t)(i >> 8), (uint8_t)(i >> 16), (uint8_t)(i >> 24)};
    if (use_sha1) {
      Sha1 s; sha1_init(&s); sha1_update(&s, it, 4); sha1_update(&s, h, hl); sha1_final(&s, h); hl = 20;
    } else {
      Sha512 s; sha512_init(&s); sha512_update(&s, it, 4); sha512_update(&s, h, hl); sha512_final(&s, h); hl = 64;
    }
  }
  return JS_NewUint8ArrayCopy(ctx, h, hl);
}

typedef struct WT {
  JSRuntime *rt;
  JSContext *ctx;
} WT;

static char *dup_str(const char *s, size_t n) {
  char *out = (char *)malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, s, n);
  out[n] = 0;
  return out;
}

WT_EXPORT WT *wt_new(size_t stack_size) {
  WT *w = (WT *)calloc(1, sizeof(WT));
  if (!w) return NULL;
  w->rt = JS_NewRuntime();
  if (!w->rt) { free(w); return NULL; }
  JS_SetMaxStackSize(w->rt, stack_size);
  w->ctx = JS_NewContext(w->rt);
  if (!w->ctx) { JS_FreeRuntime(w->rt); free(w); return NULL; }
  JSValue global = JS_GetGlobalObject(w->ctx);
  JS_SetPropertyStr(w->ctx, global, "__nativeSpin", JS_NewCFunction(w->ctx, js_native_spin, "__nativeSpin", 3));
  JS_FreeValue(w->ctx, global);
  return w;
}

// 예외를 "메시지\n스택" 글로 만들어요.
static char *exception_text(JSContext *ctx) {
  JSValue exc = JS_GetException(ctx);
  size_t n = 0;
  const char *msg = JS_ToCStringLen(ctx, &n, exc);
  char *out;
  JSValue stack = JS_UNDEFINED;
  if (JS_IsObject(exc)) stack = JS_GetPropertyStr(ctx, exc, "stack");
  if (msg && JS_IsString(stack)) {
    size_t sn = 0;
    const char *st = JS_ToCStringLen(ctx, &sn, stack);
    out = (char *)malloc(n + sn + 2);
    if (out) {
      memcpy(out, msg, n);
      out[n] = '\n';
      if (st) memcpy(out + n + 1, st, sn);
      out[n + 1 + (st ? sn : 0)] = 0;
    }
    if (st) JS_FreeCString(ctx, st);
  } else {
    out = msg ? dup_str(msg, n) : dup_str("알 수 없는 오류", strlen("알 수 없는 오류"));
  }
  if (msg) JS_FreeCString(ctx, msg);
  JS_FreeValue(ctx, stack);
  JS_FreeValue(ctx, exc);
  return out;
}

// 스크립트를 실행하고 결과를 String(값)으로 돌려줘요. 오류면 *is_error=1 이고 오류 글을 돌려줘요.
// 돌려받은 글은 wt_free_str로 풀어 주세요.
WT_EXPORT char *wt_eval(WT *w, const char *code, size_t len, const char *name, int *is_error) {
  JS_UpdateStackTop(w->rt);
  *is_error = 0;
  JSValue v = JS_Eval(w->ctx, code, len, name, JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(v)) {
    *is_error = 1;
    return exception_text(w->ctx);
  }
  size_t n = 0;
  const char *s = JS_ToCStringLen(w->ctx, &n, v);
  char *out;
  if (!s) {
    *is_error = 1;
    out = exception_text(w->ctx);
  } else {
    out = dup_str(s, n);
    JS_FreeCString(w->ctx, s);
  }
  JS_FreeValue(w->ctx, v);
  return out;
}

// 쌓인 Promise 작업을 모두 실행해요. 실행한 개수, 오류면 -1.
WT_EXPORT int wt_pending(WT *w) {
  JS_UpdateStackTop(w->rt);
  int count = 0;
  for (;;) {
    JSContext *c = NULL;
    int r = JS_ExecutePendingJob(w->rt, &c);
    if (r == 0) return count;
    if (r < 0) {
      if (c) { JSValue e = JS_GetException(c); JS_FreeValue(c, e); }
      return -1;
    }
    count++;
  }
}

WT_EXPORT void wt_free_str(char *s) { free(s); }

WT_EXPORT void wt_free(WT *w) {
  if (!w) return;
  JS_FreeContext(w->ctx);
  JS_FreeRuntime(w->rt);
  free(w);
}
