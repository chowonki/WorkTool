// WorkTool 규칙 엔진 연결부: quickjs-ng를 문자열만 주고받는 작은 C 함수로 감싸요.
// Dart(FFI)에서 부르며, 파일·네트워크·OS 기능은 붙이지 않아요 (quickjs-libc 없이 빌드).
#include <stdlib.h>
#include <string.h>
#include "quickjs.h"

#if defined(_WIN32)
#  define WT_EXPORT __declspec(dllexport)
#else
#  define WT_EXPORT __attribute__((visibility("default")))
#endif

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
