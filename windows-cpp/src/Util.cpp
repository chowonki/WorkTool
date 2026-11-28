#include "pch.h"
#include "Util.h"
#include <charconv>
#include <objbase.h>

using namespace winrt;
using namespace winrt::Windows::Data::Json;

namespace wt
{
    std::string ToUtf8(std::wstring_view text)
    {
        if (text.empty()) return {};
        auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring ToWide(std::string_view text)
    {
        if (text.empty()) return {};
        auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
        return result;
    }

    std::wstring Trim(std::wstring_view text)
    {
        size_t first = 0, last = text.size();
        while (first < last && iswspace(text[first])) first++;
        while (last > first && iswspace(text[last - 1])) last--;
        return std::wstring(text.substr(first, last - first));
    }

    std::wstring NormalizeLines(std::wstring_view text)
    {
        std::wstring result;
        result.reserve(text.size());
        for (size_t i = 0; i < text.size(); i++)
        {
            if (text[i] == L'\r')
            {
                result += L'\n';
                if (i + 1 < text.size() && text[i + 1] == L'\n') i++;
            }
            else result += text[i];
        }
        return result;
    }

    std::vector<std::wstring> Split(std::wstring_view text, wchar_t separator)
    {
        std::vector<std::wstring> parts;
        size_t start = 0;
        for (;;)
        {
            auto at = text.find(separator, start);
            if (at == std::wstring_view::npos) { parts.emplace_back(text.substr(start)); return parts; }
            parts.emplace_back(text.substr(start, at - start));
            start = at + 1;
        }
    }

    std::wstring Join(std::vector<std::wstring> const& parts, std::wstring_view separator)
    {
        std::wstring result;
        for (size_t i = 0; i < parts.size(); i++)
        {
            if (i > 0) result += separator;
            result += parts[i];
        }
        return result;
    }

    std::wstring Replace(std::wstring text, std::wstring_view from, std::wstring_view to)
    {
        if (from.empty()) return text;
        for (size_t at = 0; (at = text.find(from, at)) != std::wstring::npos; at += to.size()) text.replace(at, from.size(), to);
        return text;
    }

    static std::wstring Lower(std::wstring_view text)
    {
        std::wstring result(text);
        if (!result.empty()) CharLowerBuffW(result.data(), static_cast<DWORD>(result.size()));
        return result;
    }

    bool ContainsIgnoreCase(std::wstring_view text, std::wstring_view query)
    {
        return query.empty() || Lower(text).find(Lower(query)) != std::wstring::npos;
    }

    int CompareText(std::wstring const& left, std::wstring const& right)
    {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, 0, left.c_str(), static_cast<int>(left.size()),
            right.c_str(), static_cast<int>(right.size()), nullptr, nullptr, 0) - CSTR_EQUAL;
    }

    std::vector<std::wstring> DistinctSorted(std::vector<std::wstring> values)
    {
        std::erase_if(values, [](auto const& value) { return value.empty(); });
        std::sort(values.begin(), values.end(), [](auto const& a, auto const& b) { return CompareText(a, b) < 0; });
        values.erase(std::unique(values.begin(), values.end()), values.end());
        return values;
    }

    std::wstring N0(double value)
    {
        auto rounded = std::llround(value);
        auto digits = std::to_wstring(rounded < 0 ? -rounded : rounded);
        std::wstring result;
        for (size_t i = 0; i < digits.size(); i++)
        {
            if (i > 0 && (digits.size() - i) % 3 == 0) result += L',';
            result += digits[i];
        }
        return rounded < 0 ? L"-" + result : result;
    }

    std::optional<double> ParseNumber(std::wstring_view text)
    {
        auto trimmed = Trim(text);
        std::string plain;
        for (auto c : trimmed)
        {
            if (c == L',') continue;
            if (c > 127) return std::nullopt;
            plain += static_cast<char>(c);
        }
        if (plain.empty()) return std::nullopt;
        auto begin = plain.data(), end = plain.data() + plain.size();
        if (*begin == '+') begin++;
        double value = 0;
        auto [stop, error] = std::from_chars(begin, end, value);
        if (error != std::errc() || stop != end || !std::isfinite(value)) return std::nullopt;
        return value;
    }

    std::wstring NumberText(double value)
    {
        if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 1e15) return std::to_wstring(static_cast<long long>(value));
        char buffer[40];
        auto [end, error] = std::to_chars(buffer, buffer + sizeof buffer, value);
        return error == std::errc() ? ToWide(std::string_view(buffer, end - buffer)) : L"0";
    }

    std::wstring Now(wchar_t const* format)
    {
        auto now = time(nullptr);
        tm local{};
        localtime_s(&local, &now);
        wchar_t buffer[64];
        return std::wstring(buffer, wcsftime(buffer, 64, format, &local));
    }

    std::wstring NewId()
    {
        GUID id{};
        CoCreateGuid(&id);
        wchar_t buffer[40];
        swprintf_s(buffer, L"%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x", id.Data1, id.Data2, id.Data3,
            id.Data4[0], id.Data4[1], id.Data4[2], id.Data4[3], id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]);
        return buffer;
    }

    std::filesystem::path AppDirectory()
    {
        wchar_t buffer[MAX_PATH * 4];
        auto size = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        return std::filesystem::path(std::wstring(buffer, size)).parent_path();
    }

    std::optional<std::string> ReadFile(std::filesystem::path const& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return std::nullopt;
        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    }

    bool WriteFile(std::filesystem::path const& path, std::string_view bytes)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        auto temporary = path;
        temporary += L"." + NewId() + L".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) return false;
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!file) { file.close(); std::filesystem::remove(temporary, error); return false; }
        }
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        std::filesystem::remove(temporary, error);
        return false;
    }

    // ── JSON ──
    std::wstring Str(JVal const& value)
    {
        if (!value) return {};
        switch (value.ValueType())
        {
        case JsonValueType::String: return std::wstring(value.GetString());
        case JsonValueType::Number: return NumberText(value.GetNumber());
        case JsonValueType::Boolean: return value.GetBoolean() ? L"true" : L"false";
        case JsonValueType::Null: return {};
        default: return std::wstring(value.Stringify());
        }
    }

    static JVal Find(JObj const& row, std::wstring_view key)
    {
        return row ? row.TryLookup(hstring(key)) : nullptr;
    }

    std::wstring Str(JObj const& row, std::wstring_view key) { return Str(Find(row, key)); }

    bool Flag(JObj const& row, std::wstring_view key)
    {
        auto value = Find(row, key);
        return value && value.ValueType() == JsonValueType::Boolean && value.GetBoolean();
    }

    JArr Arr(JObj const& row, std::wstring_view key)
    {
        auto value = Find(row, key);
        return value && value.ValueType() == JsonValueType::Array ? value.GetArray() : JArr();
    }

    std::vector<JObj> Objects(JArr const& array)
    {
        std::vector<JObj> rows;
        if (!array) return rows;
        for (auto const& item : array)
            if (item.ValueType() == JsonValueType::Object) rows.push_back(item.GetObject());
        return rows;
    }

    std::vector<std::wstring> Strings(JVal const& value)
    {
        std::vector<std::wstring> items;
        if (!value || value.ValueType() != JsonValueType::Array) return items;
        for (auto const& item : value.GetArray())
            if (item.ValueType() == JsonValueType::String) items.emplace_back(item.GetString());
        return items;
    }

    void Put(JObj const& row, std::wstring_view key, std::wstring_view value)
    {
        row.Insert(hstring(key), JsonValue::CreateStringValue(hstring(value)));
    }

    JObj Clone(JObj const& row) { return JObj::Parse(row.Stringify()); }

    std::string Dump(JObj const& row) { return ToUtf8(row.Stringify()); }

    std::string Dump(std::vector<JObj> const& rows)
    {
        std::string result = "[";
        for (size_t i = 0; i < rows.size(); i++)
        {
            if (i > 0) result += ',';
            result += Dump(rows[i]);
        }
        return result + "]";
    }

    std::string JsString(std::wstring_view text)
    {
        std::wstring result = L"\"";
        for (size_t i = 0; i < text.size(); i++)
        {
            auto c = text[i];
            auto high = c >= 0xD800 && c < 0xDC00, low = c >= 0xDC00 && c <= 0xDFFF;
            if (high && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
            {
                result += c; result += text[++i];
                continue;
            }
            switch (c)
            {
            case L'"': result += L"\\\""; break;
            case L'\\': result += L"\\\\"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            default:
                // 제어문자, 줄 구분자, 짝 없는 대리 문자는 \u로 적어요 (UTF-8로 바꿀 때 깨지지 않게).
                if (c < 0x20 || c == 0x2028 || c == 0x2029 || high || low)
                {
                    wchar_t buffer[8];
                    swprintf_s(buffer, L"\\u%04x", static_cast<unsigned>(c));
                    result += buffer;
                }
                else result += c;
            }
        }
        return ToUtf8(result + L"\"");
    }

    JObj ParseObject(std::string const& json) { return JObj::Parse(hstring(ToWide(json))); }
    JArr ParseArray(std::string const& json) { return JArr::Parse(hstring(ToWide(json))); }
}
