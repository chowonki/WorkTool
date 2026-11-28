#pragma once
#include "Util.h"

namespace wt
{
    class JsEngine;

    /// 웹 WorkTool과 같은 규칙(탁송체크·이체파일)을 부르는 창구예요.
    /// 가벼운 계산은 화면 스레드의 엔진에서 바로, 업체 엑셀 읽기는 뒤쪽 스레드의 엔진에서 해요.
    class Rules
    {
    public:
        static constexpr wchar_t SlackPending[] = L"확인 중";
        struct SlackReplyResult { int applied{}; std::vector<std::wstring> issues; };
        struct Described { std::wstring label, notes; };
        enum class ExcelKind { Workbook, Encrypted, Legacy };

        Rules();
        ~Rules();

        // 이체파일
        std::vector<std::wstring> const& Banks();
        JObj ParseTransferForReview(std::wstring_view text);
        std::wstring ParseTransferAmount(std::wstring_view text);
        std::wstring TransferFileName(std::vector<JObj> const& rows, std::wstring_view stamp);
        std::vector<std::wstring> SentKeys(std::vector<JObj> const& rows);  // 은행·계좌·금액을 섞은 지문 (계좌 원문 아님)
        std::vector<JObj> SentMatches(std::vector<JObj> const& rows, JArr const& sent);
        std::vector<JObj> Split(std::vector<JObj> const& rows);
        std::vector<JObj> Check(std::vector<JObj> const& rows);
        std::vector<std::wstring> Comments(std::vector<JObj> const& rows);

        // 탁송체크
        JObj ParseVehicles(std::wstring_view text);
        std::vector<JObj> Match(JArr const& vehicles, std::vector<JObj> const& files);
        std::vector<size_t> Filter(std::vector<JObj> const& results, std::wstring_view filter);   // 해당하는 줄 번호
        std::map<std::wstring, int> Counts(std::vector<JObj> const& results, std::vector<std::wstring> const& filters);
        std::vector<Described> Describe(std::vector<JObj> const& rows);                         // 조회 상태 글자·메모
        SlackReplyResult ApplySlackReplies(std::wstring_view text, std::vector<JObj>& rows);
        JObj Choose(JObj const& row, std::wstring_view field, std::wstring_view id);
        JArr ExportRows(std::vector<JObj> const& results);
        std::wstring ExportText(std::vector<JObj> const& results);

        // 업체 엑셀. ReadVendorFile은 오래 걸릴 수 있어서 화면 스레드 밖에서 불러요.
        static ExcelKind Inspect(std::string const& bytes);
        JObj ReadVendorFile(std::string bytes, std::wstring name, std::optional<std::wstring> password);

    private:
        std::unique_ptr<JsEngine> dispatch, transfer, excel;
        std::mutex excelLock;
        std::vector<std::wstring> banks;
    };
}
