#include "Rules.h"
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#undef GetObject
#include <fstream>
#include <ctime>

using namespace winrt;

namespace worktool
{
    fs::path AppDirectory()
    {
        std::wstring path(32768, L'\0');
        auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!size || size == path.size()) throw hresult_error(E_FAIL, L"앱 폴더를 열지 못했어요.");
        path.resize(size);
        return fs::path(path).parent_path();
    }

    Bytes ReadBytes(fs::path const& path, size_t limit)
    {
        auto size = fs::file_size(path);
        if (size > limit) throw hresult_error(E_INVALIDARG, L"파일은 40MB까지 첨부할 수 있어요.");
        std::ifstream file(path, std::ios::binary);
        Bytes bytes(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
            throw hresult_error(E_FAIL, L"파일을 읽지 못했어요.");
        return bytes;
    }

    void WriteBytes(fs::path const& path, std::span<uint8_t const> bytes)
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
        file.close();
        if (!file) throw hresult_error(E_FAIL, L"파일을 저장하지 못했어요.");
    }

    std::wstring Text(IJsonValue const& value)
    {
        switch (value.ValueType())
        {
        case JsonValueType::String: return std::wstring(value.GetString());
        case JsonValueType::Null: return {};
        default: return std::wstring(value.Stringify());
        }
    }

    std::wstring Text(JsonObject const& value, std::wstring_view key)
    {
        return Text(value.GetNamedValue(hstring(key), JsonValue::CreateNullValue()));
    }

    double Number(JsonObject const& value, std::wstring_view key, double fallback)
    {
        auto field = value.GetNamedValue(hstring(key), JsonValue::CreateNullValue());
        return field.ValueType() == JsonValueType::Number ? field.GetNumber() : fallback;
    }

    std::wstring Stamp(wchar_t const* format)
    {
        auto now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        wchar_t text[64]{};
        std::wcsftime(text, std::size(text), format, &local);
        return text;
    }

    bool Encrypted(std::span<uint8_t const> bytes)
    {
        constexpr uint8_t signature[]{0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};
        return bytes.size() >= std::size(signature) && std::equal(std::begin(signature), std::end(signature), bytes.begin());
    }

    static std::string Base64(std::span<uint8_t const> bytes)
    {
        if (bytes.empty()) return {};
        DWORD size{};
        constexpr DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
        check_bool(CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()), flags, nullptr, &size));
        std::string result(size, '\0');
        check_bool(CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()), flags, result.data(), &size));
        result.resize(size);
        return result;
    }

    struct Rules::Engine
    {
        using Create = void* (*)(size_t);
        using Evaluate = char* (*)(void*, char const*, size_t, char const*, int*);
        using Pending = int (*)(void*);
        using FreeString = void (*)(char*);
        using Free = void (*)(void*);

        struct Library
        {
            HMODULE handle = LoadLibraryExW((AppDirectory() / L"worktool_js.dll").c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            ~Library() { if (handle) FreeLibrary(handle); }
            template<typename T> T Function(char const* name)
            {
                if (!handle) throw hresult_error(E_FAIL, L"업무 규칙 엔진을 불러오지 못했어요.");
                auto symbol = GetProcAddress(handle, name);
                if (!symbol) throw hresult_error(E_FAIL, L"업무 규칙 엔진 버전이 맞지 않아요.");
                return reinterpret_cast<T>(symbol);
            }
        } library;
        Evaluate evaluate = library.Function<Evaluate>("wt_eval");
        Pending pending = library.Function<Pending>("wt_pending");
        FreeString freeString = library.Function<FreeString>("wt_free_str");
        Free free = library.Function<Free>("wt_free");
        void* handle = library.Function<Create>("wt_new")(640 * 1024);

        Engine() { if (!handle) throw hresult_error(E_OUTOFMEMORY); }
        ~Engine() { free(handle); }
    };

    static void ReplaceAll(std::string& text, std::string_view from, std::string_view to)
    {
        for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size())
            text.replace(at, from.size(), to);
    }

    Rules::Rules(bool withExcel) : engine_(std::make_unique<Engine>())
    {
        for (auto name : {"host.js", "dispatch.js", "transfer.js", "excel.js"})
        {
            bool excel = std::string_view(name) == "excel.js";
            if (excel && !withExcel) continue;
            auto bytes = ReadBytes(AppDirectory() / "Rules" / name);
            std::string source(bytes.begin(), bytes.end());
            if (excel)
            {
                ReplaceAll(source, "\r\n", "\n");
                ReplaceAll(source, R"(\\-)", R"(\\x2d)");
                constexpr std::string_view spin = "        for (let i = 0; i < spinCount; i++) {";
                auto start = source.find(spin);
                if (start != std::string::npos)
                    source.insert(start, R"(        if (typeof __nativeSpin === "function" && /^sha-?(1|512)$/i.test(String(hashAlgorithm))) key = Buffer.from(__nativeSpin(new Uint8Array(key), spinCount, /^sha-?1$/i.test(String(hashAlgorithm))));
        else
)");
            }
            Eval(source, name);
        }
    }

    Rules::~Rules() = default;

    std::string Rules::Eval(std::string_view script, char const* name)
    {
        // QuickJS accepts a byte count and also expects a trailing NUL.
        std::string source(script);
        int error{};
        std::unique_ptr<char, Engine::FreeString> value(
            engine_->evaluate(engine_->handle, source.c_str(), source.size(), name, &error), engine_->freeString);
        if (!value) throw hresult_error(E_OUTOFMEMORY);
        if (error)
        {
            std::string_view message(value.get());
            throw hresult_error(E_FAIL, to_hstring(message.substr(0, message.find('\n'))));
        }
        return value.get();
    }

    void Rules::Set(std::string_view name, IJsonValue const& value)
    {
        auto json = JsonValue::CreateStringValue(value.Stringify()).Stringify();
        Eval("globalThis." + std::string(name) + "=JSON.parse(" + to_string(json) + ")");
    }

    void Rules::Set(std::string_view name, std::wstring_view value)
    {
        Set(name, JsonValue::CreateStringValue(hstring(value)));
    }

    IJsonValue Rules::Json(std::string_view expression)
    {
        return JsonValue::Parse(to_hstring(Eval("JSON.stringify((" + std::string(expression) + ")) ?? 'null'")));
    }

    void Rules::Wait(std::string_view expression)
    {
        Eval("globalThis.__done=false;globalThis.__fail=null;globalThis.__result=undefined;"
            "Promise.resolve().then(()=>(" + std::string(expression) +
            ")).then(v=>{__result=v;__done=true},e=>{__fail=String(e&&e.message||e);__done=true});");
        for (unsigned idle = 0; idle < 50;)
        {
            if (engine_->pending(engine_->handle) < 0) break;
            auto state = Eval("__done?(__fail===null?'ok':'fail'):(__drain()?'busy':'idle')");
            if (state == "ok") return;
            if (state == "fail") throw hresult_error(E_FAIL, to_hstring(Eval("__fail")));
            idle = state == "busy" ? 0 : idle + 1;
        }
        throw hresult_error(E_FAIL, L"엑셀 작업이 끝나지 않았어요.");
    }

    Bytes Rules::Binary(std::string_view expression)
    {
        auto base64 = Eval("__b64enc(__bytesOf(" + std::string(expression) + "))");
        DWORD length{};
        check_bool(CryptStringToBinaryA(base64.c_str(), static_cast<DWORD>(base64.size()), CRYPT_STRING_BASE64,
            nullptr, &length, nullptr, nullptr));
        Bytes bytes(length);
        check_bool(CryptStringToBinaryA(base64.c_str(), static_cast<DWORD>(base64.size()), CRYPT_STRING_BASE64,
            bytes.data(), &length, nullptr, nullptr));
        return bytes;
    }

    JsonObject Rules::Vendor(std::span<uint8_t const> data, std::wstring_view name, std::wstring_view password)
    {
        Set("__name", name);
        Set("__pw", password);
        Eval("globalThis.__file=__b64dec(\"" + Base64(data) + "\")");
        Wait("(async()=>{const b=__pw?await DispatchExcel.decrypt(__file.buffer,__pw):__file.buffer;"
            "return DispatchCore.readSheets(await DispatchExcel.readAsync(b),__name);})()");
        return Json("__result").GetObject();
    }

    Bytes Rules::DispatchExcel(JsonArray const& rows)
    {
        Set("__rows", rows);
        Wait("DispatchExport.excelBlob(__rows)");
        return Binary("__result");
    }

    Bytes Rules::TransferExcel(JsonArray const& rows)
    {
        Set("__rows", rows);
        return Binary("Transfer.xlsx(__rows)");
    }

    LocalState::LocalState()
    {
        auto app = AppDirectory();
        if (fs::exists(app / "portable.mode"))
        {
            file_ = app.parent_path() / "Data" / "worktool-state.json";
            return;
        }
        PWSTR folder{};
        check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder));
        std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> memory(folder, CoTaskMemFree);
        file_ = fs::path(folder) / "WorkTool" / "worktool-state.json";
    }

    JsonObject LocalState::Read() const
    {
        if (!fs::exists(file_)) return JsonObject{};
        auto data = ReadBytes(file_, 4 * 1024 * 1024);
        JsonObject value;
        auto string = std::string_view(reinterpret_cast<char const*>(data.data()), data.size());
        if (!JsonObject::TryParse(to_hstring(string), value)) return JsonObject{};
        return value;
    }

    void LocalState::Write(JsonObject const& data) const
    {
        fs::create_directories(file_.parent_path());
        auto temp = file_;
        temp += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        auto value = to_string(data.Stringify());
        WriteBytes(temp, {reinterpret_cast<uint8_t const*>(value.data()), value.size()});
        if (!MoveFileExW(temp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            auto error = GetLastError();
            DeleteFileW(temp.c_str());
            throw hresult_error(HRESULT_FROM_WIN32(error), L"작업 기록을 저장하지 못했어요.");
        }
    }

    JsonObject LocalState::Today(std::wstring_view key) const
    {
        auto value = Read().GetNamedValue(hstring(key), JsonValue::CreateNullValue());
        if (value.ValueType() == JsonValueType::Object && Text(value.GetObject(), L"date") == Stamp(L"%Y-%m-%d"))
            return value.GetObject();
        return JsonObject{};
    }

    std::wstring LocalState::Theme() const { return Text(Read(), L"appearance"); }

    void LocalState::Theme(std::wstring_view theme)
    {
        auto state = Read();
        state.SetNamedValue(L"appearance", JsonValue::CreateStringValue(hstring(theme)));
        Write(state);
    }

    void LocalState::SaveToday(std::wstring_view key, JsonObject const& value)
    {
        auto state = Read();
        value.SetNamedValue(L"date", JsonValue::CreateStringValue(hstring(Stamp(L"%Y-%m-%d"))));
        state.SetNamedValue(hstring(key), value);
        Write(state);
    }

    void LocalState::Forget(std::wstring_view key)
    {
        auto state = Read();
        if (state.HasKey(hstring(key)))
        {
            state.Remove(hstring(key));
            Write(state);
        }
    }
}
