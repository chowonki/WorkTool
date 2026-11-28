#pragma once
// 웹과 같은 규칙 JavaScript를 실행하는 엔진 (quickjs-ng).
// 브라우저·파일·네트워크·OS 기능은 주지 않아요. 저장소에 들어 있는 규칙만 실행해요.

struct JSRuntime;
struct JSContext;

namespace wt
{
    class JsEngine
    {
    public:
        JsEngine(size_t memoryLimit, int timeoutSeconds, size_t stackSize);
        ~JsEngine();
        JsEngine(JsEngine const&) = delete;
        JsEngine& operator=(JsEngine const&) = delete;

        /// 스크립트를 실행하고 String(결과)를 UTF-8로 돌려줘요. 오류면 AppError.
        std::string Eval(std::string_view code, char const* name = "eval");
        /// 식의 결과를 JSON 글자로 돌려줘요 (undefined는 "null").
        std::string Json(std::string_view expression);
        /// Promise를 돌려주는 식을 끝까지 기다려요.
        void Wait(std::string_view expression);
        /// 다음 __takeBytes() 호출이 받을 바이트를 올려 둬요 (엑셀 파일 전달).
        void StageBytes(std::vector<uint8_t> bytes) { staged = std::move(bytes); }

        std::vector<uint8_t> staged;
        std::chrono::steady_clock::time_point deadline{};

    private:
        JSRuntime* runtime{};
        JSContext* context{};
        int timeout;
        void Pending();
        std::wstring ExceptionText();
    };
}
