#include "Capture.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#undef GetObject
#include <array>
#include <format>

using namespace Gdiplus;
using namespace winrt;

namespace worktool
{
    namespace
    {
        constexpr int width = 1520;
        constexpr int padding = 36;
        constexpr int rowHeight = 47;
        constexpr std::array colWidths{232, 175, 254, 157, 135, 265, 230};
        constexpr std::array headings{L"탁송일정", L"차량번호", L"주유·충전비용 (원)", L"탁송비용 (원)", L"부가세 (원)", L"조회 상태", L"업체"};

        void Check(Status status)
        {
            if (status != Ok) throw hresult_error(E_FAIL, L"결과 표 이미지를 만들지 못했어요.");
        }

        struct GdiSession
        {
            ULONG_PTR token{};
            GdiSession() { GdiplusStartupInput input; Check(GdiplusStartup(&token, &input, nullptr)); }
            ~GdiSession() { GdiplusShutdown(token); }
        };

        std::wstring NumberText(JsonObject const& row, wchar_t const* key)
        {
            auto value = row.GetNamedValue(key, JsonValue::CreateNullValue());
            if (value.ValueType() != JsonValueType::Number) return L"—";
            auto text = std::format(L"{:.0f}", value.GetNumber());
            for (int i = static_cast<int>(text.size()) - 3; i > 0; i -= 3) text.insert(i, L",");
            return text;
        }

        struct Image { Bytes bytes; int height; };

        Image Draw(JsonArray const& rows, uint32_t begin, uint32_t end, std::wstring_view scope, bool jpeg)
        {
            GdiSession session;
            auto height = 164 + rowHeight * static_cast<int>(end - begin) + 62;
            Bitmap bitmap(width, height, PixelFormat32bppRGB);
            Graphics canvas(&bitmap);
            Check(canvas.Clear(Color::White));
            canvas.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            Font title(L"Malgun Gothic", 35, FontStyleBold, UnitPixel);
            Font bold(L"Malgun Gothic", 22, FontStyleBold, UnitPixel);
            Font regular(L"Malgun Gothic", 22, FontStyleRegular, UnitPixel);
            SolidBrush ink(Color(26, 45, 69));
            SolidBrush muted(Color(77, 95, 120));
            SolidBrush header(Color(227, 237, 249));
            SolidBrush stripe(Color(244, 248, 252));
            StringFormat format;
            format.SetLineAlignment(StringAlignmentCenter);
            format.SetFormatFlags(StringFormatFlagsNoWrap);
            format.SetTrimming(StringTrimmingEllipsisCharacter);
            auto draw = [&](std::wstring_view text, Font const& font, Brush const& brush,
                int x, int y, int w, int h, bool right = false)
            {
                format.SetAlignment(right ? StringAlignmentFar : StringAlignmentNear);
                Check(canvas.DrawString(text.data(), static_cast<int>(text.size()), &font,
                    RectF(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)), &format, &brush));
            };
            draw(L"탁송 조회 결과", title, ink, padding, 19, width - 2 * padding, 55);
            draw(std::wstring(scope) + L" · 차량 " + std::to_wstring(rows.Size()) + L"대 · " + Stamp(L"%Y-%m-%d %H:%M"),
                regular, muted, padding, 77, width - 2 * padding, 32);
            Check(canvas.FillRectangle(&header, padding, 117, width - 2 * padding, rowHeight));
            int x = padding;
            for (size_t i = 0; i < headings.size(); ++i)
            {
                draw(headings[i], bold, ink, x + 9, 117, colWidths[i] - 18, rowHeight, i >= 2 && i <= 4);
                x += colWidths[i];
            }
            double fuel{}, fee{}, vat{};
            for (auto const& value : rows)
            {
                auto row = value.GetObject();
                fuel += Number(row, L"amount");
                fee += Number(row, L"dispatchFee");
                vat += Number(row, L"vat");
            }
            int y = 164;
            for (auto i = begin; i < end; ++i, y += rowHeight)
            {
                auto row = rows.GetObjectAt(i);
                if ((i - begin) % 2 == 0) Check(canvas.FillRectangle(&stripe, padding, y, width - 2 * padding, rowHeight));
                std::array cells{
                    row.GetNamedBoolean(L"scheduleConflict", false) ? std::wstring(L"선택 필요") : Text(row, L"schedule"),
                    Text(row, L"plate"),
                    row.GetNamedBoolean(L"slackPending", false) ? std::wstring(L"확인 중") : NumberText(row, L"amount"),
                    row.GetNamedBoolean(L"feeConflict", false) ? std::wstring(L"선택 필요") : NumberText(row, L"dispatchFee"),
                    NumberText(row, L"vat"), Text(row, L"_label"), Text(row, L"vendor")};
                x = padding;
                for (size_t k = 0; k < cells.size(); ++k)
                {
                    draw(cells[k].empty() ? L"—" : cells[k], k == 1 ? bold : regular, ink, x + 9, y,
                        colWidths[k] - 18, rowHeight, k >= 2 && k <= 4);
                    x += colWidths[k];
                }
            }
            JsonObject totals;
            totals.SetNamedValue(L"fuel", JsonValue::CreateNumberValue(fuel));
            totals.SetNamedValue(L"fee", JsonValue::CreateNumberValue(fee));
            totals.SetNamedValue(L"vat", JsonValue::CreateNumberValue(vat));
            draw(L"합계 · 주유·충전비용 " + NumberText(totals, L"fuel") + L"원 · 탁송비용 " + NumberText(totals, L"fee") +
                L"원 · 부가세 " + NumberText(totals, L"vat") + L"원", bold, ink, padding, y + 7, width - 2 * padding, 44);
            constexpr CLSID pngId{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0, 0, 0xf8, 0x1e, 0xf3, 0x2e}};
            constexpr CLSID jpegId{0x557cf401, 0x1a04, 0x11d3, {0x9a, 0x73, 0, 0, 0xf8, 0x1e, 0xf3, 0x2e}};
            com_ptr<IStream> stream;
            check_hresult(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()));
            Check(bitmap.Save(stream.get(), jpeg ? &jpegId : &pngId));
            STATSTG info{};
            check_hresult(stream->Stat(&info, STATFLAG_NONAME));
            Bytes bytes(static_cast<size_t>(info.cbSize.QuadPart));
            check_hresult(stream->Seek({}, STREAM_SEEK_SET, nullptr));
            ULONG count{};
            check_hresult(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &count));
            if (count != bytes.size()) throw hresult_error(E_FAIL, L"결과 이미지를 읽지 못했어요.");
            return {std::move(bytes), height};
        }
    }

    Bytes CapturePng(JsonArray const& rows, std::wstring_view scope)
    {
        if (rows.Size() > 300) throw hresult_error(E_INVALIDARG, L"한 장에 300대까지 캡처할 수 있어요.");
        return Draw(rows, 0, rows.Size(), scope, false).bytes;
    }

    Bytes CapturePdf(JsonArray const& rows, std::wstring_view scope)
    {
        constexpr uint32_t perPage = 17;
        auto pages = std::max(1u, (rows.Size() + perPage - 1) / perPage);
        Bytes pdf;
        auto append = [&](std::string_view text) { pdf.insert(pdf.end(), text.begin(), text.end()); };
        std::vector<size_t> offsets{0};
        auto open = [&](std::string_view dictionary)
        {
            offsets.push_back(pdf.size());
            append(std::format("{} 0 obj\n", offsets.size() - 1));
            append(dictionary);
        };
        append("%PDF-1.4\n%\xe2\xe3\xcf\xd3\n");
        open("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
        std::string children;
        for (uint32_t page = 0; page < pages; ++page) children += std::format("{} 0 R ", 3 + page * 3);
        open(std::format("<< /Type /Pages /Count {} /Kids [{}] >>\nendobj\n", pages, children));
        for (uint32_t page = 0; page < pages; ++page)
        {
            auto id = 3 + page * 3;
            auto image = Draw(rows, page * perPage, std::min(rows.Size(), (page + 1) * perPage), scope, true);
            open(std::format("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 842 595] /Resources << /XObject << /Table {} 0 R >> >> /Contents {} 0 R >>\nendobj\n", id + 1, id + 2));
            open(std::format("<< /Type /XObject /Subtype /Image /Width {} /Height {} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length {} >>\nstream\n", width, image.height, image.bytes.size()));
            pdf.insert(pdf.end(), image.bytes.begin(), image.bytes.end());
            append("\nendstream\nendobj\n");
            int height = image.height * 790 / width;
            auto commands = std::format("q 790 0 0 {} 26 {} cm /Table Do Q\n", height, 569 - height);
            open(std::format("<< /Length {} >>\nstream\n{}endstream\nendobj\n", commands.size(), commands));
        }
        auto xref = pdf.size();
        append(std::format("xref\n0 {}\n0000000000 65535 f \n", offsets.size()));
        for (size_t i = 1; i < offsets.size(); ++i) append(std::format("{:010} 00000 n \n", offsets[i]));
        append(std::format("trailer\n<< /Size {} /Root 1 0 R >>\nstartxref\n{}\n%%EOF\n", offsets.size(), xref));
        return pdf;
    }
}
