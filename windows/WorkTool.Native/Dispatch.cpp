#include "Dispatch.h"
#include "Capture.h"
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
#include <array>
#include <cwctype>
#include <numeric>

namespace worktool
{
    namespace
    {
        constexpr std::array filters{
            std::pair{"all", L"전체"}, std::pair{"review", L"확인 필요"}, std::pair{"confirmed", L"금액 확인"},
            std::pair{"progress", L"진행 중"}, std::pair{"missing", L"미조회"}, std::pair{"jeju", L"제주 출발"},
            std::pair{"carrier", L"캐리어탁송"}, std::pair{"slack", L"슬랙봇 문의"}, std::pair{"slack-replies", L"슬랙 회신 반영"}};
        constexpr std::array vendors{L"전체 업체", L"한빛탁송", L"오픈마일", L"렌톡"};
        constexpr std::array sorts{L"입력 순서", L"차량번호", L"탁송일정", L"주유·충전비용", L"탁송비용"};
        constexpr std::array sortFields{L"", L"plate", L"schedule", L"amount", L"dispatchFee"};
        constexpr std::array scopes{L"전체 조회 결과", L"금액 확인 차량", L"현재 표시 목록"};
        constexpr std::array widths{52., 62., 158., 115., 140., 118., 102., 144., 146., 340.};
        constexpr std::array headings{L"선택", L"완료", L"탁송일정", L"차량번호", L"주유·충전비용", L"탁송비용", L"부가세", L"조회 상태", L"업체", L"메모"};
        constexpr uint32_t pageSize = 50;

        void Annotate(Rules& rules, JsonArray& rows)
        {
            rules.Set("__rows", rows);
            rows = rules.Json(R"(__rows.map(r=>({...r,_label:DispatchCore.statusLabel(r),_notes:DispatchCore.notes(r),
                _filters:['all','review','confirmed','progress','missing','jeju','carrier','slack','slack-replies'].filter(f=>DispatchCore.includes(r,f))})))").GetArray();
        }

        std::vector<uint32_t> Filter(JsonArray const& rows, int filter, int vendor, int sort, std::wstring_view search)
        {
            std::vector<uint32_t> indexes;
            for (uint32_t i = 0; i < rows.Size(); ++i)
            {
                auto row = rows.GetObjectAt(i);
                if (vendor > 0 && Text(row, L"vendor").find(vendors.at(vendor)) == std::wstring::npos) continue;
                if (!search.empty() && Text(row, L"plate").find(search) == std::wstring::npos) continue;
                auto flags = row.GetNamedArray(L"_filters", JsonArray{});
                auto key = to_hstring(filters.at(std::max(0, filter)).first);
                if (filter > 0 && std::none_of(flags.begin(), flags.end(), [&](auto const& v) { return v.GetString() == key; })) continue;
                indexes.push_back(i);
            }
            if (sort > 0)
            {
                auto key = sortFields.at(sort);
                std::stable_sort(indexes.begin(), indexes.end(), [&](uint32_t a, uint32_t b)
                {
                    auto first = rows.GetObjectAt(a), second = rows.GetObjectAt(b);
                    if (sort >= 3) return Number(first, key, -1) > Number(second, key, -1);
                    return Text(first, key) < Text(second, key);
                });
            }
            return indexes;
        }

        void LoadSet(JsonObject const& value, wchar_t const* key, std::set<std::wstring>& set)
        {
            auto entries = value.GetNamedArray(key, JsonArray{});
            for (auto const& entry : entries)
                if (entry.ValueType() == JsonValueType::String) set.insert(Text(entry));
        }

        JsonArray ToArray(std::set<std::wstring> const& set)
        {
            JsonArray result;
            for (auto const& item : set) result.Append(JsonValue::CreateStringValue(hstring(item)));
            return result;
        }
    }

    Dispatch::Dispatch(Ui& ui) : ui_(ui)
    {
        try
        {
            auto marks = ui_.state.Today(L"dispatch-marks");
            LoadSet(marks, L"done", done_);
            LoadSet(marks, L"copied", copied_);
        }
        catch (...) { status_.Text(L"이전 완료 기록을 불러오지 못했어요."); }
        view_.Children().Append(Label(L"탁송체크", 30));
        view_.Children().Append(Label(L"차량목록과 업체 엑셀을 대조해 상태와 비용을 확인하세요."));
        vehicles_.PlaceholderText(L"차량번호와 날짜·시간을 붙여넣으세요.");
        vehicles_.TextChanged([this](auto const&, auto const&) { MarkStale(); });
        view_.Children().Append(vehicles_);
        auto input = Line();
        input.Children().Append(ButtonFor(L"차량목록 정리", [this] { Guard([this]
        {
            ui_.rules.Set("__input", NormalizeLines(vehicles_.Text()));
            auto parsed = ui_.rules.Json("DispatchCore.parseInput(__input)").GetObject();
            auto cleaned = Text(parsed, L"cleaned");
            if (!cleaned.empty()) vehicles_.Text(hstring(cleaned));
            auto invalid = parsed.GetNamedArray(L"invalid");
            status_.Text(hstring(L"차량 " + std::to_wstring(parsed.GetNamedArray(L"vehicles").Size()) + L"대 · 제외 " +
                std::to_wstring(invalid.Size()) + L"줄" + (invalid.Size() ? L"\n" + Join(invalid) : L"")));
        }); }));
        auto attachment = ButtonFor(L"업체 엑셀 첨부", [this] { PickFiles(); });
        attachment.AllowDrop(true);
        attachment.DragOver([this](auto const&, DragEventArgs const& args)
        {
            using namespace Windows::ApplicationModel::DataTransfer;
            if (!busy_ && args.DataView().Contains(StandardDataFormats::StorageItems()))
                args.AcceptedOperation(DataPackageOperation::Copy);
        });
        attachment.Drop([this](auto const&, DragEventArgs const& args) -> fire_and_forget
        {
            try
            {
                std::vector<fs::path> paths;
                for (auto const& item : co_await args.DataView().GetStorageItemsAsync())
                    if (item.IsOfType(Windows::Storage::StorageItemTypes::File)) paths.emplace_back(item.Path().c_str());
                LoadFiles(std::move(paths));
            }
            catch (...) { status_.Text(hstring(ErrorText())); }
        });
        input.Children().Append(attachment);
        input.Children().Append(ButtonFor(L"차량 조회하기", [this] { Guard([this] { Lookup(); }); }, true));
        input.Children().Append(ButtonFor(L"초기화", [this] { Guard([this] { Clear(); }); }));
        view_.Children().Append(Scroll(input, true));
        view_.Children().Append(filesView_);
        view_.Children().Append(status_);
        view_.Children().Append(results_);
        results_.Visibility(Visibility::Collapsed);

        results_.Children().Append(Label(L"조회 결과", 22));
        results_.Children().Append(counts_);
        auto tools = Line();
        for (auto const& [key, label] : filters) filter_.Items().Append(box_value(hstring(label)));
        for (auto label : vendors) vendor_.Items().Append(box_value(hstring(label)));
        for (auto label : sorts) sort_.Items().Append(box_value(hstring(label)));
        filter_.SelectedIndex(0);
        vendor_.SelectedIndex(0);
        sort_.SelectedIndex(0);
        Name(filter_, L"조회 상태 필터");
        Name(vendor_, L"업체 필터");
        Name(sort_, L"정렬");
        search_.Header(nullptr);
        search_.PlaceholderText(L"차량번호 검색");
        Name(search_, L"차량번호 검색");
        search_.Width(160);
        auto refilter = [this](auto const&, auto const&) { page_ = 0; Guard([this] { RefreshResults(); }); };
        filter_.SelectionChanged(refilter);
        vendor_.SelectionChanged(refilter);
        sort_.SelectionChanged(refilter);
        search_.TextChanged(refilter);
        tools.Children().Append(filter_);
        tools.Children().Append(vendor_);
        tools.Children().Append(sort_);
        tools.Children().Append(search_);
        tools.Children().Append(ButtonFor(L"현재 목록 선택", [this]
        {
            for (auto i : Filter(rows_, filter_.SelectedIndex(), vendor_.SelectedIndex(), sort_.SelectedIndex(), Trim(search_.Text())))
                selected_.insert(Text(rows_.GetObjectAt(i), L"plate"));
            RefreshResults();
        }));
        tools.Children().Append(ButtonFor(L"선택 해제", [this] { selected_.clear(); RefreshResults(); }));
        results_.Children().Append(Scroll(tools, true));
        results_.Children().Append(Scroll(table_, true));
        auto pages = Line();
        pages.Children().Append(ButtonFor(L"이전", [this] { if (page_) --page_; RefreshResults(); }));
        pages.Children().Append(pageLabel_);
        pages.Children().Append(ButtonFor(L"다음", [this] { ++page_; RefreshResults(); }));
        results_.Children().Append(pages);

        Expander slack;
        slack.HorizontalAlignment(HorizontalAlignment::Stretch);
        slack.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        slack.Header(box_value(L"슬랙봇 확인"));
        auto body = Column();
        replies_.PlaceholderText(L"12가3456 - X\n34나5678 - 확인 중");
        body.Children().Append(replies_);
        body.Children().Append(ButtonFor(L"회신 반영", [this] { Guard([this] { ApplyReply(); }); }));
        slack.Content(body);
        results_.Children().Append(slack);

        auto exports = Line();
        for (auto text : scopes) scope_.Items().Append(box_value(hstring(text)));
        scope_.SelectedIndex(0);
        Name(scope_, L"결과 저장 범위");
        exports.Children().Append(scope_);
        exports.Children().Append(ButtonFor(L"차량번호 복사", [this] { Guard([this] { CopyPlates(); }); }));
        exports.Children().Append(ButtonFor(L"캡처 복사", [this] { Export(L"png", true); }));
        for (auto const& [title, extension] : {std::pair{L"PNG 저장", L"png"}, {L"TXT 저장", L"txt"},
            {L"Excel 저장", L"xlsx"}, {L"PDF 저장", L"pdf"}})
            exports.Children().Append(ButtonFor(title, [this, extension] { Export(extension); }));
        results_.Children().Append(Scroll(exports, true));
    }

    void Dispatch::Guard(Action action)
    {
        if (busy_) return;
        try { action(); }
        catch (...) { status_.Text(hstring(ErrorText())); }
    }

    fire_and_forget Dispatch::PickFiles()
    {
        if (busy_) co_return;
        try { LoadFiles(ui_.PickExcelFiles()); }
        catch (...) { status_.Text(hstring(ErrorText())); }
    }

    fire_and_forget Dispatch::LoadFiles(std::vector<fs::path> paths)
    {
        if (busy_ || paths.empty()) co_return;
        busy_ = true;
        apartment_context foreground;
        std::wstring errors;
        for (auto const& path : paths)
        {
            auto name = path.filename().wstring();
            auto extension = path.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
            status_.Text(hstring(name + L" 읽는 중…"));
            try
            {
                if (extension != L".xlsx") throw hresult_error(E_INVALIDARG, L".xlsx 파일을 선택해 주세요.");
                co_await resume_background();
                auto bytes = ReadBytes(path);
                std::wstring password;
                if (Encrypted(bytes))
                {
                    co_await foreground;
                    PasswordBox input;
                    input.Header(box_value(L"엑셀 암호"));
                    if (co_await ui_.Ask(name, input, L"열기", L"건너뛰기") != ContentDialogResult::Primary) continue;
                    password = std::wstring(input.Password());
                    input.Password(L"");
                    if (password.empty()) throw hresult_error(E_INVALIDARG, L"암호를 입력해 주세요.");
                    co_await resume_background();
                }
                Rules workbook(true);
                auto vendor = workbook.Vendor(bytes, name, password);
                auto serialized = vendor.Stringify();
                co_await foreground;
                auto file = JsonObject::Parse(serialized);
                for (uint32_t i = files_.Size(); i > 0; --i)
                    if (Text(files_.GetObjectAt(i - 1), L"_path") == path.wstring()) files_.RemoveAt(i - 1);
                Put(file, L"_path", path.wstring());
                files_.Append(file);
                MarkStale();
            }
            catch (...) { errors += name + L" · " + ErrorText() + L"\n"; }
            co_await foreground;
        }
        busy_ = false;
        RefreshFiles();
        status_.Text(hstring(errors.empty() ? L"업체 파일을 첨부했어요. 차량 조회하기를 눌러 결과를 갱신하세요." : errors));
    }

    void Dispatch::RefreshFiles()
    {
        filesView_.Children().Clear();
        for (uint32_t i = 0; i < files_.Size(); ++i)
        {
            auto file = files_.GetObjectAt(i);
            auto line = Line();
            auto title = Text(file, L"name") + L" · " + std::to_wstring(file.GetNamedArray(L"records").Size()) + L"건";
            auto warnings = file.GetNamedArray(L"warnings", JsonArray{});
            std::set<std::wstring> names;
            auto records = file.GetNamedArray(L"records");
            for (auto const& row : records) names.insert(Text(row.GetObject(), L"vendor"));
            for (auto const& name : names) if (!name.empty()) title += L" · " + name;
            if (warnings.Size()) title += L" · 양식 확인 필요\n" + Join(warnings);
            line.Children().Append(Label(title));
            line.Children().Append(ButtonFor(L"제거", [this, i]
            {
                Guard([this, i] { files_.RemoveAt(i); RefreshFiles(); MarkStale(); });
            }));
            filesView_.Children().Append(line);
        }
    }

    void Dispatch::Lookup()
    {
        ui_.rules.Set("__input", NormalizeLines(vehicles_.Text()));
        ui_.rules.Set("__files", files_);
        auto parsed = ui_.rules.Json("DispatchCore.parseInput(__input)").GetObject();
        auto vehicles = parsed.GetNamedArray(L"vehicles");
        auto invalid = parsed.GetNamedArray(L"invalid");
        if (invalid.Size()) throw hresult_error(E_INVALIDARG, hstring(L"인식하지 못한 차량번호를 수정해 주세요.\n" + Join(invalid)));
        if (!vehicles.Size()) throw hresult_error(E_INVALIDARG, L"유효한 차량번호를 입력해 주세요.");
        if (!files_.Size()) throw hresult_error(E_INVALIDARG, L"업체 엑셀을 첨부해 주세요.");
        ui_.rules.Set("__vehicles", vehicles);
        rows_ = ui_.rules.Json("DispatchCore.match(__vehicles,__files)").GetArray();
        Annotate(ui_.rules, rows_);
        selected_.clear();
        stale_ = false;
        page_ = 0;
        results_.Visibility(Visibility::Visible);
        auto duplicates = static_cast<int>(parsed.GetNamedNumber(L"duplicates"));
        replies_.Text(L"");
        status_.Text(hstring(L"차량 " + std::to_wstring(rows_.Size()) + L"대 조회 · 입력 중복 " +
            std::to_wstring(duplicates) + L"건"));
        RefreshResults();
    }

    void Dispatch::RefreshResults()
    {
        auto visible = Filter(rows_, filter_.SelectedIndex(), vendor_.SelectedIndex(), sort_.SelectedIndex(), Trim(search_.Text()));
        auto pageCount = std::max(1u, static_cast<uint32_t>((visible.size() + pageSize - 1) / pageSize));
        page_ = std::min(page_, pageCount - 1);
        auto count = std::to_wstring(visible.size()) + L"대 표시 / 전체 " + std::to_wstring(rows_.Size()) + L"대";
        if (!selected_.empty()) count += L" · " + std::to_wstring(selected_.size()) + L"대 선택";
        counts_.Text(hstring(count));
        pageLabel_.Text(hstring(std::to_wstring(page_ + 1) + L" / " + std::to_wstring(pageCount)));
        auto start = visible.begin() + page_ * pageSize;
        auto end = visible.begin() + std::min(visible.size(), static_cast<size_t>((page_ + 1) * pageSize));
        table_.Children().Clear();
        table_.Children().Append(RenderTable(std::vector<uint32_t>(start, end), true));
    }

    StackPanel Dispatch::RenderTable(std::vector<uint32_t> const& indexes, bool header)
    {
        auto list = Column(2);
        if (header)
        {
            Grid headerRow;
            for (size_t i = 0; i < headings.size(); ++i) Place(headerRow, Label(headings[i]), widths[i]);
            list.Children().Append(headerRow);
        }
        for (auto i : indexes)
        {
            Grid grid;
            auto row = rows_.GetObjectAt(i);
            auto plate = Text(row, L"plate");
            CheckBox select;
            select.MinWidth(0);
            select.IsChecked(box_value(selected_.contains(plate)).as<IReference<bool>>());
            Name(select, plate + L" 선택");
            select.Click([this, select = make_weak(select), plate](auto const&, auto const&)
            {
                if (auto control = select.get(); control && control.IsChecked().Value()) selected_.insert(plate);
                else selected_.erase(plate);
                auto summary = std::wstring(counts_.Text());
                if (auto at = summary.find(L" · "); at != std::wstring::npos) summary.resize(at);
                if (!selected_.empty()) summary += L" · " + std::to_wstring(selected_.size()) + L"대 선택";
                counts_.Text(hstring(summary));
            });
            Place(grid, select, widths[0]);
            CheckBox done;
            done.MinWidth(0);
            done.IsChecked(box_value(done_.contains(plate)).as<IReference<bool>>());
            Name(done, plate + L" 완료");
            done.Click([this, plate, done = make_weak(done)](auto const&, auto const&)
            {
                if (auto control = done.get(); control && control.IsChecked().Value()) done_.insert(plate);
                else done_.erase(plate);
                Guard([this] { SaveMarks(); RefreshResults(); });
            });
            Place(grid, done, widths[1]);
            auto cell = [&](size_t column, std::wstring value, bool selectable, std::string const& field = {})
            {
                if (selectable)
                {
                    Place(grid, ButtonFor(value.empty() ? L"선택 필요" : value, [this, i, field] { Choose(i, field); }), widths[column]);
                }
                else if (!value.empty())
                {
                    auto button = ButtonFor(value, [this, value, plate] { Guard([&] { CopyCell(value, plate); }); });
                    Name(button, headings[column] + (L" " + value + L" 복사"));
                    Place(grid, button, widths[column]);
                }
                else Place(grid, Label(L"—"), widths[column]);
            };
            cell(2, Text(row, L"schedule"), row.GetNamedBoolean(L"scheduleConflict", false), "schedule");
            cell(3, plate, false);
            cell(4, row.GetNamedBoolean(L"slackPending", false) ? L"확인 중" : Text(row, L"amount"), false);
            cell(5, Text(row, L"dispatchFee"), row.GetNamedBoolean(L"feeConflict", false), "fee");
            cell(6, Text(row, L"vat"), false);
            auto state = Text(row, L"_label");
            if (done_.contains(plate)) state += L" · 완료";
            else if (copied_.contains(plate)) state += L" · 복사함";
            Place(grid, Label(state), widths[7]);
            Place(grid, Label(Text(row, L"vendor")), widths[8]);
            Place(grid, Label(Text(row, L"_notes")), widths[9]);
            list.Children().Append(grid);
        }
        return list;
    }

    fire_and_forget Dispatch::Choose(uint32_t rowIndex, std::string field)
    {
        if (busy_) co_return;
        try
        {
            auto row = rows_.GetObjectAt(rowIndex);
            auto candidates = row.GetNamedArray(field == "schedule" ? L"scheduleCandidates" : L"feeCandidates");
            ComboBox choices;
            Name(choices, L"업체 기록");
            for (auto const& item : candidates)
            {
                auto option = item.GetObject();
                choices.Items().Append(box_value(hstring(Text(option, L"value") + L" · " + Text(option, L"source"))));
            }
            choices.SelectedIndex(0);
            if (co_await ui_.Ask(field == "schedule" ? L"탁송일정 선택" : L"탁송비용 선택", choices, L"반영", L"취소")
                != ContentDialogResult::Primary || choices.SelectedIndex() < 0) co_return;
            ui_.rules.Set("__row", row);
            ui_.rules.Set("__field", to_hstring(field));
            ui_.rules.Set("__id", Text(candidates.GetObjectAt(choices.SelectedIndex()), L"id"));
            auto updated = ui_.rules.Json("(DispatchCore.useCandidate(__row,__field,__id),__row)");
            rows_.SetAt(rowIndex, updated);
            Annotate(ui_.rules, rows_);
            RefreshResults();
        }
        catch (...) { status_.Text(hstring(ErrorText())); }
    }

    void Dispatch::ApplyReply()
    {
        if (stale_) throw hresult_error(E_INVALIDARG, L"자료가 바뀌었어요. 차량을 다시 조회해 주세요.");
        ui_.rules.Set("__rows", rows_);
        ui_.rules.Set("__replies", NormalizeLines(replies_.Text()));
        auto result = ui_.rules.Json("({ ...DispatchCore.applySlackReplies(__replies,__rows),rows:__rows })").GetObject();
        rows_ = result.GetNamedArray(L"rows");
        Annotate(ui_.rules, rows_);
        auto applied = static_cast<int>(result.GetNamedNumber(L"applied"));
        auto issues = result.GetNamedArray(L"issues");
        status_.Text(hstring(std::to_wstring(applied) + L"건 반영" + (issues.Size() ? L"\n" + Join(issues) : L"")));
        if (applied && !issues.Size()) replies_.Text(L"");
        RefreshResults();
    }

    void Dispatch::SaveMarks()
    {
        JsonObject marks;
        marks.SetNamedValue(L"done", ToArray(done_));
        marks.SetNamedValue(L"copied", ToArray(copied_));
        ui_.state.SaveToday(L"dispatch-marks", marks);
    }

    void Dispatch::CopyCell(std::wstring const& value, std::wstring const& plate)
    {
        if (stale_) throw hresult_error(E_INVALIDARG, L"자료가 바뀌었어요. 차량을 다시 조회해 주세요.");
        ClipboardText(value);
        copied_.insert(plate);
        SaveMarks();
        RefreshResults();
        status_.Text(hstring(value + L" 복사"));
    }

    JsonArray Dispatch::ExportRows()
    {
        if (stale_) throw hresult_error(E_INVALIDARG, L"자료가 바뀌었어요. 차량을 다시 조회해 주세요.");
        JsonArray rows;
        if (selected_.empty() && scope_.SelectedIndex() == 2)
        {
            for (auto i : Filter(rows_, filter_.SelectedIndex(), vendor_.SelectedIndex(), sort_.SelectedIndex(), Trim(search_.Text())))
                rows.Append(rows_.GetAt(i));
        }
        else
        {
            for (auto const& value : rows_)
            {
                auto row = value.GetObject();
                if (!selected_.empty() ? selected_.contains(Text(row, L"plate")) :
                    scope_.SelectedIndex() != 1 || Text(row, L"status") == L"confirmed") rows.Append(value);
            }
        }
        if (!rows.Size()) throw hresult_error(E_INVALIDARG, L"복사하거나 저장할 차량이 없어요.");
        ui_.rules.Set("__rows", rows);
        return ui_.rules.Json("DispatchCore.unique(__rows)").GetArray();
    }

    void Dispatch::CopyPlates()
    {
        if (stale_) throw hresult_error(E_INVALIDARG, L"자료가 바뀌었어요. 차량을 다시 조회해 주세요.");
        JsonArray input;
        if (selected_.empty())
        {
            for (auto i : Filter(rows_, filter_.SelectedIndex(), vendor_.SelectedIndex(), sort_.SelectedIndex(), Trim(search_.Text())))
                input.Append(rows_.GetAt(i));
        }
        else
        {
            for (auto const& row : rows_)
                if (selected_.contains(Text(row.GetObject(), L"plate"))) input.Append(row);
        }
        if (!input.Size()) throw hresult_error(E_INVALIDARG, L"현재 목록에 복사할 차량이 없어요.");
        ui_.rules.Set("__rows", input);
        auto rows = ui_.rules.Json("DispatchCore.unique(__rows)").GetArray();
        std::wstring plates, jeju;
        bool slack = filter_.SelectedIndex() == 7;
        for (auto const& value : rows)
        {
            auto row = value.GetObject();
            auto& group = slack && row.GetNamedBoolean(L"jeju", false) ? jeju : plates;
            if (!group.empty()) group += L"\r\n";
            group += Text(row, L"plate");
        }
        if (slack)
        {
            if (!plates.empty()) plates = L"[일반차량]\r\n" + plates;
            if (!jeju.empty()) plates = L"[제주도출발차량]\r\n" + jeju + (plates.empty() ? L"" : L"\r\n\r\n" + plates);
        }
        ClipboardText(plates);
        for (auto const& row : rows) copied_.insert(Text(row.GetObject(), L"plate"));
        SaveMarks();
        RefreshResults();
        status_.Text(hstring(std::to_wstring(rows.Size()) + L"대 차량번호를 복사했어요."));
    }

    fire_and_forget Dispatch::Export(std::wstring extension, bool copy)
    {
        if (busy_) co_return;
        apartment_context foreground;
        std::wstring status;
        try
        {
            auto rows = ExportRows();
            ui_.rules.Set("__rows", rows);
            if (ui_.rules.Json("__rows.some(DispatchCore.hasUnresolvedChoice)").GetBoolean())
                throw hresult_error(E_INVALIDARG, L"탁송일정과 비용에서 선택이 필요한 항목을 먼저 골라 주세요.");
            auto serialized = rows.Stringify();
            auto scope = selected_.empty() ? std::wstring(scopes.at(scope_.SelectedIndex())) : L"선택 차량";
            fs::path path;
            if (!copy)
            {
                path = ui_.PickSave(L"탁송조회_" + Stamp(L"%Y%m%d_%H%M") + L"." + extension, extension);
                if (path.empty()) co_return;
            }
            busy_ = true;
            status_.Text(L"결과를 만드는 중…");
            auto txt = extension == L"txt" ? to_string(ui_.rules.Json("DispatchCore.textExport(__rows)").GetString()) : "";
            co_await resume_background();
            auto jobRows = JsonArray::Parse(serialized);
            Bytes bytes;
            if (extension == L"txt") bytes.assign(txt.begin(), txt.end());
            else if (extension == L"xlsx") bytes = Rules(true).DispatchExcel(jobRows);
            else if (extension == L"png") bytes = CapturePng(jobRows, scope);
            else bytes = CapturePdf(jobRows, scope);
            if (!copy) WriteBytes(path, bytes);
            co_await foreground;
            if (copy) ClipboardPng(bytes);
            status = copy ? L"결과 이미지를 복사했어요." : L"결과 파일을 저장했어요.";
        }
        catch (...) { status = ErrorText(); }
        co_await foreground;
        busy_ = false;
        status_.Text(hstring(status));
    }

    void Dispatch::Clear()
    {
        vehicles_.Text(L"");
        replies_.Text(L"");
        files_.Clear();
        rows_.Clear();
        filesView_.Children().Clear();
        table_.Children().Clear();
        results_.Visibility(Visibility::Collapsed);
        selected_.clear();
        done_.clear();
        copied_.clear();
        stale_ = false;
        page_ = 0;
        filter_.SelectedIndex(0);
        vendor_.SelectedIndex(0);
        sort_.SelectedIndex(0);
        search_.Text(L"");
        ui_.state.Forget(L"dispatch-marks");
        status_.Text(L"차량목록과 조회 결과를 초기화했어요.");
    }

    void Dispatch::MarkStale()
    {
        if (!rows_.Size()) return;
        stale_ = true;
        status_.Text(L"자료가 바뀌었어요. 차량 조회하기를 눌러 결과를 갱신하세요.");
    }
}
