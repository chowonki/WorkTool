#include "pch.h"
#include "Rules.h"
#include "JsEngine.h"

namespace wt
{
    static std::string Resource(wchar_t const* name)
    {
        auto found = FindResourceW(nullptr, name, RT_RCDATA);
        auto loaded = found ? LoadResource(nullptr, found) : nullptr;
        if (!loaded) throw AppError{ L"규칙을 불러오지 못했어요." };
        std::string text(static_cast<char const*>(LockResource(loaded)), SizeofResource(nullptr, found));
        if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
        return text;
    }

    static std::unique_ptr<JsEngine> Create(wchar_t const* script, char const* name)
    {
        // 화면 스레드 엔진: 메모리 128MB, 한 번에 30초, 스택 4MB (실행 파일 스택은 8MB)
        auto engine = std::make_unique<JsEngine>(128'000'000, 30, 4 * 1024 * 1024);
        engine->Eval(Resource(script), name);
        return engine;
    }

    static void ReplaceAll(std::string& text, std::string_view from, std::string_view to)
    {
        for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size()) text.replace(at, from.size(), to);
    }

    Rules::Rules() : dispatch(Create(L"DISPATCH_JS", "dispatch.js")), transfer(Create(L"TRANSFER_JS", "transfer.js")) {}
    Rules::~Rules() = default;

    static std::string Text(std::wstring_view value) { return JsString(value); }
    static std::wstring Line(std::wstring_view value) { return NormalizeLines(value); }

    // ── 이체파일 ──
    std::vector<std::wstring> const& Rules::Banks()
    {
        if (banks.empty()) banks = Strings(ParseArray(transfer->Json("BANK_OPTIONS")));
        return banks;
    }

    JObj Rules::ParseTransferForReview(std::wstring_view text)
    {
        return ParseObject(transfer->Json("parseForReview(" + Text(Line(text)) + ")"));
    }

    std::wstring Rules::ParseTransferAmount(std::wstring_view text)
    {
        return ToWide(transfer->Eval("String(parseKoreanAmount(" + Text(text) + "))"));
    }

    std::wstring Rules::TransferFileName(std::vector<JObj> const& rows, std::wstring_view stamp)
    {
        transfer->Eval("rows=" + Dump(rows) + ";0");
        return ToWide(transfer->Eval("String(fileName(" + Text(stamp) + "))"));
    }

    std::vector<std::wstring> Rules::SentKeys(std::vector<JObj> const& rows)
    {
        return Strings(ParseArray(transfer->Json(Dump(rows) + ".map(sentKey)")));
    }

    std::vector<JObj> Rules::SentMatches(std::vector<JObj> const& rows, JArr const& sent)
    {
        return Objects(ParseArray(transfer->Json("sentMatches(" + Dump(rows) + "," + ToUtf8(sent.Stringify()) + ")")));
    }

    std::vector<JObj> Rules::Split(std::vector<JObj> const& rows)
    {
        return Objects(ParseArray(transfer->Json("splitAll(" + Dump(rows) + ")")));
    }

    std::vector<JObj> Rules::Check(std::vector<JObj> const& rows)
    {
        transfer->Eval("rows=" + Dump(rows) + ";0");
        return Objects(ParseArray(transfer->Json("rows.map(check)")));
    }

    std::vector<std::wstring> Rules::Comments(std::vector<JObj> const& rows)
    {
        transfer->Eval("rows=" + Dump(rows) + ";0");
        return Strings(ParseArray(transfer->Json("buildComments()")));
    }

    // ── 탁송체크 ──
    JObj Rules::ParseVehicles(std::wstring_view text)
    {
        return ParseObject(dispatch->Json("DispatchCore.parseInput(" + Text(text) + ")"));
    }

    std::vector<JObj> Rules::Match(JArr const& vehicles, std::vector<JObj> const& files)
    {
        return Objects(ParseArray(dispatch->Json("DispatchCore.match(" + ToUtf8(vehicles.Stringify()) + "," + Dump(files) + ")")));
    }

    std::vector<size_t> Rules::Filter(std::vector<JObj> const& results, std::wstring_view filter)
    {
        std::vector<size_t> indexes;
        if (results.empty()) return indexes;
        auto found = ParseArray(dispatch->Json(Dump(results) + ".map((r,i)=>DispatchCore.includes(r," + Text(filter) + ")?i:-1).filter(i=>i>=0)"));
        for (auto const& item : found) indexes.push_back(static_cast<size_t>(item.GetNumber()));
        return indexes;
    }

    std::map<std::wstring, int> Rules::Counts(std::vector<JObj> const& results, std::vector<std::wstring> const& filters)
    {
        std::map<std::wstring, int> counts;
        if (results.empty()) return counts;
        std::string keys = "[";
        for (auto const& filter : filters) keys += Text(filter) + ",";
        keys += "]";
        // 보기별 대수: 차량번호 중복은 한 대로 세요.
        auto found = ParseArray(dispatch->Json("(rs=>" + keys + ".map(k=>new Set(rs.filter(r=>DispatchCore.includes(r,k)).map(r=>r.plate)).size))(" + Dump(results) + ")"));
        for (uint32_t i = 0; i < found.Size() && i < filters.size(); i++) counts[filters[i]] = static_cast<int>(found.GetNumberAt(i));
        return counts;
    }

    std::vector<Rules::Described> Rules::Describe(std::vector<JObj> const& rows)
    {
        std::vector<Described> described;
        if (rows.empty()) return described;
        auto found = ParseArray(dispatch->Json(Dump(rows) + ".map(r=>[DispatchCore.statusLabel(r)||'',DispatchCore.notes(r)||''])"));
        for (auto const& item : found)
        {
            auto pair = item.GetArray();
            described.push_back({ std::wstring(pair.GetStringAt(0)), std::wstring(pair.GetStringAt(1)) });
        }
        return described;
    }

    Rules::SlackReplyResult Rules::ApplySlackReplies(std::wstring_view text, std::vector<JObj>& rows)
    {
        auto result = ParseObject(dispatch->Json("((rows)=>({rows,...DispatchCore.applySlackReplies(" + Text(text) + ",rows)}))(" + Dump(rows) + ")"));
        SlackReplyResult reply{ static_cast<int>(result.GetNamedNumber(L"applied", 0)), Strings(result.TryLookup(L"issues")) };
        if (reply.applied > 0) rows = Objects(Arr(result, L"rows"));
        return reply;
    }

    JObj Rules::Choose(JObj const& row, std::wstring_view field, std::wstring_view id)
    {
        dispatch->Eval("globalThis.chosen=" + Dump(row) + "; DispatchCore.useCandidate(chosen," + Text(field) + "," + Text(id) + ");0");
        return ParseObject(dispatch->Json("chosen"));
    }

    JArr Rules::ExportRows(std::vector<JObj> const& results)
    {
        return ParseArray(dispatch->Json("[DispatchCore.headers,...DispatchCore.unique(" + Dump(results) + ").map(DispatchCore.values)]"));
    }

    std::wstring Rules::ExportText(std::vector<JObj> const& results)
    {
        return ToWide(dispatch->Eval("String(DispatchCore.textExport(" + Dump(results) + "))"));
    }

    // ── 업체 엑셀 ──
    Rules::ExcelKind Rules::Inspect(std::string const& bytes)
    {
        // 복합 문서(OLE) 머리글이면 암호를 건 xlsx이거나 예전 .xls예요.
        static char const signature[] = "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1";
        if (bytes.size() < 8 || memcmp(bytes.data(), signature, 8) != 0) return ExcelKind::Workbook;
        static wchar_t const stream[] = L"EncryptedPackage";
        std::string_view needle(reinterpret_cast<char const*>(stream), sizeof stream - sizeof(wchar_t));
        return bytes.find(needle) != std::string::npos ? ExcelKind::Encrypted : ExcelKind::Legacy;
    }

    // 엑셀 라이브러리를 엔진에 맞게 조금 고쳐 불러와요.
    // 1) 암호 풀 때 SHA 10만 번 반복을 __nativeSpin(Windows CNG)으로 해요. 결과는 같고 훨씬 빨라요.
    // 2) u 플래그 정규식 문자열 안의 `\-`를 같은 뜻인 `\x2d`로 바꿔요 (QuickJS 계열이 `\-`를 거부할 때가 있어요).
    static std::string PatchedExcel()
    {
        auto script = Resource(L"EXCEL_JS");
        ReplaceAll(script, "\r\n", "\n");
        ReplaceAll(script, R"(\\-)", R"(\\x2d)");
        std::string const loop =
            "        for (let i = 0; i < spinCount; i++) {\n"
            "            const iterator = this._createUInt32LEBuffer(i);\n"
            "            key = this._hash(hashAlgorithm, iterator, key);\n"
            "        }";
        if (auto at = script.find(loop); at != std::string::npos)
            script.replace(at, loop.size(),
                "        if (typeof __nativeSpin === \"function\" && /^sha-?(1|512)$/i.test(String(hashAlgorithm))) "
                "key = Buffer.from(__nativeSpin(new Uint8Array(key), spinCount, /^sha-?1$/i.test(String(hashAlgorithm))));\n"
                "        else " + loop);
        return script;
    }

    JObj Rules::ReadVendorFile(std::string bytes, std::wstring name, std::optional<std::wstring> password)
    {
        struct Job
        {
            Rules* rules; std::string* bytes; std::wstring* name; std::optional<std::wstring>* password;
            std::string json; std::optional<AppError> error;
        } job{ this, &bytes, &name, &password };
        // 엑셀 라이브러리는 깊게 재귀해서 스택을 넉넉하게(64MB) 준 스레드에서 돌려요.
        auto run = [](void* parameter) -> DWORD
        {
            auto& job = *static_cast<Job*>(parameter);
            std::lock_guard lock(job.rules->excelLock);
            try
            {
                auto& engine = job.rules->excel;
                if (!engine)
                {
                    engine = std::make_unique<JsEngine>(1'500'000'000, 180, 48 * 1024 * 1024);
                    engine->Eval(PatchedExcel(), "excel.js");
                    engine->Eval(Resource(L"DISPATCH_JS"), "dispatch.js");
                }
                engine->StageBytes(std::vector<uint8_t>(job.bytes->begin(), job.bytes->end()));
                engine->Eval("globalThis.__file=__takeBytes();globalThis.__name=" + JsString(*job.name) +
                    ";globalThis.__pw=" + (*job.password ? JsString(**job.password) : std::string("null")) + ";0");
                try
                {
                    engine->Wait("(async()=>{const b=__pw!=null?await DispatchExcel.decrypt(__file.buffer,__pw):__file.buffer;"
                        "const sheets=await DispatchExcel.readAsync(b);globalThis.__read=DispatchCore.readSheets(sheets,__name);})()");
                    job.json = engine->Json("__read");
                    engine->Eval("globalThis.__pw=null;globalThis.__file=null;globalThis.__read=null;0");
                }
                catch (AppError const&)
                {
                    engine->Eval("globalThis.__pw=null;globalThis.__file=null;globalThis.__read=null;0");
                    throw;
                }
            }
            catch (AppError const& error) { job.error = error; }
            catch (std::exception const&) { job.error = AppError{ L"엑셀 파일을 읽지 못했어요." }; }
            catch (winrt::hresult_error const& error) { job.error = AppError{ std::wstring(error.message()) }; }
            return 0;
        };
        auto thread = CreateThread(nullptr, 64 * 1024 * 1024, run, &job, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (!thread) throw AppError{ L"엑셀 파일을 읽지 못했어요." };
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        if (job.error)
        {
            if (password) throw AppError{ L"암호가 맞지 않거나 지원하지 않는 암호화 파일이에요. 암호와 파일을 확인해 주세요." };
            throw *job.error;
        }
        return ParseObject(job.json);
    }
}
