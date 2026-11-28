#include "pch.h"
#include "MainWindow.h"

// 하나은행 이체파일: 단계 표시줄 대신 탐색 창 하위 항목(1·2·3단계)으로 오가요.
// 단계마다 CommandBar와 녹색 주 단추, 오른쪽 정보 패널이 바뀌어요.

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace wt
{
    static wchar_t const SentTodayFile[] = L"transfer-sent-today.json";

    FrameworkElement MainWindow::TransferPage()
    {
        LoadTransferWidths();
        threadInputs = StackPanel();
        threadInputs.Spacing(0);
        AddThreadInput(true);
        transferSummary = Muted(L"이체 요청을 붙여넣고 추출해 주세요.");
        transferSummary.VerticalAlignment(VerticalAlignment::Center);
        transferRowCount = Muted(L"");
        transferRowCount.VerticalAlignment(VerticalAlignment::Center);
        transferList = StackPanel();
        acknowledge = CheckBox();
        acknowledge.Content(Text(L"검토 항목과 원문을 대조했고, 중복 송금 여부를 확인했어요.", 13));
        acknowledge.Checked([this](auto&&, auto&&) { acknowledgedSignature = Signature(); Guard([this] { ValidateTransfers(); }); });
        acknowledge.Unchecked([this](auto&&, auto&&) { acknowledgedSignature.clear(); Guard([this] { ValidateTransfers(); }); });
        download = AsyncButton(L"이체파일 다운로드", [this] { return SaveTransfer(); }, true);
        download.HorizontalAlignment(HorizontalAlignment::Stretch);
        download.Padding(Th(12, 8, 12, 8));
        previewRows = StackPanel();
        commentList = StackPanel();

        // ── CommandBar (단계별) ──
        auto bar = [this](std::vector<AppBarButton> const& commands)
        {
            CommandBar commandBar;
            commandBar.DefaultLabelPosition(CommandBarDefaultLabelPosition::Right);
            commandBar.Background(transparent);
            commandBar.HorizontalAlignment(HorizontalAlignment::Left);
            commandBar.IsDynamicOverflowEnabled(true);
            for (auto const& command : commands) commandBar.PrimaryCommands().Append(command);
            commandBar.SecondaryCommands().Append(CommandAsync(L"처음부터", L"", [this] { return ResetTransfers(); }, &purple));
            commandBar.SecondaryCommands().Append(CommandAsync(L"단축키", L"", [this] { return ShowTransferShortcuts(); }));
            return commandBar;
        };
        transferCommands1 = bar({ Command(L"입력칸 추가", L"", [this] { AddThreadInput().Focus(FocusState::Programmatic); }, &green) });
        transferCommands2 = bar({ Command(L"행 추가", L"", [this] { AddTransfer(); }, &green),
            CommandAsync(L"선택 삭제", L"", [this] { return DeleteSelectedTransfers(); }, &red),
            Command(L"표시 내역 선택", L"", [this] { SelectVisibleTransfers(); }), Command(L"1억 초과 분할", L"", [this] { SplitTransfers(); }),
            Command(L"필터 지우기", L"", [this] { ClearTransferFilters(); }), CommandAsync(L"열 너비", L"", [this] { return EditTransferWidths(); }) });
        transferCommands3 = bar({ Command(L"2단계에서 고치기", L"", [this] { Step(2); }, &orange),
            CommandAsync(L"코멘트 목록", L"", [this] { return ShowTransferComments(); }) });
        extractButton = MakeButton(L"이체정보 추출하기", [this] { ExtractTransfer(); }, true);
        nextStepButton = MakeButton(L"다운로드 단계로 →", [this] { Guard([this] { Step(3); }); }, true);
        downloadCommand = MakeButton(L"이체파일 다운로드", [this] { Run([this] { return SaveTransfer(); }); }, true);
        for (auto const& button : { extractButton, nextStepButton, downloadCommand })
        {
            Tip(button, L"Ctrl+Enter");
            button.VerticalAlignment(VerticalAlignment::Center);
        }
        transferPanelToggle = PanelToggle(L"정보 패널", [this]() -> FrameworkElement { return transferPanel; });
        Grid commandsLeft;
        commandsLeft.Children().Append(transferCommands1);
        commandsLeft.Children().Append(transferCommands2);
        commandsLeft.Children().Append(transferCommands3);
        Grid commandRow;
        commandRow.Margin(Th(16, 0, 16, 4));
        AddColumn(commandRow, Star());
        AddColumn(commandRow, AutoSize());
        commandRow.Children().Append(commandsLeft);
        auto primary = Bar({ extractButton, nextStepButton, downloadCommand, transferPanelToggle });
        primary.VerticalAlignment(VerticalAlignment::Center);
        Place(commandRow, primary, 0, 1);

        // ── 1단계 ──
        transferInput = Grid();
        transferInput.BorderBrush(divider);
        transferInput.BorderThickness(Th(0, 1, 0, 0));
        auto threadsBody = Stack({ threadInputs, Muted(L"같은 요청을 다시 추출하면 이미 가져온 건은 건너뛰어요. 새 스레드를 붙여넣어 추가할 수 있어요.", 12) });
        threadsBody.Padding(Th(24, 16, 24, 16));
        transferInput.Children().Append(VerticalScroll(threadsBody));

        // ── 2단계: 보기(전체·정상·검토 필요·오류) + 자동 보정 + 총 입금액, 편집 가능한 표 ──
        autoFixAccounts = CheckBox();
        autoFixAccounts.Content(Text(L"계좌번호 자동 제거(하이픈·공백)", 13));
        autoFixAccounts.IsChecked(true);
        autoFixAccounts.VerticalAlignment(VerticalAlignment::Center);
        transferSum = Text(L"", 14);
        transferSum.VerticalAlignment(VerticalAlignment::Center);
        Grid reviewTop;
        reviewTop.Margin(Th(20, 0, 24, 6));
        reviewTop.ColumnSpacing(16);
        AddColumn(reviewTop, Star());
        AddColumn(reviewTop, AutoSize());
        auto filters = TransferFilters();
        filters.Children().Append(autoFixAccounts);
        reviewTop.Children().Append(filters);
        Place(reviewTop, transferSum, 0, 1);
        transferHeaderHost = Grid();
        Grid table;
        AddRow(table, AutoSize());
        AddRow(table, Star());
        table.Children().Append(transferHeaderHost);
        ScrollViewer rows;
        rows.Content(transferList);
        rows.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        rows.HorizontalScrollMode(ScrollMode::Disabled);
        Place(table, rows, 1);
        auto tableScroll = Horizontal(table);
        tableScroll.Padding(Th(12, 0, 0, 0));
        transferReview = Grid();
        AddRow(transferReview, AutoSize());
        AddRow(transferReview, Star());
        transferReview.Children().Append(reviewTop);
        Grid tableHost;
        tableHost.BorderBrush(divider);
        tableHost.BorderThickness(Th(0, 1, 0, 0));
        tableHost.Children().Append(tableScroll);
        Place(transferReview, tableHost, 1);

        // ── 3단계: 요약 한 줄 + 파일 미리보기 ──
        summary3 = Text(L"", 14);
        summary3.Margin(Th(24, 0, 24, 10));
        auto previewTable = Stack({ TableHeader(TransferColumns, { L"입금은행", L"입금계좌번호", L"입금액", L"출금통장 표시", L"입금통장 표시", L"CMS코드" }), previewRows });
        previewTable.Spacing(0);
        previewTable.MinWidth(790);
        transferDownload = Grid();
        AddRow(transferDownload, AutoSize());
        AddRow(transferDownload, Star());
        transferDownload.Children().Append(summary3);
        Grid previewHost;
        previewHost.BorderBrush(divider);
        previewHost.BorderThickness(Th(0, 1, 0, 0));
        auto previewScroll = Scroll(previewTable);
        previewScroll.Padding(Th(12, 0, 0, 0));
        previewHost.Children().Append(previewScroll);
        Place(transferDownload, previewHost, 1);

        // ── 오른쪽 정보 패널 ──
        cmsOnExtract = Input(L"CMS코드 (선택)", L"비워 둬도 돼요");
        transferPanel1 = PanelSections({
            Stack({ ColorHeading(L"추출 설정", blue, L"", 16, 20), ColorHeading(L"CMS코드", indigo, L""), Muted(L"추출하는 모든 건에 같은 코드를 넣어요.", 12), cmsOnExtract }),
            Stack({ ColorHeading(L"이렇게 추출해요", purple, L""), Muted(L"1. Slack 이체 요청 스레드를 그대로 붙여넣어요.\n2. 스레드가 여러 개면 ‘입력칸 추가’로 나눠 넣어요.\n3. 이체정보 추출하기(Ctrl+Enter)를 누르면 은행·계좌번호·금액·차량번호를 뽑아 2단계로 가요.\n\n넣은 내용은 이 앱 안에서만 처리하고 서버로 보내지 않아요.", 13) }) });
        auto detailHeading = ColorHeading(L"선택한 건 원문", blue, L"");
        detailTitle = detailHeading.Children().GetAt(1).as<TextBlock>();
        detailSource = TextBlock();
        detailSource.Text(L"표에서 줄을 누르면 Slack 원문이 여기에 보여요.");
        detailSource.TextWrapping(TextWrapping::Wrap);
        detailSource.FontSize(12);
        detailSource.Foreground(mutedInk);
        detailSource.FontFamily(AppFont(L"MonoFont"));
        detailSource.IsTextSelectionEnabled(true);
        detailIssues = StackPanel();
        detailIssues.Spacing(4);
        ScrollViewer sourceScroll;
        sourceScroll.Content(detailSource);
        sourceScroll.MaxHeight(200);
        sourceScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        transferPanel2 = PanelSections({
            Stack({ detailHeading, sourceScroll }),
            Stack({ ColorHeading(L"확인 사항", orange, L""), detailIssues }),
            Stack({ ColorHeading(L"통장 메모·CMS 일괄 입력", teal, L""), MemoControls() }) });
        HyperlinkButton copyAll;
        copyAll.Content(box_value(L"모두 복사"));
        copyAll.Padding(Th(0));
        copyAll.Click([this](auto&&, auto&&) { Guard([this] { Copy(Join(rules.Comments(transfers), L"\r\n")); }); });
        Tip(copyAll, L"Alt+Shift+C");
        auto commentHead = Columns({ 0, 0 }, { ColorHeading(L"송금 코멘트", purple, L""), copyAll });
        commentHead.Padding(Th(0));
        copyAll.HorizontalAlignment(HorizontalAlignment::Right);
        auto notReal = Text(L"이체파일 생성은 실제 송금이 아니에요. 은행에 올리기 전 받는 분·계좌번호·금액을 다시 확인해 주세요.", 12);
        notReal.Foreground(orange.Solid);
        auto ack = Stack({ PanelHeading(L"검토 확인"), acknowledge });
        ack.Spacing(6);
        ackSection = ack;
        transferPanel3 = PanelSections({
            Stack({ ColorHeading(L"이체파일 받기", green, L""), ack, download, Muted(L"하나은행 기업뱅킹 [다계좌이체] 업로드용 .xlsx", 12), notReal }),
            Stack({ commentHead, Muted(L"송금을 마친 뒤 Slack에 남길 문구예요.", 12), commentList }) });
        Grid panelHost;
        panelHost.Children().Append(transferPanel1);
        panelHost.Children().Append(transferPanel2);
        panelHost.Children().Append(transferPanel3);
        transferPanel = SidePanel(VerticalScroll(panelHost));

        // ── 배치 ──
        transferSubtitle = Text(L"1단계 · 이체 요청 입력");
        Grid host;
        host.Children().Append(transferInput);
        host.Children().Append(transferReview);
        host.Children().Append(transferDownload);
        Grid main;
        for (int i = 0; i < 4; i++) AddRow(main, i == 2 ? Star() : AutoSize());
        main.Children().Append(PageTitle(L"하나은행 이체파일", transferSubtitle));
        Place(main, commandRow, 1);
        Place(main, host, 2);
        Place(main, StatusLine(transferSummary, transferRowCount), 3);
        Grid page;
        AddColumn(page, Star(), 420);
        AddColumn(page, AutoSize());
        page.Children().Append(main);
        Place(page, transferPanel, 0, 1);
        BuildTransferHeader();
        Step(1);
        AddTransferShortcuts(page);
        return page;
    }

    /// 패널 안 구역들: 구분선으로만 나눠요.
    StackPanel MainWindow::PanelSections(std::vector<StackPanel> const& sections)
    {
        StackPanel panel;
        for (size_t i = 0; i < sections.size(); i++)
        {
            sections[i].Spacing(8);
            Border border;
            border.Child(sections[i]);
            border.Padding(Th(20, 16, 20, 18));
            border.BorderBrush(divider);
            border.BorderThickness(Th(0, 0, 0, i + 1 < sections.size() ? 1 : 0));
            panel.Children().Append(border);
        }
        return panel;
    }

    UIElement MainWindow::MemoControls()
    {
        auto outMemo = Input(L"출금통장 표시", L"일괄 적용");
        auto inMemo = Input(L"입금통장 표시", L"일괄 적용");
        auto cms = Input(L"CMS코드", L"일괄 적용");
        CheckBox onlyEmpty;
        onlyEmpty.Content(Text(L"빈 칸에만 적용", 13));
        onlyEmpty.IsChecked(true);
        auto apply = MakeButton(L"메모 적용", [this, outMemo, inMemo, cms, onlyEmpty]
        {
            Guard([&]
            {
                std::pair<wchar_t const*, std::wstring> values[]{ { L"outMemo", std::wstring(outMemo.Text()) }, { L"inMemo", std::wstring(inMemo.Text()) }, { L"cms", std::wstring(cms.Text()) } };
                for (auto const& row : transfers)
                    for (auto const& [key, value] : values)
                        if (!value.empty() && (!IsOn(onlyEmpty) || Str(row, key).empty())) Put(row, key, value);
                RenderTransfers();
            });
        });
        auto stack = Stack({ outMemo, inMemo, cms, onlyEmpty, apply });
        stack.Spacing(8);
        return stack;
    }

    void MainWindow::ExtractTransfer()
    {
        Guard([this]
        {
            std::vector<std::pair<TextBox, std::wstring>> inputs;
            for (auto const& box : threadBoxes)
                if (auto value = Trim(box.Text()); !value.empty()) inputs.emplace_back(box, value);
            if (inputs.empty()) { Notify(L"이체 요청을 붙여넣어 주세요.", true); return; }
            std::vector<JObj> staged;
            std::set<std::wstring> stagedKeys;
            for (auto const& [box, value] : inputs)
            {
                auto rawKey = L"raw:" + NormalizeLines(value);
                if (imported.contains(rawKey) || stagedKeys.contains(rawKey)) continue;
                auto parsed = rules.ParseTransferForReview(value);
                auto items = Objects(Arr(parsed, L"items"));
                if (items.empty()) continue;
                stagedKeys.insert(rawKey);
                std::wstring tag(unbox_value_or<hstring>(box.Tag(), L""));
                std::map<std::wstring, int> occurrences;
                for (auto const& item : items)
                {
                    auto signature = Str(item, L"bank") + L"|" + Str(item, L"account") + L"|" + Str(item, L"amount");
                    auto key = tag + L":" + signature + L":" + std::to_wstring(++occurrences[signature]);
                    if (imported.contains(key) || !stagedKeys.insert(key).second) continue;
                    auto row = Clone(item);
                    Put(row, L"id", NewId());
                    if (auto ambiguous = parsed.TryLookup(L"ambiguous")) row.Insert(L"ambiguous", Windows::Data::Json::JsonValue::Parse(ambiguous.Stringify()));
                    Put(row, L"outMemo", Str(row, L"plate"));
                    Put(row, L"inMemo", Str(row, L"plate"));
                    Put(row, L"cms", cmsOnExtract ? Trim(cmsOnExtract.Text()) : L"");
                    staged.push_back(row);
                }
            }
            auto split = rules.Split(staged);
            if (transfers.size() + split.size() > MaxTransferRows) { Notify(L"한 파일에 100건까지 가능해요. 요청을 나눠서 처리해 주세요.", true); return; }
            imported.insert(stagedKeys.begin(), stagedKeys.end());
            for (auto const& row : split) transfers.push_back(row);
            if (split.empty()) { Notify(L"추가할 새 이체 내역이 없어요."); return; }
            RenderTransfers();
            Step(2);
            Notify(std::to_wstring(split.size()) + L"건을 추가했어요. 이미 가져온 건은 건너뛰었어요.");
        });
    }

    void MainWindow::AddTransfer()
    {
        if (transfers.size() >= MaxTransferRows) { Notify(L"한 파일에 100건까지 가능해요.", true); return; }
        JObj row;
        Put(row, L"id", NewId());
        for (auto key : { L"bank", L"account", L"amount", L"plate", L"outMemo", L"inMemo", L"cms", L"source" }) Put(row, key, L"");
        transfers.push_back(row);
        RenderTransfers();
    }

    void MainWindow::SplitTransfers()
    {
        auto parts = rules.Split(transfers);
        if (parts.size() > MaxTransferRows) { Notify(L"금액을 나누면 100건을 넘어요. 다른 줄을 줄여 주세요.", true); return; }
        if (parts.size() != transfers.size()) { transfers = parts; RenderTransfers(); }
    }

    std::string MainWindow::ThreadSignature()
    {
        std::string signature = "[";
        for (auto const& box : threadBoxes) signature += JsString(box.Text()) + ",";
        return signature + "]";
    }

    bool MainWindow::HasUnsavedTransfers()
    {
        if (transfers.empty())
            return std::any_of(threadBoxes.begin(), threadBoxes.end(), [](auto const& box) { return !Trim(box.Text()).empty(); });
        return savedSignature != Signature() || savedThread != ThreadSignature();
    }

    int MainWindow::TransferIndex(JObj const& row)
    {
        auto found = std::find(transfers.begin(), transfers.end(), row);
        return found == transfers.end() ? -1 : static_cast<int>(found - transfers.begin());
    }

    // 표 열: [선택][#][상태][은행][계좌번호][예금주][입금액][차량번호][출금 표시][입금 표시][CMS]
    std::vector<double> MainWindow::TransferTableWidths()
    {
        std::vector<double> widths{ 36, 72, 150 };
        widths.insert(widths.end(), transferWidths.begin(), transferWidths.end());
        return widths;
    }

    void MainWindow::BuildTransferHeader()
    {
        std::vector<std::wstring> labels{ L"", L"#", L"상태" };
        for (auto const& field : TransferFields) labels.emplace_back(std::wstring_view(field.key) == L"amount" ? L"입금액" : field.label);
        transferHeaderHost.Children().Clear();
        transferHeaderHost.Children().Append(TableHeader(TransferTableWidths(), labels));
    }

    void MainWindow::RenderTransfers()
    {
        refreshing = true;
        transferList.Children().Clear();
        transferViews.clear();
        groupedEditors.clear();
        auto const& banks = rules.Banks();
        auto widths = TransferTableWidths();
        for (auto const& row : transfers)
        {
            auto id = Str(row, L"id");
            auto index = static_cast<int>(transferViews.size());
            CheckBox pick;
            pick.IsChecked(selectedTransfers.contains(id));
            pick.MinWidth(0);
            pick.Padding(Th(0));
            Name(pick, std::to_wstring(index + 1) + L"번 선택");
            // # 아래 주황 '분할 1/2'
            StackPanel number;
            number.Children().Append(Text(std::to_wstring(index + 1), 13));
            if (auto group = Str(row, L"splitGroup"); !group.empty())
            {
                int position = 0, total = 0;
                for (auto const& other : transfers)
                {
                    if (Str(other, L"splitGroup") != group) continue;
                    total++;
                    if (other == row) position = total;
                }
                auto split = Text(L"분할 " + std::to_wstring(position) + L"/" + std::to_wstring(total), 11);
                split.Foreground(orange.Solid);
                split.FontWeight(SemiBold());
                number.Children().Append(split);
            }
            auto statusText = Text(L"", 12);
            auto pill = Pill(statusText);
            auto why = Muted(L"", 11);
            why.MaxWidth(140);
            auto status = Stack({ pill, why });
            status.Spacing(1);
            ComboBox bank;
            SetItems(bank, banks);
            if (auto found = std::find(banks.begin(), banks.end(), Str(row, L"bank")); found != banks.end()) bank.SelectedIndex(static_cast<int>(found - banks.begin()));
            bank.HorizontalAlignment(HorizontalAlignment::Stretch);
            bank.MinWidth(0);
            Name(bank, L"입금은행");
            bank.SelectionChanged([this, row](IInspectable const& sender, auto&&)
            {
                auto value = SelectedText(sender.as<ComboBox>());
                Guard([&] { UpdateTransferField(row, L"bank", value); });
            });
            groupedEditors[{ id, L"bank" }] = [this, bank](std::wstring const& value)
            {
                auto const& names = rules.Banks();
                auto found = std::find(names.begin(), names.end(), value);
                bank.SelectedIndex(found == names.end() ? -1 : static_cast<int>(found - names.begin()));
            };
            std::vector<FrameworkElement> cells{ pick, number, status, bank };
            for (size_t i = 1; i < TransferFields.size(); i++)
            {
                auto box = Edit(row, TransferFields[i].key, TransferFields[i].label);
                if (std::wstring_view(TransferFields[i].key) == L"amount") box.TextAlignment(TextAlignment::Right);
                cells.push_back(box);
            }
            auto grid = Columns(widths, cells);
            grid.ColumnSpacing(8);
            grid.MinHeight(48);
            auto line = TableRow(grid, index);
            auto highlight = [this, id, index, weak = make_weak(line)]
            {
                if (auto border = weak.get()) border.Background(selectedTransfers.contains(id) ? rowSelected : index % 2 == 1 ? rowAlt : transparent);
            };
            pick.Checked([this, id, highlight](auto&&, auto&&) { selectedTransfers.insert(id); highlight(); });
            pick.Unchecked([this, id, highlight](auto&&, auto&&) { selectedTransfers.erase(id); highlight(); });
            highlight();
            // 줄을 누르거나 칸에 들어가면 오른쪽 패널에 원문·확인 사항을 보여 줘요.
            line.AddHandler(UIElement::PointerPressedEvent(), box_value(Input::PointerEventHandler([this, row](auto&&, auto&&) { Guard([&] { ShowTransferDetail(row); }); })), true);
            line.GotFocus([this, row](auto&&, auto&&) { Guard([&] { ShowTransferDetail(row); }); });
            transferViews.push_back({ line, pick, statusText, pill, why, row });
            transferList.Children().Append(line);
        }
        if (transfers.empty())
        {
            auto empty = Muted(L"아직 이체 내역이 없어요. 1단계에서 스레드를 붙여넣고 추출해 주세요.", 13);
            empty.Margin(Th(12, 16, 0, 0));
            transferList.Children().Append(empty);
        }
        if (detailRow && TransferIndex(detailRow) < 0) detailRow = nullptr;
        refreshing = false;
        ValidateTransfers();
    }

    TextBox MainWindow::Edit(JObj const& row, std::wstring const& key, std::wstring_view label)
    {
        auto box = Input(L"");
        box.Text(Str(row, key));
        box.MinWidth(0);
        Name(box, label);
        groupedEditors[{ Str(row, L"id"), key }] = [box](std::wstring const& value) { box.Text(value); };
        box.TextChanged([this, row, key](IInspectable const& sender, auto&&)
        {
            if (refreshing) return;
            std::wstring text(sender.as<TextBox>().Text());
            Guard([&] { UpdateTransferField(row, key, NormalizeTransferValue(key, text)); });
        });
        box.LostFocus([this, row, key](IInspectable const& sender, auto&&)
        {
            auto editor = sender.as<TextBox>();
            Guard([&] { CommitTransferEdit(row, key, editor); });
        });
        return box;
    }

    void MainWindow::ValidateTransfers()
    {
        if (refreshing || !acknowledge) return;
        if (IsOn(acknowledge) && acknowledgedSignature != Signature()) acknowledge.IsChecked(false);
        auto checks = rules.Check(transfers);
        auto count = [&](wchar_t const* state) { return static_cast<int>(std::count_if(checks.begin(), checks.end(), [&](auto const& check) { return Str(check, L"state") == state; })); };
        auto bad = count(L"bad"), warn = count(L"warn");
        lastBad = bad;
        lastWarn = warn;
        auto total = Sum(transfers, L"amount");
        auto size = static_cast<int>(transfers.size());
        transferSummary.Text(transfers.empty() ? std::wstring(L"이체 요청을 붙여넣고 추출해 주세요.")
            : N0(size) + L"건 · 합계 " + N0(total) + L"원 · 오류 " + std::to_wstring(bad) + L"건 · 검토 " + std::to_wstring(warn) + L"건"
                + (size > 100 ? L" · 한 파일에 100건까지 가능해요." : L""));
        transferSum.Inlines().Clear();
        auto sumLabel = MakeRun(L"총 입금액 ");
        sumLabel.Foreground(mutedInk);
        auto sumValue = MakeRun(N0(total) + L"원");
        sumValue.FontWeight(Bold());
        sumValue.Foreground(accent.Solid);
        transferSum.Inlines().Append(sumLabel);
        transferSum.Inlines().Append(sumValue);
        UpdateTransferSeg(size, size - bad - warn, warn, bad);
        int shown = 0;
        for (size_t i = 0; i < checks.size() && i < transferViews.size(); i++)
        {
            auto& view = transferViews[i];
            auto state = Str(checks[i], L"state");
            auto visible = ShowTransfer(transfers[i], state);
            if (visible) shown++;
            view.card.Visibility(Shown(visible));
            view.status.Text(state == L"bad" ? L"오류" : state == L"warn" ? L"검토 필요" : L"정상");
            TintPill(view.pill, state == L"bad" ? red : state == L"warn" ? orange : green);
            auto reasons = Strings(checks[i].TryLookup(L"errors"));
            for (auto const& review : Strings(checks[i].TryLookup(L"reviews"))) reasons.push_back(review);
            view.why.Text(Join(reasons, L" · "));
            view.why.Visibility(Shown(!reasons.empty()));
        }
        transferRowCount.Text(transfers.empty() ? std::wstring() : shown == size ? L"이체 내역 " + N0(size) + L"건" : L"이체 내역 " + N0(shown) + L" / " + N0(size) + L"건");
        download.IsEnabled(!saving && size > 0 && size <= MaxTransferRows && bad == 0 && (warn == 0 || IsOn(acknowledge)));
        downloadCommand.IsEnabled(download.IsEnabled());
        ackSection.Visibility(Shown(warn > 0));
        if (detailRow) ShowTransferDetail(detailRow);
        if (step == 3) { UpdatePreview(); RenderComments(); }
        UpdateTransferNav();
    }

    /// 오른쪽 패널: 고른 건의 원문과 확인 사항 (오류 빨강, 검토 주황)
    void MainWindow::ShowTransferDetail(JObj const& row)
    {
        auto index = TransferIndex(row);
        if (index < 0) return;
        detailRow = row;
        detailTitle.Text(std::to_wstring(index + 1) + L"번 건 원문");
        auto source = Str(row, L"ctx").empty() ? Str(row, L"source") : Str(row, L"ctx");
        detailSource.Text(source.empty() ? L"(원문 없음)" : source);
        detailIssues.Children().Clear();
        auto checks = rules.Check(transfers);
        if (index >= static_cast<int>(checks.size())) return;
        auto add = [this](std::wstring const& text, Media::Brush const& ink)
        {
            auto dot = DotLabel(Text(text, 13), ink);
            dot.VerticalAlignment(VerticalAlignment::Top);
            detailIssues.Children().Append(dot);
        };
        for (auto const& error : Strings(checks[index].TryLookup(L"errors"))) add(error, red.Solid);
        for (auto const& review : Strings(checks[index].TryLookup(L"reviews"))) add(review, orange.Solid);
        if (detailIssues.Children().Size() == 0) add(L"확인할 사항이 없어요.", green.Solid);
    }

    void MainWindow::Step(int n)
    {
        step = n;
        transferInput.Visibility(Shown(n == 1));
        transferReview.Visibility(Shown(n == 2));
        transferDownload.Visibility(Shown(n == 3));
        transferPanel1.Visibility(Shown(n == 1)); transferCommands1.Visibility(Shown(n == 1)); extractButton.Visibility(Shown(n == 1));
        transferPanel2.Visibility(Shown(n == 2)); transferCommands2.Visibility(Shown(n == 2)); nextStepButton.Visibility(Shown(n == 2));
        transferPanel3.Visibility(Shown(n == 3)); transferCommands3.Visibility(Shown(n == 3)); downloadCommand.Visibility(Shown(n == 3));
        transferSubtitle.Text(std::to_wstring(n) + L"단계 · " + TransferStepNames[n - 1]);
        ValidateTransfers();
        UpdateTransferNav();
    }

    void MainWindow::UpdatePreview()
    {
        previewRows.Children().Clear();
        summary3.Inlines().Clear();
        auto separator = [this]
        {
            auto run = MakeRun(L"  ·  ");
            run.Foreground(mutedInk);
            return run;
        };
        auto total = MakeRun(L"총 " + N0(static_cast<double>(transfers.size())) + L"건");
        total.FontWeight(SemiBold());
        auto sum = MakeRun(N0(Sum(transfers, L"amount")) + L"원");
        sum.FontWeight(SemiBold());
        auto state = MakeRun(L"● " + (lastBad > 0 ? L"오류 " + std::to_wstring(lastBad) + L"건" : lastWarn > 0 ? L"검토 필요 " + std::to_wstring(lastWarn) + L"건" : std::wstring(L"오류 없음")));
        state.Foreground(lastBad > 0 ? red.Solid : lastWarn > 0 ? orange.Solid : green.Solid);
        for (auto const& run : { total, separator(), sum, separator(), state }) summary3.Inlines().Append(run);
        if (transfers.empty())
        {
            auto empty = Muted(L"이체 내역을 추가하면 여기에 표시돼요.", 13);
            empty.Margin(Th(12, 12, 0, 0));
            previewRows.Children().Append(empty);
        }
        int index = 0;
        for (auto const& row : transfers)
        {
            auto amount = Text(Trim(Str(row, L"amount")).empty() ? L"미입력" : Money(row, L"amount"));
            amount.TextAlignment(TextAlignment::Right);
            previewRows.Children().Append(TableRow(Columns(TransferColumns, { Text(Str(row, L"bank")), Text(Str(row, L"account")), amount,
                Text(Str(row, L"outMemo")), Text(Str(row, L"inMemo")), Text(Str(row, L"cms")) }), index++));
        }
    }

    /// 송금 코멘트: 줄마다 복사 아이콘
    void MainWindow::RenderComments()
    {
        commentList.Children().Clear();
        auto lines = rules.Comments(transfers);
        std::erase_if(lines, [](auto const& line) { return line.empty(); });
        if (lines.empty()) { commentList.Children().Append(Muted(L"이체 내역이 없어요.", 13)); return; }
        auto first = true;
        for (auto const& line : lines)
        {
            Button copy;
            copy.Content(Icon(L"", 14));
            copy.Background(transparent);
            copy.BorderThickness(Th(0));
            copy.Padding(Th(6));
            copy.VerticalAlignment(VerticalAlignment::Top);
            Tip(copy, L"복사");
            Name(copy, L"복사: " + line);
            copy.Click([this, line](auto&&, auto&&) { Copy(line); });
            auto grid = Columns({ 0, 36 }, { Text(line, 13), copy });
            grid.Padding(Th(0, 4, 0, 4));
            Border border;
            border.Child(grid);
            border.BorderBrush(divider);
            border.BorderThickness(Th(0, first ? 0 : 1, 0, 0));
            commentList.Children().Append(border);
            first = false;
        }
    }

    Async MainWindow::SaveTransfer()
    {
        if (saving) co_return;
        // 쓰기 직전에 한 번 더 검사해요. 단추가 켜져 있는지는 안전장치가 아니에요.
        auto checks = rules.Check(transfers);
        auto signature = Signature();
        auto has = [&](wchar_t const* state) { return std::any_of(checks.begin(), checks.end(), [&](auto const& check) { return Str(check, L"state") == state; }); };
        auto needsReview = has(L"warn");
        auto reviewed = [&] { return !needsReview || (IsOn(acknowledge) && acknowledgedSignature == signature); };
        if (transfers.empty() || transfers.size() > MaxTransferRows || has(L"bad") || !reviewed())
            throw AppError{ L"오류를 수정하고 검토 항목을 확인해 주세요." };
        saving = true;
        std::wstring failure;
        try
        {
            auto proceed = smokeDirectory.has_value() || savedSignature == signature;
            if (!proceed) proceed = co_await ConfirmSentAgain();
            if (proceed)
            {
                auto sourceSignature = ThreadSignature();
                auto values = Spreadsheets::TransferRows(transfers);
                std::filesystem::path fileName = rules.TransferFileName(transfers, Now(L"%Y%m%d_%H%M"));
                std::wstring path(co_await SavePicker(fileName.stem().wstring(), L".xlsx"));
                if (!path.empty())
                {
                    if (signature != Signature() || sourceSignature != ThreadSignature() || !reviewed())
                        Notify(L"저장 창을 연 뒤 내역이 바뀌었어요. 변경 내용을 확인하고 다시 저장해 주세요.", true);
                    else
                    {
                        Spreadsheets::Write(path, values, L"다계좌이체");
                        savedSignature = signature;
                        savedThread = sourceSignature;
                        if (!smokeDirectory) RememberSent(Now(L"%H:%M"));
                        Notify(L"이체파일을 저장했어요. 은행 업로드 전에 계좌와 금액을 확인해 주세요.");
                    }
                }
            }
        }
        catch (hresult_canceled const&) {}
        catch (hresult_error const& error) { failure = error.message(); if (failure.empty()) failure = L"이체파일을 저장하지 못했어요."; }
        saving = false;
        ValidateTransfers();
        if (!failure.empty()) throw AppError{ failure };
    }

    // 오늘 이미 받은 이체파일과 같은 은행·계좌·금액이 있으면 한 번 더 물어봐요 (앱을 다시 연 뒤 두 번 송금 방지).
    Windows::Foundation::IAsyncOperation<bool> MainWindow::ConfirmSentAgain()
    {
        auto sent = Arr(LocalState::ReadToday(SentTodayFile), L"items");
        auto matches = rules.SentMatches(transfers, sent);
        if (matches.empty()) co_return true;
        std::vector<std::wstring> lines;
        for (size_t i = 0; i < matches.size() && i < 10; i++)
        {
            auto index = static_cast<size_t>(matches[i].GetNamedNumber(L"row", 0));
            if (index >= transfers.size()) continue;
            auto const& row = transfers[index];
            lines.push_back(Str(row, L"bank") + L" " + Str(row, L"account") + L" " + Money(row, L"amount") + L" · " + Str(matches[i], L"at") + L"에 받음");
        }
        if (matches.size() > 10) lines.push_back(L"외 " + std::to_wstring(matches.size() - 10) + L"건");
        auto dialog = Dialog(L"오늘 이미 받은 이체 건이 있어요",
            Stack({ Text(L"같은 은행·계좌·금액 " + std::to_wstring(matches.size()) + L"건이 오늘 받은 이체파일에 이미 들어 있어요. 두 번 송금하지 않도록 확인해 주세요."),
                Text(Join(lines, L"\r\n"), 13) }), L"그래도 저장");
        co_return co_await dialog.ShowAsync() == ContentDialogResult::Primary;
    }

    void MainWindow::RememberSent(std::wstring const& at)
    {
        auto saved = LocalState::ReadToday(SentTodayFile);
        auto items = Arr(saved, L"items");
        std::set<std::wstring> have;
        for (auto const& item : Objects(items)) have.insert(Str(item, L"key"));
        for (auto const& key : rules.SentKeys(transfers))
        {
            if (!have.insert(key).second) continue;
            JObj item;
            Put(item, L"key", key);
            Put(item, L"at", at);
            items.Append(item);
        }
        while (items.Size() > 2000) items.RemoveAt(0);
        saved.Insert(L"items", items);
        LocalState::WriteToday(SentTodayFile, saved);
    }

    Async MainWindow::ResetTransfers()
    {
        auto dialog = Dialog(L"전체 초기화", Text(L"붙여넣은 스레드와 이체 내역을 모두 지울까요?"), L"모두 지우기");
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        transfers.clear();
        imported.clear();
        selectedTransfers.clear();
        threadBoxes.clear();
        threadInputs.Children().Clear();
        detailRow = nullptr;
        AddThreadInput(true);
        savedSignature.clear();
        RenderTransfers();
        Step(1);
    }
}
