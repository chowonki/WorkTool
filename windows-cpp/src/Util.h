#pragma once
// 문자열·숫자·파일·JSON 도우미

namespace wt
{
    /// 사용자에게 그대로 보여 줄 오류
    /// (hresult_error에서 파생해서 비동기 작업 경계를 넘어도 글자가 그대로 전달돼요. 글자는 message()로 읽어요.)
    struct AppError : winrt::hresult_error
    {
        AppError(std::wstring_view message) : winrt::hresult_error(E_FAIL, winrt::hstring(message)) {}
    };

    std::string ToUtf8(std::wstring_view text);
    std::wstring ToWide(std::string_view text);

    std::wstring Trim(std::wstring_view text);
    std::wstring NormalizeLines(std::wstring_view text);               // CRLF·CR → LF
    std::vector<std::wstring> Split(std::wstring_view text, wchar_t separator);
    std::wstring Join(std::vector<std::wstring> const& parts, std::wstring_view separator);
    std::wstring Replace(std::wstring text, std::wstring_view from, std::wstring_view to);
    bool ContainsIgnoreCase(std::wstring_view text, std::wstring_view query);
    int CompareText(std::wstring const& left, std::wstring const& right);  // 사용자 로캘 순서
    std::vector<std::wstring> DistinctSorted(std::vector<std::wstring> values); // 빈 값 빼고 정렬

    std::wstring N0(double value);                                     // 1,234
    std::optional<double> ParseNumber(std::wstring_view text);
    std::wstring NumberText(double value);                             // JSON 숫자 글자
    std::wstring Now(wchar_t const* format);                           // wcsftime 형식
    std::wstring NewId();                                              // 32자 16진수

    std::filesystem::path AppDirectory();
    std::optional<std::string> ReadFile(std::filesystem::path const& path);
    bool WriteFile(std::filesystem::path const& path, std::string_view bytes); // 임시 파일에 쓴 뒤 바꿔요

    // ── JSON (Windows.Data.Json: 참조형이라 같은 줄을 여러 곳에서 고쳐 쓸 수 있어요) ──
    using JObj = winrt::Windows::Data::Json::JsonObject;
    using JArr = winrt::Windows::Data::Json::JsonArray;
    using JVal = winrt::Windows::Data::Json::IJsonValue;

    std::wstring Str(JVal const& value);
    std::wstring Str(JObj const& row, std::wstring_view key);          // 없거나 null이면 ""
    bool Flag(JObj const& row, std::wstring_view key);
    JArr Arr(JObj const& row, std::wstring_view key);                  // 없으면 빈 배열
    std::vector<JObj> Objects(JArr const& array);
    std::vector<std::wstring> Strings(JVal const& value);              // 글자 항목만
    void Put(JObj const& row, std::wstring_view key, std::wstring_view value);
    JObj Clone(JObj const& row);
    std::string Dump(std::vector<JObj> const& rows);                   // UTF-8 JSON 배열
    std::string Dump(JObj const& row);
    std::string JsString(std::wstring_view text);                      // UTF-8 JSON 글자 리터럴
    JObj ParseObject(std::string const& json);
    JArr ParseArray(std::string const& json);
}
