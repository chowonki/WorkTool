#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace worktool
{
    namespace fs = std::filesystem;
    using namespace winrt::Windows::Data::Json;
    using Bytes = std::vector<uint8_t>;

    fs::path AppDirectory();
    Bytes ReadBytes(fs::path const& path, size_t limit = 40 * 1024 * 1024);
    void WriteBytes(fs::path const& path, std::span<uint8_t const> bytes);
    std::wstring Text(IJsonValue const& value);
    std::wstring Text(JsonObject const& value, std::wstring_view key);
    double Number(JsonObject const& value, std::wstring_view key, double fallback = 0);
    std::wstring Stamp(wchar_t const* format);
    bool Encrypted(std::span<uint8_t const> bytes);

    class Rules
    {
    public:
        explicit Rules(bool withExcel = false);
        ~Rules();
        Rules(Rules const&) = delete;
        Rules& operator=(Rules const&) = delete;

        void Set(std::string_view name, IJsonValue const& value);
        void Set(std::string_view name, std::wstring_view value);
        IJsonValue Json(std::string_view expression);
        JsonObject Vendor(std::span<uint8_t const> data, std::wstring_view name, std::wstring_view password);
        Bytes DispatchExcel(JsonArray const& rows);
        Bytes TransferExcel(JsonArray const& rows);

    private:
        struct Engine;
        std::unique_ptr<Engine> engine_;
        std::string Eval(std::string_view script, char const* name = "worktool");
        void Wait(std::string_view expression);
        Bytes Binary(std::string_view expression);
    };

    class LocalState
    {
    public:
        LocalState();
        JsonObject Today(std::wstring_view key) const;
        std::wstring Theme() const;
        void Theme(std::wstring_view theme);
        void SaveToday(std::wstring_view key, JsonObject const& value);
        void Forget(std::wstring_view key);
    private:
        JsonObject Read() const;
        void Write(JsonObject const& data) const;
        fs::path file_;
    };
}
