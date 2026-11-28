#include "pch.h"
#include "MainWindow.h"

// 탁송체크: 작업 표시·열 너비·파일 목록·필터와 정렬·슬랙봇 회신·결과 캡처

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Graphics::Imaging;
using winrt::Windows::Foundation::DateTime;

namespace wt
{
    static wchar_t const WorkMarksFile[] = L"dispatch-work-marks.json";
    static wchar_t const DispatchWidthsFile[] = L"dispatch-columns.json";
    static constexpr int SlackPageSize = 10;

    // 복사함·완료 표시는 오늘 날짜로만 이 기기에 남겨요. 앱을 다시 열고 같은 목록을 조회해도 이어서 볼 수 있어요.
    void MainWindow::LoadWorkMarks()
    {
        auto saved = LocalState::ReadToday(WorkMarksFile);
        for (auto const& plate : Strings(saved.TryLookup(L"copied"))) copiedDispatch.insert(plate);
        for (auto const& plate : Strings(saved.TryLookup(L"done"))) completed.insert(plate);
    }

    void MainWindow::SaveWorkMarks()
    {
        if (copiedDispatch.empty() && completed.empty()) { LocalState::Delete(WorkMarksFile); return; }
        auto list = [](std::set<std::wstring> const& plates)
        {
            JArr array;
            for (auto const& plate : plates) array.Append(Windows::Data::Json::JsonValue::CreateStringValue(plate));
            return array;
        };
        JObj saved;
        saved.Insert(L"copied", list(copiedDispatch));
        saved.Insert(L"done", list(completed));
        LocalState::WriteToday(WorkMarksFile, saved);
    }

    void MainWindow::LoadDispatchWidths()
    {
        auto saved = LocalState::ReadNumbers(DispatchWidthsFile);
        if (saved && saved->size() == dispatchColumns.size()
            && std::all_of(saved->begin(), saved->end(), [](double width) { return width == 0 || (width >= 48 && width <= 480); }))
            dispatchColumns = *saved;
    }

    void MainWindow::UpdateDispatchHeader()
    {
        auto children = dispatchTable.Children();
        if (children.Size() > 1) children.RemoveAt(0);
        double total = 0;
        for (auto width : dispatchColumns) total += width;
        dispatchTable.MinWidth(std::max(1020.0, total + 120));
        children.InsertAt(0, TableHeader(dispatchColumns, DispatchHeadings));
    }

    static NumberBox WidthBox(std::wstring const& label, double value, double minimum)
    {
        NumberBox box;
        box.Header(box_value(hstring(label)));
        box.Value(value);
        box.Minimum(minimum);
        box.Maximum(480);
        box.SmallChange(8);
        box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
        return box;
    }

    Async MainWindow::EditDispatchWidths()
    {
        std::vector<NumberBox> inputs;
        for (size_t i = 0; i < DispatchHeadings.size(); i++) inputs.push_back(WidthBox(DispatchHeadings[i], dispatchColumns[i], i == 1 ? 0 : 48));
        auto content = Stack({ Text(L"픽셀 단위로 조절할 수 있어요. 탁송일정 열의 0은 자동 너비예요.") });
        for (size_t i = 0; i < inputs.size(); i += 3) content.Children().Append(Columns({ 0, 0, 0 }, { inputs[i], inputs[i + 1], inputs[i + 2] }));
        auto dialog = Dialog(L"탁송 표 열 너비", content, L"적용");
        dialog.SecondaryButtonText(L"기본값");
        auto result = co_await dialog.ShowAsync();
        if (result == ContentDialogResult::None) co_return;
        if (result == ContentDialogResult::Secondary) dispatchColumns = DefaultDispatchColumns;
        else
            for (size_t i = 0; i < inputs.size(); i++)
            {
                auto value = inputs[i].Value();
                if (!std::isnan(value)) dispatchColumns[i] = i == 1 && value == 0 ? 0 : std::clamp(value, 48.0, 480.0);
            }
        UpdateDispatchHeader();
        RenderDispatch();
        LocalState::WriteNumbers(DispatchWidthsFile, dispatchColumns);
    }

    void MainWindow::CleanVehicles()
    {
        Guard([this]
        {
            auto input = rules.ParseVehicles(vehicles.Text());
            if (Arr(input, L"invalid").Size() > 0)
            {
                Notify(L"확인 필요로 표시된 줄을 고친 후 목록을 정리해 주세요.", true);
                return;
            }
            vehicles.Text(Str(input, L"cleaned"));
        });
    }

    static std::wstring VendorName(std::wstring const& vendor)
    {
        return vendor == L"A" ? L"한빛탁송" : vendor == L"B" ? L"오픈마일" : vendor == L"C" ? L"렌톡" : vendor;
    }

    void MainWindow::RenderDispatchFiles()
    {
        // 비어 있으면 안내 글자만, 있으면 파일명·업체·건수와 빼기(x) 단추
        fileSummary.Visibility(Shown(files.empty()));
        dispatchFiles.Children().Clear();
        Swatch* fileColors[]{ &green, &blue, &purple, &orange };
        auto first = true;
        for (auto const& file : files)
        {
            Button remove;
            remove.Content(Icon(L"", 11));
            remove.Padding(Th(7));
            remove.Background(transparent);
            remove.BorderThickness(Th(0));
            remove.VerticalAlignment(VerticalAlignment::Center);
            remove.Click([this, file](auto&&, auto&&)
            {
                if (importingDispatch) return;
                // 눌린 단추가 목록과 함께 사라지니, 처리가 끝난 뒤에 지워요.
                queue.TryEnqueue([this, file]
                {
                    std::erase(files, file);
                    RenderDispatchFiles();
                    Guard([this] { InvalidateDispatch(); });
                });
            });
            Name(remove, Str(file, L"name") + L" 빼기");
            Tip(remove, L"빼기");
            auto records = Objects(Arr(file, L"records"));
            std::vector<std::wstring> vendors;
            for (auto const& record : records)
            {
                auto vendor = VendorName(Str(record, L"vendor"));
                if (!vendor.empty() && std::find(vendors.begin(), vendors.end(), vendor) == vendors.end()) vendors.push_back(vendor);
            }
            auto name = Text(Str(file, L"name"), 13);
            name.TextWrapping(TextWrapping::NoWrap);
            name.TextTrimming(TextTrimming::CharacterEllipsis);
            Tip(name, Str(file, L"name"));
            auto vendorText = Join(vendors, L" · ");
            auto info = Stack({ name, Muted((vendorText.empty() ? L"" : vendorText + L" · ") + N0(static_cast<double>(records.size())) + L"건") });
            info.Spacing(0);
            auto warnings = Strings(file.TryLookup(L"warnings"));
            if (!warnings.empty())
            {
                auto warning = Text(L"⚠ 양식 확인 필요 · " + std::to_wstring(warnings.size()) + L"건", 12);
                warning.Foreground(orange.Solid);
                Tip(warning, Join(warnings, L"\r\n"));
                info.Children().Append(warning);
            }
            // 파일 아이콘: 24px 색 타일 (녹색·파랑·purple·주황 순환)
            auto icon = Tile(*fileColors[dispatchFiles.Children().Size() % 4], L"", 24, 13, 6);
            auto line = Columns({ 24, 0, 32 }, { icon, info, remove });
            line.Padding(Th(0, 6, 0, 6));
            line.ColumnSpacing(8);
            Border border;
            border.Child(line);
            border.BorderBrush(divider);
            border.BorderThickness(Th(0, first ? 0 : 1, 0, 0));
            dispatchFiles.Children().Append(border);
            first = false;
        }
    }

    void MainWindow::InvalidateDispatch()
    {
        if (results.empty()) return;
        results.clear();
        selected.clear();
        ClearSlackReplies();
        ShowSlackPanel(false);
        RecountDispatch();
        RenderDispatch();
    }

    Async MainWindow::ResetDispatch()
    {
        if (importingDispatch) co_return;
        if (!files.empty() || !vehicles.Text().empty())
        {
            auto dialog = Dialog(L"탁송체크 전체 초기화", Text(L"차량목록과 첨부 파일을 모두 비울까요?"), L"초기화");
            if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        }
        files.clear();
        completed.clear();
        copiedDispatch.clear();
        SaveWorkMarks();
        vehicles.Text(L"");
        search.Text(L"");
        dispatchVendor = dispatchState = dispatchSort = L"";
        dispatchSearches.clear();
        dispatchFrom = dispatchTo = nullptr;
        dispatchDescending = false;
        scope.SelectedIndex(0);
        dispatchFilter = L"all";
        InvalidateDispatch();
        RenderDispatchFiles();
        RecountDispatch();
        RenderDispatch();
        Go(L"dispatch");
    }

    std::vector<JObj> MainWindow::SelectedOrVisibleDispatch()
    {
        if (selected.empty()) return VisibleResults();
        std::vector<JObj> rows;
        for (auto const& row : results)
            if (selected.contains(Str(row, L"id"))) rows.push_back(row);
        return rows;
    }

    void MainWindow::CopyDispatchNumbers()
    {
        std::vector<JObj> rows;
        std::set<std::wstring> seen;
        for (auto const& row : SelectedOrVisibleDispatch())
            if (seen.insert(Str(row, L"plate")).second) rows.push_back(row);
        if (rows.empty()) { Notify(L"현재 목록에 복사할 차량이 없어요.", true); return; }
        Action markCopied = [this, seen]
        {
            copiedDispatch.insert(seen.begin(), seen.end());
            SaveWorkMarks();
            RenderDispatch();
        };
        auto plates = [&rows](std::optional<bool> jeju)
        {
            std::vector<std::wstring> list;
            for (auto const& row : rows)
                if (!jeju || Flag(row, L"jeju") == *jeju) list.push_back(Str(row, L"plate"));
            return list;
        };
        if (dispatchFilter != L"slack")
        {
            Copy(Join(plates(std::nullopt), L"\r\n"), markCopied);
            return;
        }
        // 슬랙봇 문의는 제주 출발과 일반 차량을 나눠서 적어요.
        std::vector<std::wstring> sections;
        for (auto const& [jeju, title] : std::vector<std::pair<bool, wchar_t const*>>{ { true, L"제주도출발차량" }, { false, L"일반차량" } })
            if (auto list = plates(jeju); !list.empty()) sections.push_back(L"[" + std::wstring(title) + L"]\r\n" + Join(list, L"\r\n"));
        Copy(Join(sections, L"\r\n\r\n"), markCopied);
    }

    static int DayNumber(DateTime const& time)
    {
        auto seconds = winrt::clock::to_time_t(time);
        tm local{};
        localtime_s(&local, &seconds);
        return (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
    }

    Async MainWindow::EditDispatchFilters()
    {
        auto options = [this](wchar_t const* label, wchar_t const* key, std::wstring const& current)
        {
            std::vector<std::wstring> values;
            for (auto const& row : results) values.push_back(Str(row, key));
            auto items = DistinctSorted(values);
            items.insert(items.begin(), L"전체");
            ComboBox box;
            box.Header(box_value(label));
            SetItems(box, items);
            auto found = std::find(items.begin(), items.end(), current);
            box.SelectedIndex(found == items.end() ? 0 : static_cast<int>(found - items.begin()));
            return box;
        };
        auto vendor = options(L"업체", L"vendor", dispatchVendor);
        auto state = options(L"원본 상태", L"rawState", dispatchState);
        CalendarDatePicker from, to;
        from.Header(box_value(L"탁송일정 시작일"));
        from.Date(dispatchFrom);
        to.Header(box_value(L"탁송일정 종료일"));
        to.Date(dispatchTo);
        std::vector<TextBox> searches;
        for (auto const& field : DispatchFields)
        {
            auto box = Input(field.label, L"검색어");
            if (auto found = dispatchSearches.find(field.key); found != dispatchSearches.end()) box.Text(found->second);
            searches.push_back(box);
        }
        auto columnInputs = Stack();
        for (size_t i = 0; i < searches.size(); i += 2)
            columnInputs.Children().Append(i + 1 < searches.size() ? Columns({ 0, 0 }, { searches[i], searches[i + 1] }) : Columns({ 0 }, { searches[i] }));
        std::vector<std::wstring> sortFields{ L"입력 순서" };
        int sortIndex = 0;
        for (size_t i = 0; i < DispatchFields.size(); i++)
        {
            sortFields.emplace_back(DispatchFields[i].label);
            if (dispatchSort == DispatchFields[i].key) sortIndex = static_cast<int>(i) + 1;
        }
        ComboBox sort;
        sort.Header(box_value(L"정렬 기준"));
        SetItems(sort, sortFields);
        sort.SelectedIndex(sortIndex);
        CheckBox descending;
        descending.Content(box_value(L"내림차순"));
        descending.IsChecked(dispatchDescending);
        Expander columnFilters;
        columnFilters.Header(box_value(L"열별 검색"));
        columnFilters.Content(columnInputs);
        columnFilters.IsExpanded(!dispatchSearches.empty());
        columnFilters.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        ScrollViewer body;
        body.Content(Stack({ Bar({ vendor, state }), Bar({ from, to }), Bar({ sort, descending }), columnFilters }));
        body.MaxHeight(460);
        auto dialog = Dialog(L"탁송 결과 필터·정렬", body, L"적용");
        dialog.SecondaryButtonText(L"초기화");
        auto result = co_await dialog.ShowAsync();
        if (result == ContentDialogResult::None) co_return;
        if (result == ContentDialogResult::Secondary)
        {
            dispatchVendor = dispatchState = dispatchSort = L"";
            dispatchSearches.clear();
            dispatchFrom = dispatchTo = nullptr;
            dispatchDescending = false;
        }
        else
        {
            if (from.Date() && to.Date() && DayNumber(from.Date().Value()) > DayNumber(to.Date().Value()))
            {
                Notify(L"종료일은 시작일과 같거나 이후여야 해요.", true);
                co_return;
            }
            dispatchVendor = vendor.SelectedIndex() > 0 ? SelectedText(vendor) : L"";
            dispatchState = state.SelectedIndex() > 0 ? SelectedText(state) : L"";
            dispatchFrom = from.Date();
            dispatchTo = to.Date();
            dispatchSearches.clear();
            for (size_t i = 0; i < searches.size(); i++)
                if (auto query = Trim(searches[i].Text()); !query.empty()) dispatchSearches[DispatchFields[i].key] = query;
            dispatchSort = sort.SelectedIndex() > 0 ? DispatchFields[sort.SelectedIndex() - 1].key : L"";
            dispatchDescending = IsOn(descending);
        }
        dispatchPage = 0;
        SyncDispatchBoxes();
        RenderDispatch();
    }

    bool MainWindow::MatchesDispatchOptions(JObj const& row, Rules::Described const* described)
    {
        if (!dispatchVendor.empty() && Str(row, L"vendor") != dispatchVendor) return false;
        if (!dispatchState.empty() && Str(row, L"rawState") != dispatchState) return false;
        for (auto const& [key, query] : dispatchSearches)
        {
            auto value = key == L"status" ? (described ? described->label : L"") : key == L"notes" ? (described ? described->notes : L"") : Str(row, key);
            if (!ContainsIgnoreCase(value, query)) return false;
        }
        if (!dispatchFrom && !dispatchTo) return true;
        // 탁송일정 앞 10글자(yyyy-MM-dd)로 날짜를 봐요.
        auto schedule = Str(row, L"schedule");
        if (schedule.size() < 10 || schedule[4] != L'-' || schedule[7] != L'-') return false;
        int day = 0;
        for (auto index : { 0, 1, 2, 3, 5, 6, 8, 9 })
        {
            if (!iswdigit(schedule[index])) return false;
            day = day * 10 + (schedule[index] - L'0');
        }
        return (!dispatchFrom || day >= DayNumber(dispatchFrom.Value())) && (!dispatchTo || day <= DayNumber(dispatchTo.Value()));
    }

    // ───────────── 슬랙봇 회신 (오른쪽 패널): 회신 붙여넣기 → 반영, 아래에 확인 대상 목록 ─────────────
    FrameworkElement MainWindow::CreateSlackDispatchStage()
    {
        slackReplies = TextBox();
        slackReplies.PlaceholderText(L"08구8701 - X\n77다1234 - 12,350원\n12가3456 - 확인 중");
        slackReplies.AcceptsReturn(true);
        slackReplies.TextWrapping(TextWrapping::Wrap);
        slackReplies.Height(170);
        slackReplies.FontFamily(AppFont(L"MonoFont"));
        slackReplies.FontSize(13);
        Name(slackReplies, L"슬랙봇 확인 내용");
        slackReplyReport = Text(L"", 13);
        slackReplyReport.Visibility(Visibility::Collapsed);
        slackDispatchSummary = Muted(L"", 13);
        slackDispatchList = ListView();
        slackDispatchList.SelectionMode(ListViewSelectionMode::None);
        slackDispatchList.IsItemClickEnabled(false);
        ScrollViewer::SetVerticalScrollBarVisibility(slackDispatchList, ScrollBarVisibility::Disabled);
        ScrollViewer::SetVerticalScrollMode(slackDispatchList, ScrollMode::Disabled);
        auto pagerButton = [](wchar_t const* glyph, wchar_t const* name)
        {
            Button button;
            button.Content(Icon(glyph, 12));
            button.Padding(Th(8, 5, 8, 5));
            Name(button, name);
            return button;
        };
        previousSlackPage = pagerButton(L"", L"이전 페이지");
        nextSlackPage = pagerButton(L"", L"다음 페이지");
        previousSlackPage.Click([this](auto&&, auto&&) { slackPage--; RefreshSlackDispatchList(); });
        nextSlackPage.Click([this](auto&&, auto&&) { slackPage++; RefreshSlackDispatchList(); });
        slackPageLabel = Muted(L"", 13);
        slackPageLabel.VerticalAlignment(VerticalAlignment::Center);
        showSlackResults = MakeButton(L"반영된 차량 표에서 보기", [this] { Guard([this] { ShowSlackResults(); }); });
        Button back;
        back.Content(Icon(L"", 14));
        back.Background(transparent);
        back.BorderThickness(Th(0));
        back.Padding(Th(6));
        Tip(back, L"입력으로 돌아가기");
        Name(back, L"입력으로 돌아가기");
        back.Click([this](auto&&, auto&&) { ShowSlackPanel(false); });
        auto head = Bar({ back, ColorHeading(L"슬랙봇 회신", purple, L"", 20, 22) });
        head.VerticalAlignment(VerticalAlignment::Center);
        auto apply = MakeButton(L"확인 내용 반영", [this] { Guard([this] { ApplySlackReplies(); }); }, true);
        apply.HorizontalAlignment(HorizontalAlignment::Stretch);
        HyperlinkButton copyOutstanding;
        copyOutstanding.Content(box_value(L"미확인 차량번호 복사"));
        copyOutstanding.Padding(Th(0, 4, 0, 4));
        copyOutstanding.Click([this](auto&&, auto&&) { CopyOutstandingSlackNumbers(); });
        auto entry = Stack({ ColorHeading(L"회신 붙여넣기", purple, L""),
            Muted(L"한 줄에 ‘차량번호 - 값’ 형식이에요. 금액은 10원 단위로 반올림하고, X는 0원, 확인 중은 그대로 반영돼요.", 12),
            slackReplies, apply, showSlackResults, slackReplyReport });
        entry.Spacing(8);
        auto targets = Stack({ ColorHeading(L"확인 대상", pink, L""), slackDispatchSummary, copyOutstanding, slackDispatchList,
            Bar({ previousSlackPage, slackPageLabel, nextSlackPage }) });
        targets.Spacing(6);
        auto separator = Divider();
        separator.Margin(Th(-20, 6, -20, 6));
        auto body = Stack({ head, entry, separator, targets });
        body.Spacing(12);
        body.Padding(Th(20, 16, 20, 20));
        return VerticalScroll(body);
    }

    void MainWindow::RefreshSlackDispatchList()
    {
        std::vector<JObj> eligible;
        std::set<std::wstring> seen;
        for (auto const& row : results)
            if (Flag(row, L"cInquiry") && seen.insert(Str(row, L"plate")).second) eligible.push_back(row);
        auto total = static_cast<int>(eligible.size());
        auto unanswered = static_cast<int>(std::count_if(eligible.begin(), eligible.end(), [](auto const& row) { return !Flag(row, L"slackReviewed"); }));
        auto pending = static_cast<int>(std::count_if(eligible.begin(), eligible.end(), [](auto const& row) { return Flag(row, L"slackPending"); }));
        showSlackResults.IsEnabled(total > unanswered);
        slackDispatchSummary.Text(total == 0
            ? std::wstring(L"이번 조회에는 슬랙봇 확인 대상이 없어요.")
            : L"대상 " + N0(total) + L"대 · 금액 확인 " + N0(total - unanswered - pending) + L"대 · 확인 중 " + N0(pending) + L"대 · 미입력 " + N0(unanswered) + L"대");
        auto pageCount = std::max(1, (total + SlackPageSize - 1) / SlackPageSize);
        slackPage = std::clamp(slackPage, 0, pageCount - 1);
        previousSlackPage.IsEnabled(slackPage > 0);
        nextSlackPage.IsEnabled(slackPage + 1 < pageCount);
        slackPageLabel.Text(std::to_wstring(slackPage + 1) + L"/" + std::to_wstring(pageCount) + L"페이지");
        auto items = single_threaded_vector<IInspectable>();
        for (int i = slackPage * SlackPageSize; i < total && i < (slackPage + 1) * SlackPageSize; i++)
        {
            auto const& row = eligible[i];
            auto value = !Flag(row, L"slackReviewed") ? L"미입력" : Flag(row, L"slackPending") ? std::wstring(Rules::SlackPending) : Str(row, L"amount") + L"원";
            items.Append(box_value(hstring(Str(row, L"plate") + L"   ·   " + value)));
        }
        slackDispatchList.ItemsSource(items);
    }

    void MainWindow::ApplySlackReplies()
    {
        if (Trim(slackReplies.Text()).empty())
        {
            Notify(L"슬랙봇 회신을 붙여넣어 주세요.", true);
            return;
        }
        auto result = rules.ApplySlackReplies(NormalizeLines(slackReplies.Text()), results);
        std::vector<std::wstring> report{ N0(result.applied) + L"대 반영 · 확인 필요 " + N0(static_cast<double>(result.issues.size())) + L"건" };
        for (size_t i = 0; i < result.issues.size() && i < 8; i++) report.push_back(result.issues[i]);
        if (result.issues.size() > 8) report.push_back(L"외 " + N0(static_cast<double>(result.issues.size() - 8)) + L"건");
        slackReplyReport.Text(Join(report, L"\r\n"));
        slackReplyReport.Foreground(result.issues.empty() ? green.Solid : orange.Solid);
        slackReplyReport.Visibility(Visibility::Visible);
        RecountDispatch();
        RefreshSlackDispatchList();
        if (result.applied > 0 && result.issues.empty())
        {
            ShowSlackResults();
            Notify(L"슬랙봇 회신 " + N0(result.applied) + L"대를 표에 반영했어요.");
        }
        else RenderDispatch();
    }

    void MainWindow::ShowSlackResults()
    {
        // 이 보기는 앞서 건 필터에 가려진 줄까지 반영된 차량을 모두 보여 줘요.
        dispatchVendor = dispatchState = L"";
        dispatchFrom = dispatchTo = nullptr;
        dispatchSearches.clear();
        search.Text(L"");
        dispatchPage = 0;
        SyncDispatchBoxes();
        dispatchFilter = L"slack-replies";
        RenderDispatch();
        Go(L"dispatch");
    }

    void MainWindow::CopyOutstandingSlackNumbers()
    {
        std::vector<std::wstring> plates;
        for (auto const& row : results)
        {
            if (!Flag(row, L"cInquiry") || (Flag(row, L"slackReviewed") && !Flag(row, L"slackPending"))) continue;
            auto plate = Str(row, L"plate");
            if (std::find(plates.begin(), plates.end(), plate) == plates.end()) plates.push_back(plate);
        }
        if (plates.empty()) { Notify(L"미확인 차량이 없어요."); return; }
        Copy(Join(plates, L"\r\n"));
    }

    void MainWindow::ClearSlackReplies()
    {
        slackReplies.Text(L"");
        slackReplyReport.Visibility(Visibility::Collapsed);
    }

    // ───────────── 결과 캡처 (PNG·클립보드·PDF) ─────────────

    // 기기 안에서 그린 표 이미지를 A4 가로 쪽에 나눠 담아요. 바깥 렌더러나 서비스를 쓰지 않아요.
    static std::string ImagePdf(std::string const& jpeg, int width, int height)
    {
        std::string output;
        std::vector<size_t> offsets;
        auto number = [](double value)
        {
            char buffer[32];
            snprintf(buffer, sizeof buffer, "%.3f", value);
            std::string text(buffer);
            text.erase(text.find_last_not_of('0') + 1);
            if (text.back() == '.') text.pop_back();
            return text;
        };
        auto object = [&](int id, std::string const& content)
        {
            offsets.push_back(output.size());
            output += std::to_string(id) + " 0 obj\n" + content + "\nendobj\n";
        };
        constexpr double pageW = 842, pageH = 595, margin = 20;
        auto scale = (pageW - margin * 2) / width;
        auto imageH = height * scale;
        auto pageCount = static_cast<int>(std::ceil(imageH / (pageH - margin * 2)));
        output += "%PDF-1.4\n";
        object(1, "<< /Type /Catalog /Pages 2 0 R >>");
        std::string kids;
        for (int i = 0; i < pageCount; i++) kids += (i ? " " : "") + std::to_string(4 + i * 2) + " 0 R";
        object(2, "<< /Type /Pages /Count " + std::to_string(pageCount) + " /Kids [" + kids + "] >>");
        offsets.push_back(output.size());
        output += "3 0 obj\n<< /Type /XObject /Subtype /Image /Width " + std::to_string(width) + " /Height " + std::to_string(height)
            + " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " + std::to_string(jpeg.size()) + " >>\nstream\n";
        output += jpeg;
        output += "\nendstream\nendobj\n";
        for (int i = 0; i < pageCount; i++)
        {
            auto id = 4 + i * 2;
            object(id, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 842 595] /Resources << /XObject << /Im0 3 0 R >> >> /Contents " + std::to_string(id + 1) + " 0 R >>");
            auto command = "q\n20 20 802 555 re W n\n802 0 0 " + number(imageH) + " 20 " + number(pageH - margin - imageH + i * (pageH - margin * 2)) + " cm\n/Im0 Do\nQ\n";
            object(id + 1, "<< /Length " + std::to_string(command.size()) + " >>\nstream\n" + command + "endstream");
        }
        auto xref = output.size();
        output += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
        for (auto offset : offsets)
        {
            char line[32];
            snprintf(line, sizeof line, "%010zu 00000 n \n", offset);
            output += line;
        }
        output += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
        return output;
    }

    Async MainWindow::CaptureDispatch(bool clipboard, bool pdf)
    {
        auto rows = ExportScope();
        if (rows.size() > 300) throw AppError{ L"캡처는 한 번에 300대까지 가능해요. 저장 범위를 줄여 주세요." };
        auto described = rules.Describe(rows);
        Media::SolidColorBrush white(winrt::Microsoft::UI::Colors::White()), black(winrt::Microsoft::UI::Colors::Black()),
            headBrush(Windows::UI::Color{ 255, 231, 237, 245 });
        StackPanel panel;
        panel.Width(1380);
        panel.Background(white);
        panel.Padding(Th(20));
        panel.Spacing(0);
        auto count = N0(static_cast<double>(rows.size())) + L"대";
        auto title = Text(L"WorkTool 탁송체크 · " + count + L" · " + Now(L"%Y-%m-%d %H:%M"), 22);
        title.Foreground(black);
        title.Margin(Th(0, 0, 0, 14));
        panel.Children().Append(title);
        auto addRow = [&](std::vector<std::wstring> const& cells, bool heading)
        {
            static double const widths[]{ 130, 180, 150, 150, 120, 180, 190, 220 };
            Grid grid;
            grid.Height(32);
            grid.Background(heading ? headBrush : white);
            for (size_t j = 0; j < cells.size(); j++)
            {
                AddColumn(grid, Px(widths[j]));
                TextBlock cell;
                cell.Text(cells[j]);
                cell.FontSize(13);
                cell.Margin(Th(6, 6, 0, 0));
                cell.Foreground(black);
                cell.TextTrimming(TextTrimming::CharacterEllipsis);
                Place(grid, cell, 0, static_cast<int>(j));
            }
            panel.Children().Append(grid);
        };
        addRow({ L"차량번호", L"탁송일정", L"주유/충전비", L"탁송비", L"부가세", L"조회 상태", L"업체", L"메모" }, true);
        for (size_t i = 0; i < rows.size(); i++)
            addRow({ Str(rows[i], L"plate"), Str(rows[i], L"schedule"), Money(rows[i], L"amount"), Money(rows[i], L"dispatchFee"), Money(rows[i], L"vat"),
                described[i].label, Str(rows[i], L"vendor"), described[i].notes }, false);
        addRow({ L"합계", count, N0(Sum(rows, L"amount")) + L"원", N0(Sum(rows, L"dispatchFee")) + L"원", N0(Sum(rows, L"vat")) + L"원", L"", L"", L"" }, true);
        // 화면 밖에 잠깐 붙여 그린 뒤 떼어 내요.
        Canvas canvas;
        canvas.Width(1);
        canvas.Height(1);
        canvas.IsHitTestVisible(false);
        Canvas::SetLeft(panel, -20000);
        canvas.Children().Append(panel);
        root.Children().Append(canvas);
        auto detach = [this, canvas]
        {
            uint32_t index;
            if (root.Children().IndexOf(canvas, index)) root.Children().RemoveAt(index);
        };
        std::wstring failure;
        try
        {
            panel.Measure({ 1380, std::numeric_limits<float>::infinity() });
            panel.Arrange({ 0, 0, 1380, panel.DesiredSize().Height });
            panel.UpdateLayout();
            co_await Delay(50);
            Media::Imaging::RenderTargetBitmap bitmap;
            co_await bitmap.RenderAsync(panel, 1380, static_cast<int>(panel.ActualHeight()));
            auto pixels = co_await bitmap.GetPixelsAsync();
            InMemoryRandomAccessStream stream;
            auto encoder = co_await BitmapEncoder::CreateAsync(pdf ? BitmapEncoder::JpegEncoderId() : BitmapEncoder::PngEncoderId(), stream);
            encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore, static_cast<uint32_t>(bitmap.PixelWidth()),
                static_cast<uint32_t>(bitmap.PixelHeight()), 96, 96, array_view<uint8_t const>(pixels.data(), pixels.Length()));
            co_await encoder.FlushAsync();
            stream.Seek(0);
            if (clipboard)
            {
                Windows::ApplicationModel::DataTransfer::DataPackage data;
                data.SetBitmap(RandomAccessStreamReference::CreateFromStream(stream));
                Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(data);
                Windows::ApplicationModel::DataTransfer::Clipboard::Flush();
                Notify(L"결과 캡처를 클립보드에 복사했어요.");
            }
            else
            {
                std::wstring path(co_await SavePicker(L"탁송체크_" + Now(L"%Y%m%d_%H%M"), pdf ? L".pdf" : L".png"));
                if (!path.empty())
                {
                    auto size = static_cast<uint32_t>(stream.Size());
                    DataReader reader(stream.GetInputStreamAt(0));
                    co_await reader.LoadAsync(size);
                    std::string bytes(size, '\0');
                    reader.ReadBytes(array_view<uint8_t>(reinterpret_cast<uint8_t*>(bytes.data()), size));
                    if (pdf) bytes = ImagePdf(bytes, bitmap.PixelWidth(), bitmap.PixelHeight());
                    if (!WriteFile(path, bytes)) throw AppError{ L"파일을 저장하지 못했어요. 저장 위치와 권한을 확인해 주세요." };
                    Notify(L"조회 결과를 저장했어요.");
                }
            }
        }
        catch (hresult_error const& error) { failure = error.message(); if (failure.empty()) failure = L"캡처하지 못했어요."; }
        detach();
        if (!failure.empty()) throw AppError{ failure };
    }
}
