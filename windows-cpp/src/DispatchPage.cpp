#include "pch.h"
#include "MainWindow.h"

// 탁송체크: (탐색 창의 보기 필터) | 표 | 오른쪽 입력 패널.
// 보기 필터는 탐색 창 하위 항목, 슬랙봇 회신은 오른쪽 패널, 복사·저장은 CommandBar에 있어요.

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using winrt::Windows::Storage::StorageFile;
using winrt::Windows::System::VirtualKey;

namespace wt
{
    FrameworkElement MainWindow::DispatchPage()
    {
        LoadWorkMarks();
        LoadDispatchPageSize();
        LoadDispatchWidths();

        // ── 오른쪽 입력 패널 ──
        vehicles = TextBox();
        vehicles.PlaceholderText(L"2026-11-25 10:00  123가4567\n45나6789\n\n차량번호만 입력해도 돼요.");
        vehicles.AcceptsReturn(true);
        vehicles.TextWrapping(TextWrapping::NoWrap);
        vehicles.FontFamily(AppFont(L"MonoFont"));
        vehicles.FontSize(13);
        vehicles.MinHeight(160);
        vehicles.VerticalAlignment(VerticalAlignment::Stretch);
        vehicles.IsSpellCheckEnabled(false);
        ScrollViewer::SetVerticalScrollBarVisibility(vehicles, ScrollBarVisibility::Auto);
        Name(vehicles, L"차량목록");
        vehicleCount = Muted(L"0대 인식");
        Automation::AutomationProperties::SetLiveSetting(vehicleCount, Automation::Peers::AutomationLiveSetting::Polite);
        vehicles.TextChanged([this](auto&&, auto&&) { Guard([this] { OnVehiclesChanged(); }); });
        // 여러 줄 입력칸이 Ctrl+Enter를 줄바꿈으로 먹으면 조회 뒤 결과가 지워져요. 여기서 먼저 처리해요.
        vehicles.PreviewKeyDown([this](auto&&, Input::KeyRoutedEventArgs const& args)
        {
            if (args.Key() != VirtualKey::Enter) return;
            using winrt::Windows::UI::Core::CoreVirtualKeyStates;
            auto state = winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(VirtualKey::Control);
            if ((state & CoreVirtualKeyStates::Down) != CoreVirtualKeyStates::Down) return;
            args.Handled(true);
            QueryDispatch();
        });
        fileSummary = Muted(L"여기에 .xlsx 파일을 끌어다 놓으세요");
        fileSummary.HorizontalAlignment(HorizontalAlignment::Center);
        fileSummary.Margin(Th(0, 8, 0, 8));
        dispatchFiles = StackPanel();
        auto clean = MakeButton(L"정리", [this] { CleanVehicles(); });
        Tip(clean, L"빈 줄·공백 정리");
        HyperlinkButton pick;
        pick.Content(box_value(L"파일 선택"));
        pick.Click([this](auto&&, auto&&) { Run([this] { return ImportFiles(); }); });
        HyperlinkButton clearFiles;
        clearFiles.Content(box_value(L"모두 빼기"));
        clearFiles.Click([this](auto&&, auto&&)
        {
            if (importingDispatch) return;
            files.clear();
            RenderDispatchFiles();
            InvalidateDispatch();
        });
        auto run = MakeButton(L"차량 조회하기", [this] { QueryDispatch(); }, true);
        auto shortcut = Text(L"Ctrl+Enter", 12);
        shortcut.Opacity(0.75);
        shortcut.VerticalAlignment(VerticalAlignment::Center);
        auto runContent = Bar({ Text(L"차량 조회하기"), shortcut });
        runContent.Spacing(12);
        run.Content(runContent);
        run.HorizontalAlignment(HorizontalAlignment::Stretch);
        run.Padding(Th(12, 8, 12, 8));
        Name(run, L"차량 조회하기");

        Grid input;
        input.Padding(Th(20, 20, 20, 0));
        input.RowSpacing(6);
        for (int i = 0; i < 7; i++) AddRow(input, i == 2 ? Star() : AutoSize(), i == 2 ? 160 : 0);
        auto title = ColorHeading(L"입력", accent, L"", 20, 22);
        title.Margin(Th(0, 0, 0, 8));
        input.Children().Append(title);
        auto listHead = Columns({ 0, 0 }, { ColorHeading(L"차량목록", accent, L""), clean });
        listHead.Padding(Th(0));
        clean.HorizontalAlignment(HorizontalAlignment::Right);
        Place(input, listHead, 1);
        Place(input, vehicles, 2);
        Place(input, vehicleCount, 3);
        auto separator = Divider();
        separator.Margin(Th(-20, 8, -20, 8));
        Place(input, separator, 4);
        Grid filesHead;
        AddColumn(filesHead, Star());
        AddColumn(filesHead, AutoSize());
        auto filesTitle = ColorHeading(L"업체 파일", blue, L"");
        filesTitle.VerticalAlignment(VerticalAlignment::Center);
        filesHead.Children().Append(filesTitle);
        auto fileLinks = Bar({ pick, clearFiles });
        fileLinks.Spacing(4);
        Place(filesHead, fileLinks, 0, 1);
        Place(input, filesHead, 5);
        ScrollViewer fileScroll;
        fileScroll.Content(dispatchFiles);
        fileScroll.MaxHeight(220);
        fileScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        auto fileArea = Stack({ fileScroll, fileSummary });
        fileArea.Spacing(0);
        Place(input, fileArea, 6);
        Grid inputBody;
        AddRow(inputBody, Star());
        AddRow(inputBody, AutoSize());
        inputBody.Children().Append(input);
        Border runBar;
        runBar.Child(run);
        runBar.Padding(Th(20, 12, 20, 20));
        runBar.BorderBrush(divider);
        runBar.BorderThickness(Th(0, 1, 0, 0));
        Place(inputBody, runBar, 1);
        dispatchInputPanel = inputBody;
        slackDispatchStage = CreateSlackDispatchStage();
        slackDispatchStage.Visibility(Visibility::Collapsed);
        Grid panelHost;
        panelHost.AllowDrop(true);
        panelHost.Background(transparent);
        panelHost.Children().Append(dispatchInputPanel);
        panelHost.Children().Append(slackDispatchStage);
        // 패널 전체가 파일 드롭 영역이에요.
        panelHost.DragOver([this](IInspectable const& sender, DragEventArgs const& args)
        {
            if (importingDispatch || !args.DataView().Contains(StandardDataFormats::StorageItems())) return;
            args.AcceptedOperation(DataPackageOperation::Copy);
            if (auto caption = args.DragUIOverride()) caption.Caption(L"업체 파일 첨부");
            sender.as<Grid>().Background(dropHighlight);
        });
        panelHost.DragLeave([this](IInspectable const& sender, auto&&) { sender.as<Grid>().Background(transparent); });
        panelHost.Drop([this](IInspectable const& sender, DragEventArgs const& args)
        {
            sender.as<Grid>().Background(transparent);
            if (!args.DataView().Contains(StandardDataFormats::StorageItems())) return;
            auto view = args.DataView();
            auto deferral = args.GetDeferral();
            Run([this, view, deferral]() -> Async
            {
                auto self = this;
                auto data = view;
                auto done = deferral;
                std::vector<StorageFile> dropped;
                try
                {
                    for (auto const& item : co_await data.GetStorageItemsAsync())
                        if (auto file = item.try_as<StorageFile>()) dropped.push_back(file);
                }
                catch (hresult_error const&) {}
                done.Complete();
                self->ShowSlackPanel(false);
                co_await self->AddDispatchFiles(dropped);
            });
        });
        dispatchPanel = SidePanel(panelHost);
        RenderDispatchFiles();

        // ── CommandBar ──
        auto menu = [this](std::vector<std::pair<wchar_t const*, AsyncAction>> const& entries)
        {
            MenuFlyout flyout;
            for (auto const& [text, action] : entries)
            {
                MenuFlyoutItem item;
                item.Text(text);
                item.Click([this, action = action](auto&&, auto&&) { Run(action); });
                flyout.Items().Append(item);
            }
            return flyout;
        };
        auto flyoutButton = [](wchar_t const* label, wchar_t const* glyph, Swatch& color, MenuFlyout const& flyout)
        {
            auto icon = Icon(glyph);
            icon.Foreground(color.Solid);
            AppBarButton button;
            button.Label(label);
            button.Icon(icon);
            button.Flyout(flyout);
            return button;
        };
        auto capture = flyoutButton(L"캡처", L"", indigo, menu({
            { L"클립보드에 복사", [this] { return CaptureDispatch(true); } }, { L"PNG 저장", [this] { return CaptureDispatch(false); } } }));
        auto save = flyoutButton(L"저장", L"", blue, menu({
            { L"Excel 저장", [this] { return ExportDispatch(L".xlsx"); } }, { L"PDF 저장", [this] { return CaptureDispatch(false, true); } },
            { L"TXT 저장", [this] { return ExportDispatch(L".txt"); } } }));
        CommandBar commands;
        commands.DefaultLabelPosition(CommandBarDefaultLabelPosition::Right);
        commands.Background(transparent);
        commands.HorizontalAlignment(HorizontalAlignment::Left);
        commands.IsDynamicOverflowEnabled(true);
        commands.VerticalAlignment(VerticalAlignment::Center);
        commands.PrimaryCommands().Append(Command(L"차량번호 복사", L"", [this] { CopyDispatchNumbers(); }, &teal));
        commands.PrimaryCommands().Append(capture);
        commands.PrimaryCommands().Append(save);
        commands.PrimaryCommands().Append(Command(L"슬랙봇 회신", L"", [this] { ShowSlackPanel(true); }, &purple));
        commands.SecondaryCommands().Append(Command(L"보이는 차량 모두 체크", L"", [this]
        {
            for (auto const& row : VisibleResults()) selected.insert(Str(row, L"id"));
            RenderDispatch();
        }));
        commands.SecondaryCommands().Append(Command(L"체크 해제", L"", [this] { selected.clear(); RenderDispatch(); }));
        commands.SecondaryCommands().Append(AppBarSeparator());
        commands.SecondaryCommands().Append(CommandAsync(L"필터·정렬", L"", [this] { return EditDispatchFilters(); }));
        commands.SecondaryCommands().Append(CommandAsync(L"열 너비", L"", [this] { return EditDispatchWidths(); }));
        commands.SecondaryCommands().Append(AppBarSeparator());
        commands.SecondaryCommands().Append(CommandAsync(L"전체 초기화", L"", [this] { return ResetDispatch(); }, &red));

        scope = ComboBox();
        scope.Width(160);
        SetItems(scope, { L"현재 표시 목록", L"전체 조회 결과", L"금액 확인 차량" });
        scope.SelectedIndex(0);
        scope.VerticalAlignment(VerticalAlignment::Center);
        Tip(scope, L"복사·캡처·저장 범위 (체크한 차량이 있으면 그 차량만)");
        Name(scope, L"저장 범위");
        search = TextBox();
        search.PlaceholderText(L"차량번호 검색");
        search.Width(200);
        search.VerticalAlignment(VerticalAlignment::Center);
        search.Padding(Th(10, 5, 32, 6));
        Name(search, L"차량번호 검색");
        search.TextChanged([this](auto&&, auto&&) { dispatchPage = 0; Guard([this] { RenderDispatch(); }); });
        Grid searchBox;
        searchBox.VerticalAlignment(VerticalAlignment::Center);
        searchBox.Children().Append(search);
        auto searchIcon = Icon(L"", 14);
        searchIcon.Foreground(mutedInk);
        searchIcon.HorizontalAlignment(HorizontalAlignment::Right);
        searchIcon.Margin(Th(0, 0, 11, 0));
        searchIcon.IsHitTestVisible(false);
        searchBox.Children().Append(searchIcon);
        dispatchPanelToggle = PanelToggle(L"입력 패널", [this]() -> FrameworkElement { return dispatchPanel; });
        auto right = Bar({ scope, searchBox, dispatchPanelToggle });
        right.VerticalAlignment(VerticalAlignment::Center);
        Grid commandRow;
        commandRow.Margin(Th(16, 0, 16, 0));
        AddColumn(commandRow, Star());
        AddColumn(commandRow, AutoSize());
        commandRow.Children().Append(commands);
        Place(commandRow, right, 0, 1);

        // ── 업체 / 상태 얇은 줄 ──
        vendorBox = ComboBox();
        vendorBox.MinWidth(140);
        vendorBox.PlaceholderText(L"전체 업체");
        vendorBox.VerticalAlignment(VerticalAlignment::Center);
        stateBox = ComboBox();
        stateBox.MinWidth(150);
        stateBox.PlaceholderText(L"전체 상태");
        stateBox.VerticalAlignment(VerticalAlignment::Center);
        Name(vendorBox, L"업체 필터");
        Name(stateBox, L"탁송 상태 필터");
        vendorBox.SelectionChanged([this](auto&&, auto&&)
        {
            if (syncingDispatchBoxes) return;
            dispatchVendor = vendorBox.SelectedIndex() > 0 ? SelectedText(vendorBox) : L"";
            dispatchPage = 0;
            Guard([this] { RenderDispatch(); });
        });
        stateBox.SelectionChanged([this](auto&&, auto&&)
        {
            if (syncingDispatchBoxes) return;
            dispatchState = stateBox.SelectedIndex() > 0 ? SelectedText(stateBox) : L"";
            dispatchPage = 0;
            Guard([this] { RenderDispatch(); });
        });
        auto vendorLabel = Muted(L"업체", 12);
        vendorLabel.VerticalAlignment(VerticalAlignment::Center);
        auto stateLabel = Muted(L"상태", 12);
        stateLabel.VerticalAlignment(VerticalAlignment::Center);
        stateLabel.Margin(Th(8, 0, 0, 0));
        auto filterLine = Bar({ vendorLabel, vendorBox, stateLabel, stateBox });
        filterLine.Margin(Th(24, 4, 24, 10));

        // ── 표 ──
        dispatchList = StackPanel();
        dispatchTable = Grid();
        AddRow(dispatchTable, AutoSize());
        AddRow(dispatchTable, Star());
        dispatchScroll = ScrollViewer();
        dispatchScroll.Content(dispatchList);
        dispatchScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        dispatchScroll.VerticalScrollMode(ScrollMode::Enabled);
        Place(dispatchTable, dispatchScroll, 1);
        UpdateDispatchHeader();
        Grid table;
        table.BorderBrush(divider);
        table.BorderThickness(Th(0, 1, 0, 0));
        auto list = Horizontal(dispatchTable);
        list.Padding(Th(12, 0, 0, 0));
        table.Children().Append(list);
        dispatchEmptyTitle = Text(L"조회할 차량목록을 준비해 주세요", 16);
        dispatchEmptyTitle.FontWeight(SemiBold());
        dispatchEmptyTitle.HorizontalAlignment(HorizontalAlignment::Center);
        dispatchEmptyText = Muted(L"오른쪽 입력 패널에 차량목록을 넣고 업체 파일을 끌어다 놓으면\n완료 차량의 금액과 확인할 목록을 정리해 드려요.", 13);
        dispatchEmptyText.TextAlignment(TextAlignment::Center);
        auto emptyIcon = Icon(L"", 36);
        emptyIcon.Foreground(faintInk);
        dispatchEmpty = Stack({ emptyIcon, dispatchEmptyTitle, dispatchEmptyText });
        dispatchEmpty.Spacing(8);
        dispatchEmpty.HorizontalAlignment(HorizontalAlignment::Center);
        dispatchEmpty.VerticalAlignment(VerticalAlignment::Center);
        dispatchEmpty.IsHitTestVisible(false);
        table.Children().Append(dispatchEmpty);

        // ── 상태 줄: 왼쪽 메시지, 오른쪽 "n줄 · n대"와 페이지 ──
        dispatchSummary = Muted(L"");
        dispatchSummary.VerticalAlignment(VerticalAlignment::Center);
        dispatchCountText = Muted(L"");
        dispatchCountText.VerticalAlignment(VerticalAlignment::Center);
        auto pagerButton = [](wchar_t const* glyph, wchar_t const* name)
        {
            Button button;
            button.Content(Icon(glyph, 12));
            button.Padding(Th(8, 5, 8, 5));
            Name(button, name);
            return button;
        };
        previousResults = pagerButton(L"", L"이전 페이지");
        nextResults = pagerButton(L"", L"다음 페이지");
        previousResults.Click([this](auto&&, auto&&) { Guard([this] { ChangeDispatchPage(dispatchPage - 1); }); });
        nextResults.Click([this](auto&&, auto&&) { Guard([this] { ChangeDispatchPage(dispatchPage + 1); }); });
        dispatchPageNumbers = Bar();
        dispatchPageNumbers.Spacing(2);
        dispatchPageSizeBox = ComboBox();
        dispatchPageSizeBox.MinWidth(88);
        dispatchPageSizeBox.VerticalAlignment(VerticalAlignment::Center);
        std::vector<std::wstring> sizeLabels;
        for (auto size : DispatchPageSizes) sizeLabels.push_back(std::to_wstring(size) + L"건");
        SetItems(dispatchPageSizeBox, sizeLabels);
        dispatchPageSizeBox.SelectedIndex(static_cast<int>(std::find(DispatchPageSizes.begin(), DispatchPageSizes.end(), dispatchPageSize) - DispatchPageSizes.begin()));
        Name(dispatchPageSizeBox, L"한 페이지에 표시할 건수");
        dispatchPageSizeBox.SelectionChanged([this](auto&&, auto&&)
        {
            auto index = dispatchPageSizeBox.SelectedIndex();
            if (index >= 0 && index < static_cast<int>(DispatchPageSizes.size())) Guard([&] { SetDispatchPageSize(DispatchPageSizes[index]); });
        });
        auto pageSizeLabel = Muted(L"페이지당");
        pageSizeLabel.VerticalAlignment(VerticalAlignment::Center);
        auto pagination = Bar({ dispatchCountText, previousResults, dispatchPageNumbers, nextResults, pageSizeLabel, dispatchPageSizeBox });
        pagination.Spacing(6);
        auto status = StatusLine(dispatchSummary, pagination);

        // ── 배치 ──
        dispatchSubtitle = Text(L"전체 · 0대");
        Grid main;
        for (int i = 0; i < 5; i++) AddRow(main, i == 3 ? Star() : AutoSize());
        main.Children().Append(PageTitle(L"탁송체크", dispatchSubtitle));
        Place(main, commandRow, 1);
        Place(main, filterLine, 2);
        Place(main, table, 3);
        Place(main, status, 4);
        Grid page;
        AddColumn(page, Star(), 420);
        AddColumn(page, AutoSize());
        page.Children().Append(main);
        Place(page, dispatchPanel, 0, 1);

        // Ctrl+Enter: 차량 조회하기
        Input::KeyboardAccelerator accelerator;
        accelerator.Key(VirtualKey::Enter);
        accelerator.Modifiers(winrt::Windows::System::VirtualKeyModifiers::Control);
        accelerator.Invoked([this](auto&&, Input::KeyboardAcceleratorInvokedEventArgs const& args) { args.Handled(true); QueryDispatch(); });
        page.KeyboardAccelerators().Append(accelerator);

        RenderDispatch();
        return page;
    }

    /// 오른쪽 패널: 입력 ↔ 슬랙봇 회신
    void MainWindow::ShowSlackPanel(bool slack)
    {
        if (slack && results.empty()) { Notify(L"먼저 차량 조회를 해 주세요.", true); return; }
        if (slack) SetPanelVisible(dispatchPanelToggle, dispatchPanel, true);
        dispatchInputPanel.Visibility(Shown(!slack));
        slackDispatchStage.Visibility(Shown(slack));
        if (slack) RefreshSlackDispatchList();
    }

    /// 탐색 창에서 보기 필터(하위 항목)를 골랐을 때
    void MainWindow::SetDispatchFilter(std::wstring const& key)
    {
        if (dispatchFilter == key) return;
        dispatchFilter = key;
        dispatchPage = 0;
        RenderDispatch();
    }

    int MainWindow::DispatchCount(std::wstring const& key)
    {
        auto found = dispatchCounts.find(key);
        return found == dispatchCounts.end() ? 0 : found->second;
    }

    /// 조회 결과가 바뀌면 보기별 대수(차량번호 중복 제거)를 다시 세요.
    void MainWindow::RecountDispatch()
    {
        dispatchCounts.clear();
        // '슬랙 회신 반영' 항목이 사라지면 그 보기에 머물지 않게 '전체'로 돌려요.
        if (dispatchFilter == L"slack-replies" && std::none_of(results.begin(), results.end(), [](auto const& row) { return Flag(row, L"slackReviewed"); }))
        {
            dispatchFilter = L"all";
            dispatchPage = 0;
        }
        if (!results.empty())
        {
            std::vector<std::wstring> keys;
            for (auto const& view : DispatchViews) keys.emplace_back(view.key);
            dispatchCounts = rules.Counts(results, keys);
        }
        UpdateDispatchNav();
        SyncDispatchBoxes();
    }

    /// 업체·상태 상자 목록을 조회 결과에 맞춰요.
    void MainWindow::SyncDispatchBoxes()
    {
        if (!vendorBox) return;
        syncingDispatchBoxes = true;
        auto fill = [this](ComboBox const& box, wchar_t const* all, wchar_t const* key, std::wstring const& current)
        {
            std::vector<std::wstring> values;
            for (auto const& row : results) values.push_back(Str(row, key));
            auto items = DistinctSorted(values);
            items.insert(items.begin(), all);
            SetItems(box, items);
            auto found = std::find(items.begin(), items.end(), current);
            box.SelectedIndex(found == items.end() ? 0 : static_cast<int>(found - items.begin()));
        };
        try
        {
            fill(vendorBox, L"전체 업체", L"vendor", dispatchVendor);
            fill(stateBox, L"전체 상태", L"rawState", dispatchState);
        }
        catch (...) {}
        syncingDispatchBoxes = false;
    }

    Async MainWindow::ImportFiles()
    {
        Windows::Storage::Pickers::FileOpenPicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(Handle()));
        picker.FileTypeFilter().Append(L".xlsx");
        auto picked = co_await picker.PickMultipleFilesAsync();
        std::vector<StorageFile> chosen;
        if (picked) for (auto const& file : picked) chosen.push_back(file);
        co_await AddDispatchFiles(chosen);
    }

    static std::wstring LowerExtension(std::filesystem::path const& path)
    {
        auto extension = path.extension().wstring();
        if (!extension.empty()) CharLowerBuffW(extension.data(), static_cast<DWORD>(extension.size()));
        return extension;
    }

    JObj MainWindow::LoadVendorFile(std::filesystem::path const& path, std::wstring const& name)
    {
        auto bytes = ReadFile(path);
        if (!bytes) throw AppError{ L"파일을 열지 못했어요." };
        auto parsed = rules.ReadVendorFile(std::move(*bytes), name, std::nullopt);
        Put(parsed, L"path", path.wstring());
        return parsed;
    }

    Async MainWindow::AddDispatchFiles(std::vector<StorageFile> picked)
    {
        if (importingDispatch) co_return;
        importingDispatch = true;
        auto ui = queue;
        for (auto const& file : picked)
        {
            std::wstring name(file.Name()), path(file.Path());
            auto extension = LowerExtension(path);
            if (extension == L".xls") { Notify(name + L": 예전 .xls 형식은 읽을 수 없어요. Excel에서 .xlsx로 저장한 뒤 첨부해 주세요.", true); continue; }
            if (extension != L".xlsx") { Notify(name + L": Excel 파일을 첨부해 주세요.", true); continue; }
            if (std::any_of(files.begin(), files.end(), [&](auto const& f) { return Str(f, L"path") == path; })) { Notify(name + L": 이미 첨부한 파일이에요."); continue; }
            std::wstring failure;
            JObj parsed{ nullptr };
            std::string bytes;
            auto kind = Rules::ExcelKind::Workbook;
            // 파일 읽기와 엑셀 해석은 화면이 멈추지 않게 뒤쪽 스레드에서 해요.
            co_await resume_background();
            try
            {
                std::error_code error;
                if (std::filesystem::file_size(path, error) > 40ull * 1024 * 1024) throw AppError{ L"파일은 40MB까지 불러올 수 있어요." };
                auto content = ReadFile(path);
                if (!content) throw AppError{ L"파일을 열지 못했어요." };
                bytes = std::move(*content);
                kind = Rules::Inspect(bytes);
                if (kind == Rules::ExcelKind::Legacy) throw AppError{ L"예전 .xls 형식은 읽을 수 없어요. Excel에서 .xlsx로 저장한 뒤 첨부해 주세요." };
                if (kind == Rules::ExcelKind::Workbook) parsed = rules.ReadVendorFile(std::move(bytes), name, std::nullopt);
            }
            catch (hresult_error const& error) { failure = error.message(); }
            catch (std::exception const&) { failure = L"파일을 읽지 못했어요."; }
            co_await ResumeOn{ ui };
            if (failure.empty() && kind == Rules::ExcelKind::Encrypted)
            {
                PasswordBox password;
                password.Header(box_value(hstring(name)));
                password.PlaceholderText(L"엑셀 암호");
                auto dialog = Dialog(L"암호가 설정된 파일", password, L"불러오기");
                dialog.DefaultButton(ContentDialogButton::Primary);
                auto entered = std::make_shared<bool>(false);
                password.KeyDown([entered, weak = make_weak(dialog)](auto&&, Input::KeyRoutedEventArgs const& args)
                {
                    if (args.Key() != VirtualKey::Enter) return;
                    args.Handled(true);
                    *entered = true;
                    if (auto open = weak.get()) open.Hide();
                });
                dialog.Opened([weak = make_weak(password)](auto&&, auto&&) { if (auto box = weak.get()) box.Focus(FocusState::Programmatic); });
                auto answer = co_await dialog.ShowAsync();
                if (answer != ContentDialogResult::Primary && !*entered) continue;
                std::wstring value(password.Password());
                password.Password(L"");
                co_await resume_background();
                try { parsed = rules.ReadVendorFile(std::move(bytes), name, value); }
                catch (hresult_error const& error) { failure = error.message(); }
                catch (std::exception const&) { failure = L"파일을 읽지 못했어요."; }
                SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
                co_await ResumeOn{ ui };
            }
            if (!failure.empty() || !parsed) { Notify(name + L": " + (failure.empty() ? L"파일을 읽지 못했어요." : failure), true); continue; }
            Put(parsed, L"path", path);
            files.push_back(parsed);
            InvalidateDispatch();
            RenderDispatchFiles();
            auto warnings = Strings(parsed.TryLookup(L"warnings"));
            if (!warnings.empty())
            {
                auto body = Stack({ Text(L"파일은 첨부했지만 일부 열의 값이 평소와 달라요. 업체 사이트에서 받은 원본 시트인지, 열이 추가·이동되지 않았는지 확인한 뒤 조회해 주세요."),
                    Text(Join(warnings, L"\r\n"), 13) });
                auto dialog = Dialog(L"업체 양식이 바뀌었을 수 있어요", body, L"확인");
                dialog.CloseButtonText(L"");
                dialog.DefaultButton(ContentDialogButton::Primary);
                try { co_await dialog.ShowAsync(); }
                catch (hresult_error const&) {}
            }
        }
        importingDispatch = false;
        RenderDispatchFiles();
    }

    void MainWindow::UpdateVehicleCount()
    {
        if (Trim(vehicles.Text()).empty()) { vehicleCount.Text(L"0대 인식"); return; }
        try
        {
            auto input = rules.ParseVehicles(vehicles.Text());
            auto duplicates = static_cast<int>(input.GetNamedNumber(L"duplicates", 0));
            auto invalid = static_cast<int>(Arr(input, L"invalid").Size());
            auto count = static_cast<int>(Arr(input, L"vehicles").Size()) - duplicates;
            std::vector<std::wstring> parts{ N0(count) + L"대 인식" };
            if (duplicates > 0) parts.push_back(L"중복 " + N0(duplicates) + L"줄");
            if (invalid > 0) parts.push_back(L"확인 필요 " + N0(invalid) + L"줄");
            vehicleCount.Text(Join(parts, L" · "));
        }
        catch (...) { vehicleCount.Text(L"차량 수를 계산할 수 없어요."); }
    }

    void MainWindow::QueryDispatch()
    {
        if (importingDispatch) { Notify(L"업체 파일을 읽는 중이에요."); return; }
        Guard([this]
        {
            // 조회 직전에도 빈 줄을 지우고 앞뒤 줄바꿈을 잘라요.
            std::wstring current(vehicles.Text());
            auto tidy = RemoveBlankLines(current);
            while (!tidy.empty() && tidy.back() == L'\n') tidy.pop_back();
            while (!tidy.empty() && tidy.front() == L'\n') tidy.erase(0, 1);
            if (tidy != NormalizeLines(current)) SetVehiclesQuietly(tidy, static_cast<int>(tidy.size()));
            auto input = rules.ParseVehicles(vehicles.Text());
            auto invalid = Strings(input.TryLookup(L"invalid"));
            if (Arr(input, L"invalid").Size() > 0) { Notify(L"차량번호나 날짜를 확인해 주세요: " + Join(invalid, L", "), true); return; }
            if (Arr(input, L"vehicles").Size() == 0) { Notify(L"차량목록을 입력해 주세요.", true); return; }
            if (files.empty()) { Notify(L"업체 파일을 첨부해 주세요.", true); return; }
            results = rules.Match(Arr(input, L"vehicles"), files);
            ClearSlackReplies();
            selected.clear();
            dispatchPage = 0;
            RecountDispatch();
            RenderDispatch();
            Go(L"dispatch");
        });
    }

    std::vector<JObj> MainWindow::VisibleResults()
    {
        auto query = Trim(search.Text());
        std::vector<JObj> matching;
        for (auto index : rules.Filter(results, dispatchFilter))
            if (index < results.size() && ContainsIgnoreCase(Str(results[index], L"plate"), query)) matching.push_back(results[index]);
        // 조회 상태·메모로 찾거나 정렬할 때만 규칙에 글자를 물어봐요.
        auto computed = [](std::wstring const& key) { return key == L"status" || key == L"notes"; };
        auto needsText = computed(dispatchSort) || dispatchSearches.contains(L"status") || dispatchSearches.contains(L"notes");
        auto described = needsText ? rules.Describe(matching) : std::vector<Rules::Described>();
        struct Entry { JObj row{ nullptr }; std::wstring text; std::optional<double> number; };
        std::vector<Entry> entries;
        auto numeric = dispatchSort == L"amount" || dispatchSort == L"dispatchFee" || dispatchSort == L"vat";
        for (size_t i = 0; i < matching.size(); i++)
        {
            auto const* text = needsText && i < described.size() ? &described[i] : nullptr;
            if (!MatchesDispatchOptions(matching[i], text)) continue;
            Entry entry{ matching[i] };
            if (numeric) entry.number = ParseNumber(Str(matching[i], dispatchSort));
            else if (dispatchSort == L"status") entry.text = text ? text->label : L"";
            else if (dispatchSort == L"notes") entry.text = text ? text->notes : L"";
            else if (!dispatchSort.empty()) entry.text = Str(matching[i], dispatchSort);
            entries.push_back(std::move(entry));
        }
        if (numeric)
            // 값이 없는 줄은 항상 맨 뒤에 둬요.
            std::stable_sort(entries.begin(), entries.end(), [this](Entry const& a, Entry const& b)
            {
                if (a.number.has_value() != b.number.has_value()) return a.number.has_value();
                if (!a.number) return false;
                return dispatchDescending ? *a.number > *b.number : *a.number < *b.number;
            });
        else if (!dispatchSort.empty())
            std::stable_sort(entries.begin(), entries.end(), [this](Entry const& a, Entry const& b)
            {
                auto order = CompareText(a.text, b.text);
                return dispatchDescending ? order > 0 : order < 0;
            });
        std::vector<JObj> rows;
        for (auto const& entry : entries) rows.push_back(entry.row);
        return rows;
    }

    std::vector<JObj> MainWindow::ExportScope()
    {
        std::vector<JObj> rows;
        if (!selected.empty()) rows = SelectedOrVisibleDispatch();
        else if (scope.SelectedIndex() == 1) rows = results;
        else if (scope.SelectedIndex() == 2) { for (auto index : rules.Filter(results, L"confirmed")) rows.push_back(results[index]); }
        else rows = VisibleResults();
        if (rows.empty()) throw AppError{ L"복사하거나 저장할 조회 결과가 없어요." };
        for (auto const& row : rows)
            if (Flag(row, L"scheduleConflict") || Flag(row, L"feeConflict")) throw AppError{ L"겹친 탁송일정과 비용을 먼저 선택해 주세요." };
        return rows;
    }

    static size_t DistinctValues(JArr const& candidates)
    {
        std::set<std::wstring> values;
        for (auto const& candidate : Objects(candidates)) values.insert(Str(candidate, L"value"));
        return values.size();
    }

    void MainWindow::RenderDispatch()
    {
        if (!dispatchList || !search) return;
        dispatchList.Children().Clear();
        auto visible = VisibleResults();
        dispatchPage = std::clamp(dispatchPage, 0, std::max(0, (static_cast<int>(visible.size()) - 1) / dispatchPageSize));
        visibleCount = static_cast<int>(visible.size());
        visibleAmount = Sum(visible, L"amount");
        std::set<std::wstring> cars;
        for (auto const& row : visible) cars.insert(Str(row, L"plate"));
        visibleCars = static_cast<int>(cars.size());
        std::wstring label = L"전체";
        for (auto const& view : DispatchViews) if (dispatchFilter == view.key) label = view.label;
        dispatchSubtitle.Text(label + L" · " + N0(visibleCars) + L"대");
        UpdateDispatchSummary();
        RenderDispatchPager();
        dispatchEmpty.Visibility(Shown(visible.empty()));
        dispatchEmptyTitle.Text(results.empty() ? L"조회할 차량목록을 준비해 주세요" : L"조건에 맞는 차량이 없어요");
        dispatchEmptyText.Text(results.empty()
            ? L"오른쪽 입력 패널에 차량목록을 넣고 업체 파일을 끌어다 놓으면\n완료 차량의 금액과 확인할 목록을 정리해 드려요."
            : L"탐색 창에서 다른 보기를 고르거나 검색어·업체·상태를 확인해 주세요.");
        auto first = static_cast<size_t>(dispatchPage) * dispatchPageSize;
        std::vector<JObj> shown(visible.begin() + std::min(first, visible.size()), visible.begin() + std::min(first + dispatchPageSize, visible.size()));
        auto described = rules.Describe(shown);
        for (size_t lineIndex = 0; lineIndex < shown.size(); lineIndex++)
        {
            auto const& row = shown[lineIndex];
            auto id = Str(row, L"id"), plate = Str(row, L"plate");
            auto const& statusLabel = described[lineIndex].label;
            auto const& notes = described[lineIndex].notes;

            // 줄 안의 처리기는 줄·자기 자신을 약한 참조로만 잡아요 (서로 붙잡아 메모리에 남지 않게).
            auto lineRef = std::make_shared<weak_ref<Border>>();
            auto highlight = [this, id, lineIndex, lineRef]
            {
                if (auto line = lineRef->get()) line.Background(selected.contains(id) ? rowSelected : lineIndex % 2 == 1 ? rowAlt : transparent);
            };
            CheckBox check;
            check.IsChecked(selected.contains(id));
            check.MinWidth(0);
            check.Padding(Th(0));
            check.VerticalAlignment(VerticalAlignment::Center);
            Name(check, plate + L" 행 선택");
            check.Checked([this, id, highlight](auto&&, auto&&) { selected.insert(id); UpdateDispatchSummary(); highlight(); });
            check.Unchecked([this, id, highlight](auto&&, auto&&) { selected.erase(id); UpdateDispatchSummary(); highlight(); });
            CheckBox done;
            done.IsChecked(completed.contains(plate));
            done.MinWidth(0);
            done.Padding(Th(0));
            done.HorizontalAlignment(HorizontalAlignment::Center);
            Name(done, plate + L" 완료");
            auto workStatus = Text(L"", 11);
            workStatus.MinHeight(14);
            workStatus.VerticalAlignment(VerticalAlignment::Bottom);
            workStatus.Margin(Th(28, 0, 0, 0));
            workStatus.IsHitTestVisible(false);
            auto plateRef = std::make_shared<weak_ref<Control>>();
            auto updateWork = [this, plate, plateRef, statusRef = make_weak(workStatus)]
            {
                auto isDone = completed.contains(plate), isCopied = copiedDispatch.contains(plate);
                if (auto button = plateRef->get()) button.Foreground(isDone ? green.Solid : isCopied ? blue.Solid : indigo.Solid);
                if (auto text = statusRef.get())
                {
                    text.Text(isDone ? (isCopied ? L"완료 · 복사함" : L"완료") : isCopied ? L"복사함" : L"");
                    text.Foreground(isDone ? green.Solid : blue.Solid);
                }
            };
            done.Checked([this, plate, updateWork](auto&&, auto&&) { completed.insert(plate); SaveWorkMarks(); updateWork(); });
            done.Unchecked([this, plate, updateWork](auto&&, auto&&) { completed.erase(plate); SaveWorkMarks(); updateWork(); });

            std::vector<std::wstring> facts;
            for (auto key : { L"rawState", L"carrierType", L"origin" })
                if (auto value = Str(row, key); !value.empty()) facts.push_back(value);
            auto content = Stack({ Text(Join(facts, L" · ")) });
            for (auto const& [field, fieldLabel] : std::vector<std::pair<std::wstring, std::wstring>>{ { L"schedule", L"탁송일정" }, { L"fee", L"탁송비용" } })
            {
                auto candidates = Arr(row, field + L"Candidates");
                if (!Flag(row, field + L"Conflict") && DistinctValues(candidates) < 2) continue;
                // 일정이 중첩이면 비용은 고른 일정의 기록을 따라가서 따로 고르지 않아요.
                if (field == L"fee" && DistinctValues(Arr(row, L"scheduleCandidates")) > 1) continue;
                ComboBox combo;
                combo.Header(box_value(hstring(fieldLabel + L" 원본 기록 선택")));
                combo.HorizontalAlignment(HorizontalAlignment::Stretch);
                for (auto const& candidate : Objects(candidates))
                {
                    ComboBoxItem item;
                    item.Content(box_value(hstring(Str(candidate, L"value") + L" · " + Str(candidate, L"source"))));
                    item.Tag(box_value(hstring(Str(candidate, L"id"))));
                    combo.Items().Append(item);
                }
                combo.SelectionChanged([this, id, field](IInspectable const& sender, auto&&)
                {
                    auto item = sender.as<ComboBox>().SelectedItem().try_as<ComboBoxItem>();
                    if (!item) return;
                    std::wstring candidate(unbox_value_or<hstring>(item.Tag(), L""));
                    // 표를 다시 그리면 이 상자도 없어지니, 선택 처리가 끝난 뒤에 반영해요.
                    queue.TryEnqueue([this, id, field, candidate]
                    {
                        Guard([&]
                        {
                            auto found = std::find_if(results.begin(), results.end(), [&](auto const& r) { return Str(r, L"id") == id; });
                            if (found == results.end()) return;
                            *found = rules.Choose(*found, field, candidate);
                            RecountDispatch();
                            RenderDispatch();
                        });
                    });
                });
                content.Children().Append(combo);
            }
            if (!notes.empty()) content.Children().Append(Text(notes));
            content.Children().Append(Text(Str(row, L"sources"), 12));
            content.MaxWidth(420);
            // 조회 상태: 색 알약
            auto statusText = Text(statusLabel, 13);
            auto pill = Pill(statusText);
            TintPill(pill, DispatchStatusSwatch({ Str(row, L"status"), statusLabel, Flag(row, L"slackPending") }));
            auto status = Stack({ pill });
            status.Spacing(2);
            if (Flag(row, L"slackReviewed")) status.Children().Append(Muted(L"슬랙봇 회신", 11));
            auto needsChoice = Flag(row, L"scheduleConflict") || Flag(row, L"feeConflict");
            if (needsChoice)
            {
                auto flagged = Text(L"일정·비용 선택 필요", 11);
                flagged.Foreground(orange.Solid);
                status.Children().Append(flagged);
            }
            Button details;
            details.Content(box_value(needsChoice ? L"선택" : L"보기"));
            Flyout flyout;
            flyout.Content(content);
            details.Flyout(flyout);
            details.HorizontalAlignment(HorizontalAlignment::Center);
            if (needsChoice) details.Style(AccentStyle());
            Name(details, plate + L" 상세");
            Action markCopied = [this, plate, updateWork] { copiedDispatch.insert(plate); SaveWorkMarks(); updateWork(); };
            auto plateCell0 = CopyCell(L"차량번호", plate, plate, false, markCopied);
            if (auto plateButton = plateCell0.try_as<Control>())
            {
                plateButton.FontWeight(SemiBold());
                *plateRef = make_weak(plateButton);
            }
            updateWork();
            auto plateLink = Bar({ check, plateCell0 });
            plateLink.VerticalAlignment(VerticalAlignment::Center);
            Grid plateCell;
            plateCell.MinHeight(44);
            plateCell.Children().Append(plateLink);
            plateCell.Children().Append(workStatus);
            auto schedule = Str(row, L"schedule");
            auto vendor = Text(Str(row, L"vendor"), 13);
            auto entry = Columns(dispatchColumns, { plateCell, CopyCell(L"탁송일정", schedule, schedule, false, markCopied),
                CopyCell(L"주유·충전비", Flag(row, L"slackPending") ? std::wstring(Rules::SlackPending) : Str(row, L"amount"), Money(row, L"amount"), true, markCopied),
                CopyCell(L"탁송비", Str(row, L"dispatchFee"), Money(row, L"dispatchFee"), true, markCopied),
                CopyCell(L"부가세", Str(row, L"vat"), Money(row, L"vat"), true, markCopied),
                status, vendor, done, details });
            auto line = TableRow(entry, static_cast<int>(lineIndex));
            *lineRef = make_weak(line);
            highlight();
            dispatchList.Children().Append(line);
        }
    }

    FrameworkElement MainWindow::CopyCell(std::wstring_view label, std::wstring const& value, std::wstring const& display, bool right, Action onCopy)
    {
        if (Trim(value).empty() || value == L"—")
        {
            auto text = Text(L"—");
            text.Foreground(faintInk);
            if (right) text.TextAlignment(TextAlignment::Right);
            return text;
        }
        HyperlinkButton button;
        button.Content(box_value(hstring(display)));
        button.Padding(Th(0, 4, 0, 4));
        button.HorizontalAlignment(right ? HorizontalAlignment::Right : HorizontalAlignment::Left);
        button.VerticalAlignment(VerticalAlignment::Center);
        Tip(button, std::wstring(label) + L" 복사");
        Name(button, std::wstring(label) + L" 복사: " + display);
        button.Click([this, value, onCopy](auto&&, auto&&) { Copy(value, onCopy); });
        return button;
    }

    void MainWindow::ChangeDispatchPage(int page)
    {
        dispatchPage = page;
        RenderDispatch();
        dispatchScroll.ChangeView(nullptr, 0.0, nullptr);
    }

    // 페이지 번호: 현재 페이지 주변 7개와 첫·마지막 페이지를 보여줘요.
    void MainWindow::RenderDispatchPager()
    {
        auto pageCount = DispatchPageCount();
        previousResults.IsEnabled(dispatchPage > 0);
        nextResults.IsEnabled(dispatchPage + 1 < pageCount);
        dispatchPageNumbers.Children().Clear();
        if (pageCount <= 1) return;
        auto number = [this](int page)
        {
            auto button = MakeButton(std::to_wstring(page + 1), nullptr, page == dispatchPage);
            // 눌린 단추가 다시 그리는 중에 사라지지 않게 처리가 끝난 뒤 페이지를 바꿔요.
            button.Click([this, page](auto&&, auto&&) { queue.TryEnqueue([this, page] { Guard([&] { ChangeDispatchPage(page); }); }); });
            button.MinWidth(32);
            button.Padding(Th(6, 4, 6, 4));
            Name(button, std::to_wstring(page + 1) + L"페이지");
            dispatchPageNumbers.Children().Append(button);
        };
        auto gap = [this]
        {
            auto text = Text(L"…");
            text.VerticalAlignment(VerticalAlignment::Center);
            dispatchPageNumbers.Children().Append(text);
        };
        auto from = std::max(0, dispatchPage - 3);
        auto to = std::min(pageCount - 1, from + 6);
        from = std::max(0, to - 6);
        if (from > 0) { number(0); if (from > 1) gap(); }
        for (auto page = from; page <= to; page++) number(page);
        if (to < pageCount - 1) { if (to < pageCount - 2) gap(); number(pageCount - 1); }
    }

    void MainWindow::SetDispatchPageSize(int size)
    {
        if (std::find(DispatchPageSizes.begin(), DispatchPageSizes.end(), size) == DispatchPageSizes.end() || size == dispatchPageSize) return;
        auto firstRow = dispatchPage * dispatchPageSize;
        dispatchPageSize = size;
        WriteFile(LocalState::DataFile(L"dispatch-page-size.txt"), std::to_string(size));
        ChangeDispatchPage(firstRow / size);
    }

    void MainWindow::LoadDispatchPageSize()
    {
        auto saved = ReadFile(LocalState::DataFile(L"dispatch-page-size.txt"));
        if (!saved) return;
        auto size = _wtoi(Trim(ToWide(*saved)).c_str());
        if (std::find(DispatchPageSizes.begin(), DispatchPageSizes.end(), size) != DispatchPageSizes.end()) dispatchPageSize = size;
    }

    void MainWindow::UpdateDispatchSummary()
    {
        auto first = visibleCount == 0 ? 0 : dispatchPage * dispatchPageSize + 1;
        auto last = std::min(visibleCount, (dispatchPage + 1) * dispatchPageSize);
        dispatchSummary.Text(results.empty()
            ? std::wstring(L"차량목록과 업체 파일을 넣고 조회하세요.")
            : L"전체 " + N0(static_cast<double>(results.size())) + L"대 · 주유·충전비 " + N0(visibleAmount) + L"원 · " + N0(first) + L"–" + N0(last) + L"줄 ("
                + std::to_wstring(dispatchPage + 1) + L"/" + std::to_wstring(DispatchPageCount()) + L"페이지)");
        dispatchCountText.Text(results.empty() ? std::wstring()
            : N0(visibleCount) + L"줄 · " + N0(visibleCars) + L"대" + (selected.empty() ? L"" : L" · 선택 " + N0(static_cast<double>(selected.size())) + L"대"));
    }

    std::wstring MainWindow::Money(JObj const& row, std::wstring_view field)
    {
        if (field == L"amount" && Flag(row, L"slackPending")) return Rules::SlackPending;
        auto number = ParseNumber(Str(row, field));
        return number ? N0(*number) + L"원" : L"—";
    }

    double MainWindow::Sum(std::vector<JObj> const& rows, std::wstring_view field)
    {
        double total = 0;
        for (auto const& row : rows) total += ParseNumber(Str(row, field)).value_or(0);
        return total;
    }

    Async MainWindow::ExportDispatch(std::wstring extension)
    {
        auto data = ExportScope();
        std::wstring path(co_await SavePicker(L"탁송체크_" + Now(L"%Y%m%d_%H%M"), extension));
        if (path.empty()) co_return;
        if (extension == L".xlsx") Spreadsheets::Write(path, rules.ExportRows(data));
        else if (!WriteFile(path, ToUtf8(rules.ExportText(data)))) throw AppError{ L"파일을 저장하지 못했어요. 저장 위치와 권한을 확인해 주세요." };
        Notify(L"조회 결과를 저장했어요.");
    }

    /// 오른쪽 패널 보이기/숨기기 단추 (채움 없는 아이콘 단추)
    AppBarButton MainWindow::PanelToggle(std::wstring_view label, std::function<FrameworkElement()> panel)
    {
        AppBarButton button;
        button.Label(hstring(label));
        button.Icon(Icon(L""));
        button.LabelPosition(CommandBarLabelPosition::Collapsed);
        button.Width(48);
        Tip(button, std::wstring(label) + L" 보이기/숨기기");
        Name(button, std::wstring(label) + L" 숨기기");
        button.Click([panel](IInspectable const& sender, auto&&) { SetPanelVisible(sender.as<AppBarButton>(), panel(), !IsShown(panel())); });
        return button;
    }

    void MainWindow::SetPanelVisible(AppBarButton const& button, FrameworkElement const& panel, bool visible)
    {
        panel.Visibility(Shown(visible));
        button.Icon().as<FontIcon>().Glyph(visible ? L"" : L"");
        Name(button, std::wstring(button.Label()) + (visible ? L" 숨기기" : L" 보이기"));
    }

    // 차량목록: 붙여넣기처럼 한 번에 2글자 이상 바뀌면 빈 줄(공백·탭만 있는 줄 포함)을 바로 지워요.
    // 한 글자씩 칠 때(Enter 등)는 그대로 둬서 줄을 추가할 수 있어요. '정리' 단추(날짜·시간 맞추기)는 따로 있어요.

    /// 빈 줄을 지워요. 원래 끝이 줄바꿈이면 마지막 줄바꿈 하나는 남겨요.
    std::wstring MainWindow::RemoveBlankLines(std::wstring_view text)
    {
        auto normalized = NormalizeLines(text);
        std::vector<std::wstring> kept;
        for (auto const& line : Split(normalized, L'\n'))
            if (!Trim(line).empty()) kept.push_back(line);
        auto joined = Join(kept, L"\n");
        return !joined.empty() && normalized.ends_with(L'\n') ? joined + L"\n" : joined;
    }

    /// 바뀐 글자 수 (앞뒤 같은 부분을 뺀 넣은·지운 글자 중 큰 쪽)
    static size_t ChangedCharacters(std::wstring const& before, std::wstring const& after)
    {
        size_t prefix = 0;
        while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) prefix++;
        size_t suffix = 0;
        while (suffix < before.size() - prefix && suffix < after.size() - prefix
            && before[before.size() - suffix - 1] == after[after.size() - suffix - 1]) suffix++;
        return std::max(before.size() - prefix - suffix, after.size() - prefix - suffix);
    }

    void MainWindow::OnVehiclesChanged()
    {
        std::wstring now(vehicles.Text());
        // 프로그램이 정리해 넣은 글자면 조회 결과를 지우지 않아요.
        if (pendingVehicleText && NormalizeLines(now) == *pendingVehicleText)
        {
            pendingVehicleText.reset();
            lastVehicleText = now;
            UpdateVehicleCount();
            return;
        }
        // 같은 글자로 다시 알려 오는 변경(정리해 넣은 뒤 늦게 오는 알림 등)은 무시해요.
        if (NormalizeLines(now) == NormalizeLines(lastVehicleText)) { pendingVehicleText.reset(); return; }
        auto changed = ChangedCharacters(NormalizeLines(lastVehicleText), NormalizeLines(now));
        lastVehicleText = now;
        UpdateVehicleCount();
        InvalidateDispatch();
        if (changed < 2) return;
        auto cleaned = RemoveBlankLines(now);
        if (cleaned == NormalizeLines(now)) return;
        // 커서 위치 유지: 커서 앞부분에서 지워진 빈 줄만큼 당겨요.
        auto caret = std::clamp(vehicles.SelectionStart(), 0, static_cast<int>(now.size()));
        SetVehiclesQuietly(cleaned, static_cast<int>(std::min(RemoveBlankLines(now.substr(0, caret)).size(), cleaned.size())));
    }

    void MainWindow::SetVehiclesQuietly(std::wstring const& text, int caret)
    {
        pendingVehicleText = text;
        vehicles.Text(text);
        lastVehicleText = vehicles.Text();
        vehicles.SelectionStart(std::clamp(caret, 0, static_cast<int>(vehicles.Text().size())));
        vehicles.SelectionLength(0);
    }
}
