#include "pch.h"
#include "MainWindow.h"
#include <winrt/Microsoft.UI.Interop.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Windowing;
using winrt::Windows::Foundation::Uri;

namespace wt
{
    MainWindow::MainWindow()
    {
        BuildWindow();
        window.Title(L"WorkTool");
        queue = window.DispatcherQueue();
        appWindow = window.AppWindow();
        InitColors();
        LoadWorkToolTheme();
        UpdateWorkColors();
        ObserveSystemColors();
        window.SystemBackdrop(Media::MicaBackdrop());
        window.ExtendsContentIntoTitleBar(true);
        window.SetTitleBar(header); // 시스템 캡션 단추가 Snap Layouts·접근성·DPI 처리를 그대로 해 줘요.
        appWindow.Resize({ 1440, 960 });
        // 작업 표시줄·Alt+Tab 아이콘. 실행 파일에 들어간 아이콘과 같은 파일이에요.
        auto iconPath = AppDirectory() / L"Assets" / L"WorkTool.ico";
        std::error_code ignored;
        if (std::filesystem::exists(iconPath, ignored)) appWindow.SetIcon(iconPath.wstring());
        auto titleBar = appWindow.TitleBar();
        titleBar.PreferredHeightOption(TitleBarHeightOption::Tall);
        titleBar.ButtonBackgroundColor(winrt::Microsoft::UI::Colors::Transparent());
        titleBar.ButtonInactiveBackgroundColor(winrt::Microsoft::UI::Colors::Transparent());
        root.ActualThemeChanged([this](auto&&, auto&&) { UpdateCaptionColors(); UpdateWorkColors(); });
        UpdateCaptionColors();
        appWindow.Closing([this](auto&&, AppWindowClosingEventArgs const& args)
        {
            if (allowClose) return;
            if (saving || confirmingClose) { args.Cancel(true); return; }
            if (!HasUnsavedTransfers()) return;
            args.Cancel(true);
            ConfirmClose();
        });
        BuildNavigation();
        // 탐색 창의 대수·단계 표시를 위해 두 업무 화면은 처음부터 만들어 둬요.
        pages.insert_or_assign(L"dispatch", DispatchPage());
        pages.insert_or_assign(L"transfer", TransferPage());
        Select(L"home");
        int count = 0;
        auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (arguments && count == 4 && std::wstring_view(arguments[1]) == L"--smoke-test")
        {
            std::wstring output = arguments[2], fixtures = arguments[3];
            auto started = std::make_shared<bool>(false);
            root.Loaded([this, output, fixtures, started](auto&&, auto&&)
            {
                if (std::exchange(*started, true)) return;
                Run([this, output, fixtures] { return SmokeTest(output, fixtures); });
            });
        }
        if (arguments) LocalFree(arguments);
    }

    // 제목 표시줄과 왼쪽 탐색 창은 Mica, 내용 영역은 NavigationView 기본 면이에요.
    void MainWindow::BuildWindow()
    {
        window = Window();
        root = Grid();
        AddRow(root, Px(48));
        AddRow(root, Star());
        header = Grid();
        Image icon;
        icon.Width(16);
        icon.Height(16);
        try { icon.Source(Media::Imaging::BitmapImage(Uri((AppDirectory() / L"Assets" / L"WorkTool.ico").wstring()))); }
        catch (hresult_error const&) {}
        TextBlock title;
        title.Text(L"WorkTool 업무도구");
        title.FontSize(12);
        title.VerticalAlignment(VerticalAlignment::Center);
        title.FontFamily(AppFont());
        auto caption = Bar({ icon, title });
        caption.Spacing(12);
        caption.Margin(Th(16, 0, 150, 0));
        caption.VerticalAlignment(VerticalAlignment::Center);
        header.Children().Append(caption);
        root.Children().Append(header);

        navigation = NavigationView();
        navigation.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        navigation.IsSettingsVisible(true);
        navigation.FontFamily(AppFont());
        navigation.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        navigation.OpenPaneLength(240);
        navigation.IsPaneOpen(true);
        navigation.IsPaneToggleButtonVisible(false);
        navigation.IsTitleBarAutoPaddingEnabled(false);
        pageHost = Grid();
        notice = InfoBar();
        notice.IsOpen(false);
        notice.IsClosable(true);
        notice.VerticalAlignment(VerticalAlignment::Bottom);
        notice.HorizontalAlignment(HorizontalAlignment::Center);
        notice.MaxWidth(640);
        notice.Margin(Th(16, 0, 16, 48));
        Grid content;
        content.Children().Append(pageHost);
        content.Children().Append(notice);
        navigation.Content(content);
        Place(root, navigation, 1);
        window.Content(root);
        navigation.SelectionChanged([this](auto&&, NavigationViewSelectionChangedEventArgs const& args)
        {
            if (syncingNav) return;
            std::wstring key = L"home";
            if (args.IsSettingsSelected()) key = L"settings";
            else if (auto item = args.SelectedItem().try_as<NavigationViewItem>()) key = unbox_value_or<hstring>(item.Tag(), L"home");
            Guard([&] { ActivateNav(key); });
        });
    }

    HWND MainWindow::Handle() { return winrt::Microsoft::UI::GetWindowFromWindowId(appWindow.Id()); }

    void MainWindow::UpdateCaptionColors()
    {
        auto dark = root.ActualTheme() == ElementTheme::Dark;
        auto titleBar = appWindow.TitleBar();
        titleBar.ButtonForegroundColor(dark ? winrt::Microsoft::UI::Colors::White() : winrt::Microsoft::UI::Colors::Black());
        titleBar.ButtonInactiveForegroundColor(dark ? winrt::Microsoft::UI::Colors::Gray() : winrt::Microsoft::UI::Colors::DimGray());
    }

    // ───────────── 탐색 창 ─────────────
    void MainWindow::BuildNavigation()
    {
        auto items = navigation.MenuItems();
        auto headerItem = [](wchar_t const* text)
        {
            NavigationViewItemHeader item;
            item.Content(box_value(text));
            return item;
        };
        // 최상위 항목: 시스템 설정처럼 20px 색 타일 위 흰 아이콘
        items.Append(NavItem(L"home", L"홈", nullptr, false, Tile(indigo, L"")));
        items.Append(headerItem(L"업무 도구"));
        auto dispatch = NavItem(L"dispatch", L"탁송체크", nullptr, false, Tile(accent, L""));
        dispatch.IsExpanded(true);
        // 탁송체크 보기: 색 아이콘
        std::map<std::wstring, std::pair<wchar_t const*, Swatch*>> viewIcons{
            { L"all", { L"", &teal } }, { L"review", { L"", &orange } }, { L"confirmed", { L"", &green } },
            { L"progress", { L"", &yellow } }, { L"missing", { L"", &red } }, { L"jeju", { L"", &blue } },
            { L"carrier", { L"", &brown } }, { L"slack", { L"", &purple } }, { L"slack-replies", { L"", &pink } } };
        for (auto const& view : DispatchViews)
        {
            auto const& [glyph, color] = viewIcons[view.key];
            auto icon = Icon(glyph);
            icon.Foreground(color->Solid);
            dispatch.MenuItems().Append(NavItem(std::wstring(L"d:") + view.key, view.label, icon, true, nullptr));
        }
        navItems.at(L"d:slack-replies").Visibility(Visibility::Collapsed);
        items.Append(dispatch);
        auto transfer = NavItem(L"transfer", L"하나은행 이체파일", nullptr, false, Tile(blue, L""));
        transfer.IsExpanded(true);
        // 이체 단계: 채운 숫자 원 (파랑·indigo·purple), 끝난 단계는 초록 ✓
        Swatch* stepColors[]{ &blue, &indigo, &purple };
        for (int i = 1; i <= 3; i++)
        {
            TextBlock number;
            number.Text(to_hstring(i));
            number.FontSize(11);
            number.FontWeight(SemiBold());
            number.Foreground(onTile);
            number.HorizontalAlignment(HorizontalAlignment::Center);
            number.VerticalAlignment(VerticalAlignment::Center);
            Border circle;
            circle.Width(20);
            circle.Height(20);
            circle.CornerRadius(Radius(10));
            circle.Background(stepColors[i - 1]->Tile);
            circle.Child(number);
            circle.VerticalAlignment(VerticalAlignment::Center);
            stepMarks[i] = { circle, number, stepColors[i - 1] };
            transfer.MenuItems().Append(NavItem(L"t:" + std::to_wstring(i), TransferStepNames[i - 1], nullptr, i == 2, circle));
        }
        items.Append(transfer);
        items.Append(headerItem(L"도움말"));
        items.Append(NavItem(L"help", L"사용법", nullptr, false, Tile(orange, L"")));
        items.Append(NavItem(L"changelog", L"변경 이력", nullptr, false, Tile(purple, L"")));
        navigation.Loaded([this](auto&&, auto&&)
        {
            auto settings = navigation.SettingsItem().try_as<NavigationViewItem>();
            if (!settings) return;
            settings.Icon(nullptr);
            settings.Content(NavContent(L"설정", Tile(gray, L""), nullptr));
        });
    }

    NavigationViewItem MainWindow::NavItem(std::wstring const& key, std::wstring_view label, IconElement const& icon, bool count, UIElement const& lead)
    {
        NavigationViewItem item;
        item.Tag(box_value(hstring(key)));
        if (icon) item.Icon(icon);
        item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        Name(item, label);
        TextBlock number{ nullptr };
        if (count)
        {
            number = TextBlock();
            number.FontSize(12);
            number.Foreground(mutedInk);
            number.VerticalAlignment(VerticalAlignment::Center);
            number.Margin(Th(8, 0, 4, 0));
            navCounts.insert_or_assign(key, number);
        }
        item.Content(NavContent(label, lead, number));
        navItems.insert_or_assign(key, item);
        return item;
    }

    /// 항목 안쪽: [색 타일][이름][오른쪽 숫자]
    Grid MainWindow::NavContent(std::wstring_view label, UIElement const& lead, TextBlock const& number)
    {
        Grid grid;
        grid.ColumnSpacing(12);
        AddColumn(grid, AutoSize());
        AddColumn(grid, Star());
        AddColumn(grid, AutoSize());
        if (lead) grid.Children().Append(lead);
        else grid.ColumnSpacing(0);
        TextBlock text;
        text.Text(hstring(label));
        text.VerticalAlignment(VerticalAlignment::Center);
        text.TextTrimming(TextTrimming::CharacterEllipsis);
        Place(grid, text, 0, 1);
        if (number) Place(grid, number, 0, 2);
        return grid;
    }

    /// 탐색 항목을 골라요. activate=false면 표시만 맞춰요 (화면 안에서 이미 바뀐 경우).
    void MainWindow::Select(std::wstring const& key, bool activate)
    {
        auto found = navItems.find(key);
        if (found == navItems.end()) return;
        auto const& item = found->second;
        if (!activate)
        {
            currentNav = key;
            syncingNav = true;
            try { navigation.SelectedItem(item); } catch (...) {}
            syncingNav = false;
            return;
        }
        if (navigation.SelectedItem() == item) ActivateNav(key);
        else navigation.SelectedItem(item);
    }

    void MainWindow::ActivateNav(std::wstring const& key)
    {
        // 부모 항목을 누르면 지금 보던 하위 항목으로 가요
        if (key == L"dispatch") { Select(L"d:" + dispatchFilter); return; }
        if (key == L"transfer") { Select(L"t:" + std::to_wstring(step)); return; }
        currentNav = key;
        auto page = key.starts_with(L"d:") ? L"dispatch" : key.starts_with(L"t:") ? L"transfer" : key;
        ShowPage(page);
        if (key.starts_with(L"d:")) SetDispatchFilter(key.substr(2));
        else if (key.starts_with(L"t:") && step != key[2] - L'0') Step(key[2] - L'0');
    }

    void MainWindow::ShowPage(std::wstring const& key)
    {
        auto found = pages.find(key);
        if (found == pages.end())
        {
            auto page = key == L"help" ? HelpPage() : key == L"settings" ? SettingsPage() : key == L"changelog" ? ChangeLogPage() : HomePage();
            found = pages.insert_or_assign(key, page).first;
        }
        auto children = pageHost.Children();
        if (children.Size() == 1 && children.GetAt(0) == found->second) return;
        children.Clear();
        children.Append(found->second);
    }

    void MainWindow::Go(std::wstring const& key)
    {
        Select(key == L"dispatch" ? L"d:" + dispatchFilter : key == L"transfer" ? L"t:" + std::to_wstring(step) : key);
    }

    /// 탁송체크 하위 항목 오른쪽 대수, '슬랙 회신 반영' 항목 보이기
    void MainWindow::UpdateDispatchNav()
    {
        if (navCounts.empty()) return;
        for (auto const& view : DispatchViews)
            navCounts.at(std::wstring(L"d:") + view.key).Text(results.empty() ? L"" : N0(DispatchCount(view.key)));
        auto replies = std::any_of(results.begin(), results.end(), [](auto const& row) { return Flag(row, L"slackReviewed"); });
        navItems.at(L"d:slack-replies").Visibility(Shown(replies));
        if (currentNav.starts_with(L"d:") && currentNav != L"d:" + dispatchFilter) Select(L"d:" + dispatchFilter, false);
    }

    /// 이체파일 단계 표시: 끝난 단계는 ✓, 2단계 오른쪽에 건수
    void MainWindow::UpdateTransferNav()
    {
        if (stepMarks.empty()) return;
        auto bad = transfers.empty() ? 0 : lastBad;
        for (int i = 1; i <= 3; i++)
        {
            auto done = !transfers.empty() && (i == 1 || (i == 2 && step == 3 && bad == 0) || (i == 3 && savedSignature == Signature()));
            auto& mark = stepMarks[i];
            mark.circle.Background(done ? green.Tile : mark.color->Tile);
            mark.number.Text(done ? hstring(L"") : to_hstring(i));
            mark.number.FontFamily(done ? Media::FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets") : AppFont());
        }
        navCounts.at(L"t:2").Text(transfers.empty() ? L"" : N0(static_cast<double>(transfers.size())));
        auto current = L"t:" + std::to_wstring(step);
        if (currentNav.starts_with(L"t:") && currentNav != current) Select(current, false);
    }

    // ───────────── 공용 도구 ─────────────
    TextBlock MainWindow::Muted(std::wstring_view value, double size)
    {
        auto text = Text(value, size);
        text.Foreground(mutedInk);
        return text;
    }

    /// 구역: 카드 상자 없이 아래 1px 구분선으로만 나눠요.
    Border MainWindow::Card(UIElement const& child)
    {
        Border card;
        card.Child(child);
        card.Padding(Th(0, 16, 0, 20));
        card.BorderThickness(Th(0, 0, 0, 1));
        card.BorderBrush(divider);
        return card;
    }

    void MainWindow::Guard(Action const& action)
    {
        try { action(); }
        catch (hresult_error const& error) { Notify(error.message(), true); }
        catch (std::exception const&) { Notify(L"작업을 끝내지 못했어요.", true); }
    }

    fire_and_forget MainWindow::Run(AsyncAction action)
    {
        std::wstring failure;
        try { co_await action(); }
        catch (hresult_canceled const&) {}
        catch (hresult_error const& error) { failure = error.message(); }
        catch (std::exception const&) { failure = L"작업을 끝내지 못했어요."; }
        if (!failure.empty()) Notify(failure, true);
    }

    fire_and_forget MainWindow::RunButton(Button button, AsyncAction action)
    {
        button.IsEnabled(false);
        std::wstring failure;
        try { co_await action(); }
        catch (hresult_canceled const&) {}
        catch (hresult_error const& error) { failure = error.message(); }
        catch (std::exception const&) { failure = L"작업을 끝내지 못했어요."; }
        if (!failure.empty()) Notify(failure, true);
        if (button == download) ValidateTransfers();
        else button.IsEnabled(true);
    }

    Button MainWindow::AsyncButton(std::wstring_view title, AsyncAction action, bool primary)
    {
        auto button = MakeButton(title, nullptr, primary);
        button.Click([this, action](IInspectable const& sender, auto&&) { RunButton(sender.as<Button>(), action); });
        return button;
    }

    /// CommandBar 단추 (아이콘 + 글자, 배경 없음). 아이콘만 색을 입히고 글자는 기본색이에요.
    AppBarButton MainWindow::CommandAsync(std::wstring_view label, std::wstring_view glyph, AsyncAction action, Swatch* color)
    {
        auto icon = Icon(glyph);
        if (color) icon.Foreground(color->Solid);
        AppBarButton button;
        button.Label(hstring(label));
        button.Icon(icon);
        button.Click([this, action](auto&&, auto&&) { Run(action); });
        return button;
    }

    AppBarButton MainWindow::Command(std::wstring_view label, std::wstring_view glyph, Action action, Swatch* color)
    {
        auto icon = Icon(glyph);
        if (color) icon.Foreground(color->Solid);
        AppBarButton button;
        button.Label(hstring(label));
        button.Icon(icon);
        button.Click([this, action](auto&&, auto&&) { Guard(action); });
        return button;
    }

    void MainWindow::Notify(std::wstring_view message, bool error)
    {
        notice.Message(hstring(message));
        notice.Severity(error ? InfoBarSeverity::Error : InfoBarSeverity::Success);
        notice.IsOpen(true);
    }

    /// H1 + 회색 부제
    StackPanel MainWindow::PageTitle(std::wstring_view title, TextBlock const& subtitle)
    {
        subtitle.Foreground(mutedInk);
        subtitle.FontSize(14);
        StackPanel stack;
        stack.Spacing(2);
        stack.Margin(Th(24, 20, 24, 8));
        stack.Children().Append(TitleText(title));
        stack.Children().Append(subtitle);
        return stack;
    }

    /// 표 아래 상태 줄 (12px 회색, 위에 구분선)
    Border MainWindow::StatusLine(UIElement const& left, FrameworkElement const& right)
    {
        Grid grid;
        grid.ColumnSpacing(16);
        AddColumn(grid, Star());
        AddColumn(grid, AutoSize());
        grid.Children().Append(left);
        Place(grid, right, 0, 1);
        Border line;
        line.Child(grid);
        line.BorderBrush(divider);
        line.BorderThickness(Th(0, 1, 0, 0));
        line.Padding(Th(24, 6, 16, 6));
        return line;
    }

    /// 오른쪽 패널 (320px, 왼쪽 구분선)
    Border MainWindow::SidePanel(UIElement const& content)
    {
        Border panel;
        panel.Child(content);
        panel.Width(320);
        panel.BorderBrush(divider);
        panel.BorderThickness(Th(1, 0, 0, 0));
        return panel;
    }

    /// 표 머리글: 배경 없이 12px 회색 글자, 아래 구분선
    Grid MainWindow::TableHeader(std::vector<double> const& widths, std::vector<std::wstring> const& labels)
    {
        std::vector<FrameworkElement> cells;
        for (auto const& label : labels)
        {
            auto text = Text(label, 12);
            text.FontWeight(SemiBold());
            text.Foreground(mutedInk);
            if (label == L"주유·충전비" || label == L"탁송비" || label == L"부가세" || label == L"입금액") text.TextAlignment(TextAlignment::Right);
            if (label == L"차량번호")
            {
                text.Margin(Th(28, 0, 0, 0));
                text.TextWrapping(TextWrapping::NoWrap);
                text.TextTrimming(TextTrimming::CharacterEllipsis);
            }
            if (label == L"완료" || label == L"상세") text.TextAlignment(TextAlignment::Center);
            cells.push_back(text);
        }
        auto row = Columns(widths, cells);
        row.MinHeight(36);
        row.BorderBrush(divider);
        row.BorderThickness(Th(0, 0, 0, 1));
        return row;
    }

    Border MainWindow::TableRow(UIElement const& content, int index)
    {
        Border row;
        row.Child(content);
        row.Background(index % 2 == 1 ? rowAlt : transparent);
        return row;
    }

    Async MainWindow::Delay(int milliseconds)
    {
        auto ui = queue;
        co_await resume_after(std::chrono::milliseconds(milliseconds));
        co_await ResumeOn{ ui };
    }

    ContentDialog MainWindow::Dialog(std::wstring_view title, UIElement const& content, std::wstring_view primary)
    {
        ContentDialog dialog;
        dialog.XamlRoot(root.XamlRoot());
        dialog.Title(box_value(hstring(title)));
        dialog.Content(content);
        dialog.PrimaryButtonText(hstring(primary));
        dialog.CloseButtonText(L"취소");
        dialog.DefaultButton(ContentDialogButton::Close);
        dialog.RequestedTheme(root.ActualTheme());
        return dialog;
    }

    fire_and_forget MainWindow::ConfirmClose()
    {
        confirmingClose = true;
        auto failed = false;
        try
        {
            auto dialog = Dialog(L"저장하지 않은 내용이 있어요", Text(L"창을 닫으면 입력 내용과 이체 내역이 지워져요. 닫을까요?"), L"닫기");
            if (co_await dialog.ShowAsync() == ContentDialogResult::Primary)
            {
                allowClose = true;
                window.Close();
            }
        }
        catch (hresult_error const&) { failed = true; }
        if (failed) Notify(L"열린 대화 상자를 닫은 뒤 다시 시도해 주세요.", true);
        confirmingClose = false;
    }

    Windows::Foundation::IAsyncOperation<hstring> MainWindow::SavePicker(std::wstring name, std::wstring extension)
    {
        if (smokeDirectory) co_return hstring((*smokeDirectory / (name + extension)).wstring());
        Windows::Storage::Pickers::FileSavePicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(Handle()));
        picker.SuggestedFileName(name);
        auto kind = extension.substr(1);
        CharUpperBuffW(kind.data(), static_cast<DWORD>(kind.size()));
        picker.FileTypeChoices().Insert(kind + L" 파일", single_threaded_vector<hstring>({ hstring(extension) }));
        auto file = co_await picker.PickSaveFileAsync();
        co_return file ? file.Path() : hstring();
    }

    void MainWindow::Copy(std::wstring const& value, Action onCopy)
    {
        try
        {
            Windows::ApplicationModel::DataTransfer::DataPackage data;
            data.SetText(value);
            Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(data);
        }
        catch (hresult_error const& error)
        {
            Notify(error.message(), true);
            return;
        }
        if (onCopy) onCopy();
        Notify(CopyPreview(value));
    }

    /// 복사한 값을 그대로 보여줘요. 길면 앞 5줄 + "외 n줄"
    std::wstring MainWindow::CopyPreview(std::wstring const& value)
    {
        std::vector<std::wstring> lines;
        for (auto& line : Split(NormalizeLines(value), L'\n'))
        {
            while (!line.empty() && iswspace(line.back())) line.pop_back();
            if (!line.empty()) lines.push_back(line.size() > 80 ? line.substr(0, 80) + L"…" : line);
        }
        if (lines.empty()) return L"복사했어요.";
        auto total = lines.size();
        if (total > 5) lines.resize(5);
        auto shown = Join(lines, L"\r\n");
        return total > 5 ? shown + L"\r\n외 " + std::to_wstring(total - 5) + L"줄 (총 " + std::to_wstring(total) + L"줄 복사)" : shown;
    }

    // ───────────── 홈 · 사용법 · 변경 이력 · 설정 (카드 없이 구분선 목록) ─────────────
    static StackPanel PlainPage(double maxWidth)
    {
        StackPanel page;
        page.Spacing(0);
        page.Padding(Th(32, 24, 32, 32));
        page.MaxWidth(maxWidth);
        page.HorizontalAlignment(HorizontalAlignment::Left);
        return page;
    }

    static ScrollViewer PageScroll(UIElement const& page)
    {
        ScrollViewer view;
        view.Content(page);
        view.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        return view;
    }

    FrameworkElement MainWindow::HomePage()
    {
        auto tool = [this](wchar_t const* glyph, Swatch& color, wchar_t const* title, wchar_t const* description,
            std::vector<std::wstring> const& steps, wchar_t const* action, std::wstring route)
        {
            Grid grid;
            grid.ColumnSpacing(16);
            AddColumn(grid, Px(40));
            AddColumn(grid, Star());
            AddColumn(grid, AutoSize());
            auto tile = Tile(color, glyph, 40, 20, 9);
            tile.VerticalAlignment(VerticalAlignment::Top);
            grid.Children().Append(tile);
            auto heading = Text(title, 18);
            heading.FontWeight(SemiBold());
            std::vector<std::wstring> numbered;
            for (size_t i = 0; i < steps.size(); i++) numbered.push_back(std::to_wstring(i + 1) + L". " + steps[i]);
            auto body = Stack({ heading, Muted(description, 14), Muted(Join(numbered, L"\n"), 13) });
            body.Spacing(6);
            Place(grid, body, 0, 1);
            auto go = MakeButton(action, [this, route] { Go(route); }, true);
            go.VerticalAlignment(VerticalAlignment::Top);
            TintAccentButton(go, color);
            Place(grid, go, 0, 2);
            return Card(grid);
        };
        auto page = PlainPage(980);
        // "업무 도구": 녹색 → teal → 파랑 가로 그라데이션 글자
        auto heading = TitleText(L"업무 도구");
        heading.FontSize(28);
        heading.Foreground(headingGradient);
        page.Children().Append(heading);
        auto lead = Muted(L"파일과 입력 내용은 이 기기 안에서만 처리해요. 로그인 없이 바로 시작하세요.", 14);
        lead.Margin(Th(0, 4, 0, 16));
        page.Children().Append(lead);
        page.Children().Append(Divider());
        page.Children().Append(tool(L"", accent, L"탁송체크", L"한빛탁송 · 오픈마일 · 렌톡 엑셀을 대조하고 주유비용과 탁송 상태를 정리해요.",
            { L"차량목록을 붙여넣어요.", L"업체 Excel 파일을 끌어다 놓아요. 암호 파일도 돼요.", L"차량 조회하기를 누르고 결과를 복사·저장해요." }, L"탁송체크 열기", L"dispatch"));
        page.Children().Append(tool(L"", blue, L"하나은행 이체파일", L"Slack 요청을 붙여넣고, 계좌와 금액을 검토한 뒤 다계좌이체 Excel 파일을 만들어요.",
            { L"이체 요청 입력: 스레드를 붙여넣고 추출해요.", L"확인·수정: 오류(빨강)와 검토 필요(주황)를 정리해요.", L"다운로드: 미리보기를 확인하고 파일을 받아요." }, L"이체파일 만들기", L"transfer"));
        auto warnIcon = Icon(L"", 14);
        warnIcon.Foreground(orange.Solid);
        auto warnText = Text(L"은행에 올리기 전에는 금액과 계좌를 원문과 대조해 주세요. 파일 생성은 실제 송금을 실행하지 않아요.", 13);
        warnText.Foreground(orange.Solid);
        auto warn = Bar({ warnIcon, warnText });
        warn.Margin(Th(0, 16, 0, 0));
        page.Children().Append(warn);
        return PageScroll(page);
    }

    FrameworkElement MainWindow::HelpPage()
    {
        auto page = PlainPage(980);
        page.Children().Append(TitleText(L"사용법"));
        page.Children().Append(Divider());
        auto section = [this](wchar_t const* title, wchar_t const* body)
        {
            auto heading = Text(title, 18);
            heading.FontWeight(SemiBold());
            auto stack = Stack({ heading, Muted(body, 14) });
            stack.Spacing(8);
            return Card(stack);
        };
        page.Children().Append(section(L"탁송체크", L"1. 오른쪽 입력 패널에 차량번호 목록을 붙여넣어요. 날짜·시간도 함께 넣을 수 있어요.\n2. 한빛탁송·오픈마일·렌톡에서 내려받은 업체 엑셀(.xlsx)을 패널에 끌어다 놓아요. 암호 파일은 암호를 입력해요.\n3. 차량 조회하기(Ctrl+Enter)를 누르고, 왼쪽 탐색 창의 ‘확인 필요’부터 검토해요.\n4. 일정·비용이 겹치면 ‘선택’에서 원본 기록을 골라요.\n5. 슬랙봇 문의 차량은 ‘슬랙봇 회신’에 회신을 붙여넣어 반영해요.\n6. 필요한 줄을 체크하고 차량번호 복사·캡처·저장을 써요."));
        page.Children().Append(section(L"하나은행 이체파일", L"1. Slack 스레드를 붙여넣고 이체정보를 추출해요.\n2. 은행·계좌·금액·차량번호를 원문과 대조해요. 줄을 누르면 오른쪽에 원문과 확인 사항이 보여요.\n3. 오류를 모두 수정하고 검토 항목을 확인해요. 수정하면 확인 체크가 풀려요.\n4. 미리보기를 확인한 뒤 Excel 파일을 저장해요. 1억 원 초과 금액은 분할하며 파일당 100건까지예요.\n5. 은행 업로드 화면에서 받는 분과 금액을 다시 확인해 주세요."));
        auto links = Bar();
        links.Spacing(4);
        for (auto const& [title, file] : std::vector<std::pair<wchar_t const*, wchar_t const*>>{
            { L"따라 해 보기", L"quick-guide.html" }, { L"탁송체크 그림 안내", L"dispatch-guide.html" }, { L"이체파일 그림 안내", L"transfer-guide.html" } })
        {
            HyperlinkButton link;
            link.Content(box_value(title));
            link.NavigateUri(Uri(std::wstring(L"https://chowonki.github.io/WorkTool/") + file));
            links.Children().Append(link);
        }
        auto webHeading = Text(L"웹 그림 안내", 18);
        webHeading.FontWeight(SemiBold());
        auto web = Stack({ webHeading, links });
        web.Spacing(6);
        page.Children().Append(Card(web));
        auto note = Muted(L"입력값과 첨부 파일은 서버로 전송하지 않아요. 앱을 닫으면 작업 내용은 지워지고, 저장한 파일은 기기에 남아요.", 13);
        note.Margin(Th(0, 16, 0, 0));
        page.Children().Append(note);
        return PageScroll(page);
    }

    /// 변경 이력: 앱에 함께 넣은 CHANGELOG.md를 날짜별 목록으로 보여 줘요.
    FrameworkElement MainWindow::ChangeLogPage()
    {
        auto page = PlainPage(1000);
        page.Children().Append(TitleText(L"변경 이력"));
        auto lead = Muted(L"탁송체크와 하나은행 이체파일의 주요 변경사항이에요. 날짜는 main 반영 기준(한국 시간)이에요.", 14);
        lead.Margin(Th(0, 4, 0, 8));
        page.Children().Append(lead);
        HyperlinkButton link;
        link.Content(box_value(L"GitHub에서 보기"));
        link.NavigateUri(Uri(L"https://chowonki.github.io/WorkTool/changelog.html"));
        link.Padding(Th(0, 4, 0, 12));
        page.Children().Append(link);
        page.Children().Append(Divider());
        auto content = ReadFile(AppDirectory() / L"CHANGELOG.md");
        StackPanel section{ nullptr };
        for (auto raw : Split(NormalizeLines(ToWide(content.value_or(""))), L'\n'))
        {
            while (!raw.empty() && iswspace(raw.back())) raw.pop_back();
            if (raw.starts_with(L"## "))
            {
                if (section) page.Children().Append(Card(section));
                section = StackPanel();
                section.Spacing(6);
                auto heading = Text(raw.substr(3), 16);
                heading.FontWeight(SemiBold());
                section.Children().Append(heading);
            }
            else if (section && raw.starts_with(L"- "))
            {
                TextBlock item;
                item.TextWrapping(TextWrapping::Wrap);
                item.FontSize(13);
                item.Foreground(mutedInk);
                item.Inlines().Append(MakeRun(L"• "));
                // **굵게**만 살려요
                auto body = Replace(raw.substr(2), L"`", L"");
                size_t start = 0;
                for (auto bold = false;; bold = !bold)
                {
                    auto at = body.find(L"**", start);
                    auto run = MakeRun(body.substr(start, at == std::wstring::npos ? at : at - start));
                    if (bold) run.FontWeight(SemiBold());
                    item.Inlines().Append(run);
                    if (at == std::wstring::npos) break;
                    start = at + 2;
                }
                section.Children().Append(item);
            }
        }
        if (section) page.Children().Append(Card(section));
        if (!content) page.Children().Append(Muted(L"변경 이력 파일을 찾지 못했어요. 위 링크에서 볼 수 있어요.", 13));
        return PageScroll(page);
    }

    static wchar_t const* const ThemeNames[]{ L"Default", L"Light", L"Dark" };

    void MainWindow::LoadWorkToolTheme()
    {
        auto saved = ReadFile(LocalState::DataFile(L"theme.txt"));
        if (!saved) return;
        auto name = Trim(ToWide(*saved));
        for (int i = 0; i < 3; i++)
            if (name == ThemeNames[i]) root.RequestedTheme(static_cast<ElementTheme>(i));
    }

    FrameworkElement MainWindow::SettingsPage()
    {
        ComboBox theme;
        theme.Header(box_value(L"화면 모드"));
        SetItems(theme, { L"시스템 설정", L"라이트", L"다크" });
        theme.SelectedIndex(static_cast<int>(root.RequestedTheme()));
        theme.Width(220);
        theme.SelectionChanged([this](IInspectable const& sender, auto&&)
        {
            auto index = sender.as<ComboBox>().SelectedIndex();
            if (index < 0 || index > 2) return;
            root.RequestedTheme(static_cast<ElementTheme>(index));
            if (!WriteFile(LocalState::DataFile(L"theme.txt"), ToUtf8(ThemeNames[index]))) Notify(L"화면 모드 설정을 저장하지 못했어요.", true);
        });
        auto page = PlainPage(980);
        page.Children().Append(TitleText(L"설정"));
        page.Children().Append(Divider());
        page.Children().Append(Card(theme));
        page.Children().Append(Card(Muted(L"WorkTool · Windows 네이티브 (C++)\nC++/WinRT · WinUI 3 / 로컬 처리 · Mica · Pretendard 글꼴 포함", 13)));
        return page;
    }
}
