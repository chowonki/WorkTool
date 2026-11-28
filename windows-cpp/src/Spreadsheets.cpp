#include "pch.h"
#include "Spreadsheets.h"
#include <shlobj_core.h>

using namespace winrt;
using namespace winrt::Windows::Data::Json;

namespace wt::Spreadsheets
{
    // ── 압축하지 않는(stored) ZIP. 외부 라이브러리 없이 은행 양식 정도의 작은 파일을 만들어요. ──
    static uint32_t Crc32(std::string_view data)
    {
        static auto const table = []
        {
            std::array<uint32_t, 256> values{};
            for (uint32_t i = 0; i < 256; i++)
            {
                auto c = i;
                for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                values[i] = c;
            }
            return values;
        }();
        uint32_t crc = 0xFFFFFFFFu;
        for (unsigned char byte : data) crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }

    static void Put16(std::string& out, uint16_t value) { out.append({ static_cast<char>(value), static_cast<char>(value >> 8) }); }
    static void Put32(std::string& out, uint32_t value) { Put16(out, static_cast<uint16_t>(value)); Put16(out, static_cast<uint16_t>(value >> 16)); }

    static std::string Zip(std::vector<std::pair<std::string, std::string>> const& entries)
    {
        std::string out, directory;
        for (auto const& [name, data] : entries)
        {
            auto offset = static_cast<uint32_t>(out.size()), crc = Crc32(data), size = static_cast<uint32_t>(data.size());
            auto header = [&](std::string& target)
            {
                Put16(target, 20); Put16(target, 0x0800); Put16(target, 0);       // 버전, UTF-8 이름, 압축 없음
                Put16(target, 0); Put16(target, 0x21);                            // 시각 00:00, 날짜 1980-01-01
                Put32(target, crc); Put32(target, size); Put32(target, size);
                Put16(target, static_cast<uint16_t>(name.size())); Put16(target, 0);
            };
            Put32(out, 0x04034b50); header(out); out += name; out += data;
            Put32(directory, 0x02014b50); Put16(directory, 20); header(directory);
            Put16(directory, 0); Put16(directory, 0); Put16(directory, 0); Put32(directory, 0); Put32(directory, offset);
            directory += name;
        }
        auto start = static_cast<uint32_t>(out.size());
        out += directory;
        Put32(out, 0x06054b50); Put16(out, 0); Put16(out, 0);
        Put16(out, static_cast<uint16_t>(entries.size())); Put16(out, static_cast<uint16_t>(entries.size()));
        Put32(out, static_cast<uint32_t>(directory.size())); Put32(out, start); Put16(out, 0);
        return out;
    }

    // 엑셀 파일을 깨뜨리는 제어문자를 지우고 특수문자를 안전하게 바꿔 적어요.
    static std::string Xml(std::wstring_view text)
    {
        std::wstring safe;
        for (size_t i = 0; i < text.size(); i++)
        {
            auto c = text[i];
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) { safe += c; safe += text[++i]; continue; }
            if (c >= 0xD800 && c <= 0xDFFF) continue;
            if (!(c == 9 || c == 10 || c == 13 || (c >= 32 && c != 0xFFFE && c != 0xFFFF))) continue;
            switch (c)
            {
            case L'&': safe += L"&amp;"; break;
            case L'<': safe += L"&lt;"; break;
            case L'>': safe += L"&gt;"; break;
            case L'"': safe += L"&quot;"; break;
            case L'\r': safe += L"&#13;"; break;
            default: safe += c;
            }
        }
        return ToUtf8(safe);
    }

    static std::string Column(uint32_t index)
    {
        std::string name;
        for (auto i = index + 1; i > 0; i = (i - 1) / 26) name.insert(name.begin(), static_cast<char>('A' + (i - 1) % 26));
        return name;
    }

    void Write(std::filesystem::path const& path, JArr const& values, std::wstring_view sheetName)
    {
        std::string const head = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
        std::string const main = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
        std::string sheet = head + "<worksheet xmlns=\"" + main + "\"><sheetData>";
        for (uint32_t i = 0; i < values.Size(); i++)
        {
            auto row = std::to_string(i + 1);
            sheet += "<row r=\"" + row + "\">";
            auto items = values.GetArrayAt(i);
            for (uint32_t j = 0; j < items.Size(); j++)
            {
                auto item = items.GetAt(j);
                auto cell = Column(j) + row;
                if (i > 0 && item.ValueType() == JsonValueType::Number)
                    sheet += "<c r=\"" + cell + "\"><v>" + ToUtf8(NumberText(item.GetNumber())) + "</v></c>";
                else
                    sheet += "<c r=\"" + cell + "\" t=\"inlineStr\"><is><t xml:space=\"preserve\">" + Xml(Str(item)) + "</t></is></c>";
            }
            sheet += "</row>";
        }
        sheet += "</sheetData></worksheet>";
        auto zip = Zip({
            { "[Content_Types].xml", head + "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/></Types>" },
            { "_rels/.rels", head + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>" },
            { "xl/_rels/workbook.xml.rels", head + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/></Relationships>" },
            { "xl/workbook.xml", head + "<workbook xmlns=\"" + main + "\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                "<sheets><sheet name=\"" + Xml(sheetName) + "\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>" },
            { "xl/worksheets/sheet1.xml", sheet } });
        if (!WriteFile(path, zip)) throw AppError{ L"파일을 저장하지 못했어요. 저장 위치와 권한을 확인해 주세요." };
    }

    JArr TransferRows(std::vector<JObj> const& rows)
    {
        JArr values;
        auto text = [](std::wstring_view value) { return JsonValue::CreateStringValue(hstring(value)); };
        JArr head;
        for (auto title : { L"입금은행", L"입금계좌번호", L"입금액(원)", L"출금통장표시", L"입금통장표시", L"CMS코드" }) head.Append(text(title));
        values.Append(head);
        for (auto const& row : rows)
        {
            auto amount = Str(row, L"amount");
            if (amount.empty() || amount.find_first_not_of(L"0123456789") != std::wstring::npos) throw AppError{ L"금액을 확인해 주세요." };
            JArr line;
            line.Append(text(Str(row, L"bank"))); line.Append(text(Str(row, L"account")));
            line.Append(JsonValue::CreateNumberValue(std::stod(amount)));
            line.Append(text(Str(row, L"outMemo"))); line.Append(text(Str(row, L"inMemo"))); line.Append(text(Str(row, L"cms")));
            values.Append(line);
        }
        return values;
    }
}

namespace wt::LocalState
{
    std::filesystem::path DataFile(std::wstring_view name)
    {
        auto app = AppDirectory();
        std::error_code error;
        if (std::filesystem::exists(app / L"portable.mode", error)) return (app.parent_path() / L"Data" / name).lexically_normal();
        PWSTR folder{};
        std::filesystem::path base = SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)) ? folder : app.c_str();
        CoTaskMemFree(folder);
        return base / L"WorkTool" / name;
    }

    static std::wstring Today() { return Now(L"%Y-%m-%d"); }

    JObj ReadToday(std::wstring_view name)
    {
        try
        {
            auto path = DataFile(name);
            if (auto text = ReadFile(path))
            {
                JObj saved{ nullptr };
                if (JObj::TryParse(hstring(ToWide(*text)), saved) && Str(saved, L"date") == Today()) return saved;
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        }
        catch (...) {}
        JObj fresh;
        Put(fresh, L"date", Today());
        return fresh;
    }

    void WriteToday(std::wstring_view name, JObj const& value)
    {
        try
        {
            Put(value, L"date", Today());
            WriteFile(DataFile(name), ToUtf8(value.Stringify()));
        }
        catch (...) {}
    }

    void Delete(std::wstring_view name)
    {
        std::error_code error;
        std::filesystem::remove(DataFile(name), error);
    }

    std::optional<std::vector<double>> ReadNumbers(std::wstring_view name)
    {
        try
        {
            auto text = ReadFile(DataFile(name));
            JArr saved{ nullptr };
            if (!text || !JArr::TryParse(hstring(ToWide(*text)), saved)) return std::nullopt;
            std::vector<double> values;
            for (auto const& item : saved)
            {
                if (item.ValueType() != JsonValueType::Number) return std::nullopt;
                values.push_back(item.GetNumber());
            }
            return values;
        }
        catch (...) { return std::nullopt; }
    }

    bool WriteNumbers(std::wstring_view name, std::vector<double> const& values)
    {
        std::wstring text = L"[";
        for (size_t i = 0; i < values.size(); i++) text += (i > 0 ? L"," : L"") + NumberText(values[i]);
        return WriteFile(DataFile(name), ToUtf8(text + L"]"));
    }
}
