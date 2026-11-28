#pragma once
#include "Util.h"

namespace wt::Spreadsheets
{
    /// 값 배열(첫 줄은 머리글)을 .xlsx로 저장해요. 숫자는 숫자 셀, 나머지는 글자 셀이에요.
    void Write(std::filesystem::path const& path, JArr const& values, std::wstring_view sheetName = L"조회 결과");
    /// 하나은행 다계좌이체 양식 6개 열. 계좌번호는 글자, 금액은 숫자로 적어요.
    JArr TransferRows(std::vector<JObj> const& rows);
}

namespace wt::LocalState
{
    // 이 기기에 남기는 작은 상태 파일. 포터블 실행(portable.mode)이면 실행 폴더 옆 Data 폴더에, 아니면 LocalAppData에 둬요.
    std::filesystem::path DataFile(std::wstring_view name);
    JObj ReadToday(std::wstring_view name);        // 오늘 날짜로만 유지. 날짜가 바뀌면 비어 있는 것으로 봐요.
    void WriteToday(std::wstring_view name, JObj const& value);
    void Delete(std::wstring_view name);
    std::optional<std::vector<double>> ReadNumbers(std::wstring_view name);
    bool WriteNumbers(std::wstring_view name, std::vector<double> const& values);
}
