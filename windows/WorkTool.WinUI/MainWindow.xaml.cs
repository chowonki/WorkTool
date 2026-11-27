using System.Text.RegularExpressions;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Media;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;
using Windows.Storage.Pickers;
using WinRT.Interop;
using WorkTool.Core;

namespace WorkTool.WinUI;

public sealed partial class MainWindow : Window
{
    private readonly Rules rules = new();
    private readonly Dictionary<string, FrameworkElement> pages = new();
    private readonly Dictionary<string, NavigationViewItem> navItems = new();
    private readonly Dictionary<string, TextBlock> navCounts = new();
    private readonly Dictionary<int, FontIcon> stepMarks = new();
    private string currentNav = "";
    private bool syncingNav;
    private bool allowClose;
    private bool confirmingClose;
    private string? smokeDirectory;

    private static readonly (string Key, string Label)[] DispatchViews =
    [
        ("all", "전체"), ("review", "확인 필요"), ("confirmed", "금액 확인"), ("progress", "진행 중"), ("missing", "미조회"),
        ("jeju", "제주 출발"), ("carrier", "캐리어탁송"), ("slack", "슬랙봇 문의"), ("slack-replies", "슬랙 회신 반영")
    ];
    private static readonly string[] TransferStepNames = ["이체 요청 입력", "확인·수정", "다운로드"];

    public MainWindow()
    {
        InitializeComponent();
        Title = "WorkTool";
        LoadWorkToolTheme();
        UpdateWorkColors();
        ObserveSystemColors();
        SystemBackdrop = new MicaBackdrop();
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(Header); // System caption buttons retain Snap Layouts, accessibility and DPI handling.
        AppWindow.Resize(new Windows.Graphics.SizeInt32(1440, 960));
        // 작업 표시줄·Alt+Tab 아이콘. 실행 파일에 들어간 아이콘과 같은 파일이에요.
        var iconPath = Path.Combine(AppContext.BaseDirectory, "Assets", "WorkTool.ico");
        if (File.Exists(iconPath)) AppWindow.SetIcon(iconPath);
        AppWindow.TitleBar.PreferredHeightOption = TitleBarHeightOption.Tall;
        AppWindow.TitleBar.ButtonBackgroundColor = Colors.Transparent;
        AppWindow.TitleBar.ButtonInactiveBackgroundColor = Colors.Transparent;
        Root.ActualThemeChanged += (_, _) => { UpdateCaptionColors(); UpdateWorkColors(); };
        UpdateCaptionColors();
        AppWindow.Closing += Closing;
        BuildNavigation();
        // 탐색 창의 대수·단계 표시를 위해 두 업무 화면은 처음부터 만들어 둬요.
        pages["dispatch"] = DispatchPage();
        pages["transfer"] = TransferPage();
        Select("home");
        var arguments = Environment.GetCommandLineArgs();
        if (arguments.Length == 4 && arguments[1] == "--smoke-test")
            Root.Loaded += async (_, _) => await SmokeTest(arguments[2], arguments[3]);
    }

    private void UpdateCaptionColors()
    {
        AppWindow.TitleBar.ButtonForegroundColor = Root.ActualTheme == ElementTheme.Dark ? Colors.White : Colors.Black;
        AppWindow.TitleBar.ButtonInactiveForegroundColor = Root.ActualTheme == ElementTheme.Dark ? Colors.Gray : Colors.DimGray;
    }

    // ───────────── 탐색 창 ─────────────
    private void BuildNavigation()
    {
        var items = Navigation.MenuItems;
        items.Add(NavItem("home", "홈", new SymbolIcon(Symbol.Home)));
        items.Add(new NavigationViewItemHeader { Content = "업무 도구" });
        var dispatch = NavItem("dispatch", "탁송체크", new FontIcon { Glyph = "\uE804" });
        dispatch.IsExpanded = true;
        foreach (var (key, label) in DispatchViews) dispatch.MenuItems.Add(NavItem("d:" + key, label, null, count: true));
        navItems["d:slack-replies"].Visibility = Visibility.Collapsed;
        items.Add(dispatch);
        var transfer = NavItem("transfer", "하나은행 이체파일", new FontIcon { Glyph = "\uE8C7" });
        transfer.IsExpanded = true;
        for (var i = 1; i <= 3; i++)
        {
            var mark = new FontIcon { Glyph = i.ToString(), FontFamily = new FontFamily("Segoe UI Variable Text"), FontSize = 13 };
            stepMarks[i] = mark;
            transfer.MenuItems.Add(NavItem("t:" + i, TransferStepNames[i - 1], mark, count: i == 2));
        }
        items.Add(transfer);
        items.Add(new NavigationViewItemHeader { Content = "도움말" });
        items.Add(NavItem("help", "사용법", new SymbolIcon(Symbol.Help)));
        items.Add(NavItem("changelog", "변경 이력", new FontIcon { Glyph = "\uE81C" }));
    }

    private NavigationViewItem NavItem(string key, string label, IconElement? icon, bool count = false)
    {
        var item = new NavigationViewItem { Tag = key, Icon = icon, HorizontalContentAlignment = HorizontalAlignment.Stretch };
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(item, label);
        if (count)
        {
            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var number = new TextBlock { FontSize = 12, Foreground = mutedInk, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(8, 0, 4, 0) };
            Grid.SetColumn(number, 1);
            grid.Children.Add(new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis });
            grid.Children.Add(number);
            navCounts[key] = number;
            item.Content = grid;
        }
        else item.Content = label;
        navItems[key] = item;
        return item;
    }

    private void Navigate(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (syncingNav || PageHost is null) return;
        var key = args.IsSettingsSelected ? "settings" : (args.SelectedItem as NavigationViewItem)?.Tag?.ToString() ?? "home";
        Activate(key);
    }

    /// <summary>탐색 항목을 골라요. activate=false면 표시만 맞춰요 (화면 안에서 이미 바뀐 경우).</summary>
    private void Select(string key, bool activate = true)
    {
        if (!navItems.TryGetValue(key, out var item)) return;
        if (!activate)
        {
            currentNav = key;
            syncingNav = true;
            try { Navigation.SelectedItem = item; } finally { syncingNav = false; }
            return;
        }
        if (Navigation.SelectedItem == item) Activate(key);
        else Navigation.SelectedItem = item;
    }

    private void Activate(string key)
    {
        // 부모 항목을 누르면 지금 보던 하위 항목으로 가요
        if (key == "dispatch") { Select("d:" + dispatchFilter); return; }
        if (key == "transfer") { Select("t:" + step); return; }
        currentNav = key;
        var page = key.StartsWith("d:") ? "dispatch" : key.StartsWith("t:") ? "transfer" : key;
        ShowPage(page);
        if (key.StartsWith("d:")) SetDispatchFilter(key[2..]);
        else if (key.StartsWith("t:") && step != key[2] - '0') Step(key[2] - '0');
    }

    private void ShowPage(string key)
    {
        if (!pages.TryGetValue(key, out var page))
        {
            page = key switch { "help" => HelpPage(), "settings" => SettingsPage(), "changelog" => ChangeLogPage(), _ => HomePage() };
            pages.Add(key, page);
        }
        if (PageHost.Children.Count == 1 && PageHost.Children[0] == page) return;
        PageHost.Children.Clear(); PageHost.Children.Add(page);
    }

    private void Go(string key) => Select(key switch
    {
        "dispatch" => "d:" + dispatchFilter,
        "transfer" => "t:" + step,
        _ => key
    });

    /// <summary>탁송체크 하위 항목 오른쪽 대수, '슬랙 회신 반영' 항목 보이기</summary>
    private void UpdateDispatchNav()
    {
        if (navCounts.Count == 0) return;
        foreach (var (key, _) in DispatchViews)
            navCounts["d:" + key].Text = results.Count == 0 ? "" : DispatchCount(key).ToString("N0");
        var replies = results.Any(row => row?.Flag("slackReviewed") == true);
        navItems["d:slack-replies"].Visibility = replies ? Visibility.Visible : Visibility.Collapsed;
        if (currentNav.StartsWith("d:") && currentNav != "d:" + dispatchFilter) Select("d:" + dispatchFilter, activate: false);
    }

    /// <summary>이체파일 단계 표시: 끝난 단계는 ✓, 2단계 오른쪽에 건수</summary>
    private void UpdateTransferNav()
    {
        if (stepMarks.Count == 0) return;
        var counts = transfers.Count == 0 ? (Bad: 0, Warn: 0) : TransferCounts();
        bool Done(int i) => i switch
        {
            1 => transfers.Count > 0,
            2 => transfers.Count > 0 && step == 3 && counts.Bad == 0,
            3 => transfers.Count > 0 && savedSignature == Signature,
            _ => false
        };
        for (var i = 1; i <= 3; i++)
        {
            var done = Done(i);
            stepMarks[i].Glyph = done ? "\uE73E" : i.ToString();
            stepMarks[i].FontFamily = done ? new FontFamily("Segoe Fluent Icons, Segoe MDL2 Assets") : new FontFamily("Segoe UI Variable Text");
            stepMarks[i].Foreground = done ? accentInk : mutedInk;
        }
        navCounts["t:2"].Text = transfers.Count > 0 ? transfers.Count.ToString("N0") : "";
        if (currentNav.StartsWith("t:") && currentNav != "t:" + step) Select("t:" + step, activate: false);
    }

    // ───────────── 공용 도구 ─────────────
    private static TextBlock Text(string value, double size = 14) => new() { Text = value, FontSize = size, TextWrapping = TextWrapping.Wrap };
    private TextBlock Muted(string value, double size = 12)
    {
        var text = Text(value, size);
        text.Foreground = mutedInk;
        return text;
    }
    private static StackPanel Stack(params UIElement[] children)
    {
        var stack = new StackPanel { Spacing = 14 };
        foreach (var child in children) stack.Children.Add(child);
        return stack;
    }
    private static StackPanel Bar(params UIElement[] children)
    {
        var stack = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        foreach (var child in children) stack.Children.Add(child);
        return stack;
    }
    /// <summary>구역: 카드 상자 없이 아래 1px 구분선으로만 나눠요.</summary>
    private Border Card(UIElement child) => new() { Child = child, Padding = new Thickness(0, 16, 0, 20), BorderThickness = new Thickness(0, 0, 0, 1), BorderBrush = divider };
    private static Button Button(string title, Action action, bool primary = false)
    {
        var b = new Button { Content = title };
        if (primary) b.Style = (Style)Application.Current.Resources["AccentButtonStyle"];
        b.Click += (_, _) => action(); return b;
    }
    private Button AsyncButton(string title, Func<Task> action, bool primary = false)
    {
        var b = Button(title, () => { }, primary);
        b.Click += async (_, _) =>
        {
            b.IsEnabled = false;
            try { await action(); }
            catch (Exception e) { Notify(e.Message, true); }
            finally { if (b == download) ValidateTransfers(); else b.IsEnabled = true; }
        };
        return b;
    }
    /// <summary>CommandBar 단추 (아이콘 + 글자, 배경 없음)</summary>
    private AppBarButton Command(string label, string glyph, Func<Task> action)
    {
        var b = new AppBarButton { Label = label, Icon = new FontIcon { Glyph = glyph } };
        b.Click += async (_, _) =>
        {
            try { await action(); }
            catch (Exception e) { Notify(e.Message, true); }
        };
        return b;
    }
    private AppBarButton Command(string label, string glyph, Action action) => Command(label, glyph, () => { action(); return Task.CompletedTask; });
    private static CommandBar CommandBar(params ICommandBarElement[] commands)
    {
        var bar = new CommandBar
        {
            DefaultLabelPosition = CommandBarDefaultLabelPosition.Right, Background = new SolidColorBrush(Colors.Transparent),
            HorizontalAlignment = HorizontalAlignment.Left, IsDynamicOverflowEnabled = true, OverflowButtonVisibility = CommandBarOverflowButtonVisibility.Auto
        };
        foreach (var command in commands)
        {
            if (command is AppBarButton or AppBarToggleButton or AppBarSeparator) bar.PrimaryCommands.Add(command);
        }
        return bar;
    }
    private static ScrollViewer Scroll(UIElement content) => new() { Content = content, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    private void Notify(string message, bool error = false)
    {
        Notice.Message = message; Notice.Severity = error ? InfoBarSeverity.Error : InfoBarSeverity.Success; Notice.IsOpen = true;
    }
    private static TextBox Input(string header, string placeholder = "", double width = double.NaN) => new() { Header = header, PlaceholderText = placeholder, Width = width, HorizontalAlignment = HorizontalAlignment.Stretch };
    private static TextBlock TitleText(string title) => new() { Text = title, FontSize = 24, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold };
    /// <summary>H1 + 회색 부제</summary>
    private StackPanel PageTitle(string title, TextBlock subtitle)
    {
        subtitle.Foreground = mutedInk;
        subtitle.FontSize = 14;
        var stack = new StackPanel { Spacing = 2, Margin = new Thickness(24, 20, 24, 8) };
        stack.Children.Add(TitleText(title));
        stack.Children.Add(subtitle);
        return stack;
    }
    /// <summary>표 아래 상태 줄 (12px 회색, 위에 구분선)</summary>
    private Border StatusLine(UIElement left, UIElement right)
    {
        var grid = new Grid { ColumnSpacing = 16 };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        Grid.SetColumn((FrameworkElement)right, 1);
        grid.Children.Add(left); grid.Children.Add(right);
        return new Border { Child = grid, BorderBrush = divider, BorderThickness = new Thickness(0, 1, 0, 0), Padding = new Thickness(24, 6, 16, 6) };
    }
    /// <summary>오른쪽 패널 (300~320px, 왼쪽 구분선)</summary>
    private Border SidePanel(UIElement content) => new()
    {
        Child = content, Width = 320, BorderBrush = divider, BorderThickness = new Thickness(1, 0, 0, 0)
    };
    /// <summary>패널 안 소제목</summary>
    private static TextBlock PanelHeading(string text) => new() { Text = text, FontSize = 14, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold };

    // ───────────── 홈 · 사용법 · 변경 이력 · 설정 (카드 없이 구분선 목록) ─────────────
    private FrameworkElement HomePage()
    {
        FrameworkElement Tool(string glyph, string title, string description, string[] steps, string action, string route)
        {
            var grid = new Grid { ColumnSpacing = 16 };
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(28) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            grid.Children.Add(new FontIcon { Glyph = glyph, FontSize = 22, Foreground = accentInk, VerticalAlignment = VerticalAlignment.Top, Margin = new Thickness(0, 2, 0, 0) });
            var heading = Text(title, 18); heading.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
            var body = Stack(heading, Muted(description, 14), Muted(string.Join("\n", steps.Select((s, i) => $"{i + 1}. {s}")), 13));
            body.Spacing = 6;
            Grid.SetColumn(body, 1); grid.Children.Add(body);
            var go = Button(action, () => Go(route), true); go.VerticalAlignment = VerticalAlignment.Top;
            Grid.SetColumn(go, 2); grid.Children.Add(go);
            return Card(grid);
        }
        var page = new StackPanel { Spacing = 0, Padding = new Thickness(32, 24, 32, 32), MaxWidth = 980, HorizontalAlignment = HorizontalAlignment.Left };
        page.Children.Add(TitleText("홈"));
        var lead = Muted("파일과 입력 내용은 이 기기 안에서만 처리해요. 로그인 없이 바로 시작하세요.", 14); lead.Margin = new Thickness(0, 4, 0, 16);
        page.Children.Add(lead);
        page.Children.Add(Divider());
        page.Children.Add(Tool("\uE804", "탁송체크", "한빛탁송 · 오픈마일 · 렌톡 엑셀을 대조하고 주유비용과 탁송 상태를 정리해요.",
            ["차량목록을 붙여넣어요.", "업체 Excel 파일을 끌어다 놓아요. 암호 파일도 돼요.", "차량 조회하기를 누르고 결과를 복사·저장해요."], "탁송체크 열기", "dispatch"));
        page.Children.Add(Tool("\uE8C7", "하나은행 이체파일", "Slack 요청을 붙여넣고, 계좌와 금액을 검토한 뒤 다계좌이체 Excel 파일을 만들어요.",
            ["이체 요청 입력: 스레드를 붙여넣고 추출해요.", "확인·수정: 오류(빨강)와 검토 필요(주황)를 정리해요.", "다운로드: 미리보기를 확인하고 파일을 받아요."], "이체파일 만들기", "transfer"));
        var warn = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Margin = new Thickness(0, 16, 0, 0) };
        warn.Children.Add(new FontIcon { Glyph = "\uE7BA", FontSize = 14, Foreground = warningInk });
        var warnText = Text("은행에 올리기 전에는 금액과 계좌를 원문과 대조해 주세요. 파일 생성은 실제 송금을 실행하지 않아요.", 13); warnText.Foreground = warningInk;
        warn.Children.Add(warnText);
        page.Children.Add(warn);
        return new ScrollViewer { Content = page, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    }

    private ContentDialog Dialog(string title, UIElement content, string primary) => new() { XamlRoot = Root.XamlRoot, Title = title, Content = content, PrimaryButtonText = primary, CloseButtonText = "취소", DefaultButton = ContentDialogButton.Close, RequestedTheme = Root.ActualTheme };
    private async void Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (allowClose) return;
        if (saving || confirmingClose) { args.Cancel = true; return; }
        if (!HasUnsavedTransfers()) return;
        args.Cancel = true;
        confirmingClose = true;
        try
        {
            if (await Dialog("저장하지 않은 내용이 있어요", Text("창을 닫으면 입력 내용과 이체 내역이 지워져요. 닫을까요?"), "닫기").ShowAsync() == ContentDialogResult.Primary)
            {
                allowClose = true;
                Close();
            }
        }
        catch (InvalidOperationException) { Notify("열린 대화 상자를 닫은 뒤 다시 시도해 주세요.", true); }
        catch (System.Runtime.InteropServices.COMException) { Notify("열린 대화 상자를 닫은 뒤 다시 시도해 주세요.", true); }
        finally { confirmingClose = false; }
    }
    private async Task<StorageFile?> SavePicker(string name, string extension)
    {
        if (smokeDirectory is not null)
            return await (await StorageFolder.GetFolderFromPathAsync(smokeDirectory)).CreateFileAsync(name + extension, CreationCollisionOption.ReplaceExisting);
        var picker = new FileSavePicker { SuggestedFileName = name };
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        picker.FileTypeChoices.Add(extension[1..].ToUpperInvariant() + " 파일", new[] { extension });
        return await picker.PickSaveFileAsync();
    }
    private void Copy(string value, Action? onCopy = null)
    {
        try
        {
            var data = new DataPackage();
            data.SetText(value);
            Clipboard.SetContent(data);
        }
        catch (Exception e)
        {
            Notify(e.Message, true);
            return;
        }
        onCopy?.Invoke();
        Notify("클립보드에 복사했어요.");
    }

    private FrameworkElement HelpPage()
    {
        var page = new StackPanel { Spacing = 0, Padding = new Thickness(32, 24, 32, 32), MaxWidth = 980, HorizontalAlignment = HorizontalAlignment.Left };
        page.Children.Add(TitleText("사용법"));
        page.Children.Add(Divider());
        Border Section(string title, string body)
        {
            var heading = Text(title, 18); heading.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
            var stack = Stack(heading, Muted(body, 14)); stack.Spacing = 8;
            return Card(stack);
        }
        page.Children.Add(Section("탁송체크", "1. 오른쪽 입력 패널에 차량번호 목록을 붙여넣어요. 날짜·시간도 함께 넣을 수 있어요.\n2. 한빛탁송·오픈마일·렌톡에서 내려받은 업체 엑셀을 패널에 끌어다 놓아요. 암호 파일은 암호를 입력해요.\n3. 차량 조회하기(Ctrl+Enter)를 누르고, 왼쪽 탐색 창의 ‘확인 필요’부터 검토해요.\n4. 일정·비용이 겹치면 ‘선택’에서 원본 기록을 골라요.\n5. 슬랙봇 문의 차량은 ‘슬랙봇 회신’에 회신을 붙여넣어 반영해요.\n6. 필요한 줄을 체크하고 차량번호 복사·캡처·저장을 써요."));
        page.Children.Add(Section("하나은행 이체파일", "1. Slack 스레드를 붙여넣고 이체정보를 추출해요.\n2. 은행·계좌·금액·차량번호를 원문과 대조해요. 줄을 누르면 오른쪽에 원문과 확인 사항이 보여요.\n3. 오류를 모두 수정하고 검토 항목을 확인해요. 수정하면 확인 체크가 풀려요.\n4. 미리보기를 확인한 뒤 Excel 파일을 저장해요. 1억 원 초과 금액은 분할하며 파일당 100건까지예요.\n5. 은행 업로드 화면에서 받는 분과 금액을 다시 확인해 주세요."));
        var links = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        foreach (var (title, file) in new[] { ("따라 해 보기", "quick-guide.html"), ("탁송체크 그림 안내", "dispatch-guide.html"), ("이체파일 그림 안내", "transfer-guide.html") })
            links.Children.Add(new HyperlinkButton { Content = title, NavigateUri = new Uri("https://chowonki.github.io/WorkTool/" + file) });
        var webHeading = Text("웹 그림 안내", 18); webHeading.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
        var web = Stack(webHeading, links); web.Spacing = 6;
        page.Children.Add(Card(web));
        var note = Muted("입력값과 첨부 파일은 서버로 전송하지 않아요. 앱을 닫으면 작업 내용은 지워지고, 저장한 파일은 기기에 남아요.", 13);
        note.Margin = new Thickness(0, 16, 0, 0);
        page.Children.Add(note);
        return new ScrollViewer { Content = page, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    }

    /// <summary>변경 이력: 앱에 함께 넣은 CHANGELOG.md를 날짜별 목록으로 보여 줘요.</summary>
    private FrameworkElement ChangeLogPage()
    {
        var page = new StackPanel { Spacing = 0, Padding = new Thickness(32, 24, 32, 32), MaxWidth = 1000, HorizontalAlignment = HorizontalAlignment.Left };
        page.Children.Add(TitleText("변경 이력"));
        var lead = Muted("탁송체크와 하나은행 이체파일의 주요 변경사항이에요. 날짜는 main 반영 기준(한국 시간)이에요.", 14);
        lead.Margin = new Thickness(0, 4, 0, 8);
        page.Children.Add(lead);
        page.Children.Add(new HyperlinkButton { Content = "GitHub에서 보기", NavigateUri = new Uri("https://chowonki.github.io/WorkTool/changelog.html"), Padding = new Thickness(0, 4, 0, 12) });
        page.Children.Add(Divider());
        string[] lines;
        try { lines = File.ReadAllLines(Path.Combine(AppContext.BaseDirectory, "CHANGELOG.md")); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { lines = []; }
        StackPanel? section = null;
        foreach (var raw in lines)
        {
            var line = raw.TrimEnd();
            if (line.StartsWith("## "))
            {
                if (section is not null) page.Children.Add(Card(section));
                section = new StackPanel { Spacing = 6 };
                var heading = Text(line[3..], 16); heading.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
                section.Children.Add(heading);
            }
            else if (section is not null && line.StartsWith("- "))
            {
                var item = new TextBlock { TextWrapping = TextWrapping.Wrap, FontSize = 13, Foreground = mutedInk };
                item.Inlines.Add(new Run { Text = "• " });
                // **굵게**만 살려요
                var parts = Regex.Split(line[2..].Replace("`", ""), @"\*\*");
                for (var i = 0; i < parts.Length; i++)
                    item.Inlines.Add(i % 2 == 1 ? new Run { Text = parts[i], FontWeight = Microsoft.UI.Text.FontWeights.SemiBold } : new Run { Text = parts[i] });
                section.Children.Add(item);
            }
        }
        if (section is not null) page.Children.Add(Card(section));
        if (lines.Length == 0) page.Children.Add(Muted("변경 이력 파일을 찾지 못했어요. 위 링크에서 볼 수 있어요.", 13));
        return new ScrollViewer { Content = page, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    }

    private static string ThemeFile => LocalState.DataFile("theme.txt");

    private void LoadWorkToolTheme()
    {
        try
        {
            if (File.Exists(ThemeFile) && Enum.TryParse<ElementTheme>(File.ReadAllText(ThemeFile), out var theme)
                && Enum.IsDefined(theme)) Root.RequestedTheme = theme;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
    }
    private FrameworkElement SettingsPage()
    {
        var theme = new ComboBox { Header = "화면 모드", ItemsSource = new[] { "시스템 설정", "라이트", "다크" }, SelectedIndex = (int)Root.RequestedTheme, Width = 220 };
        theme.SelectionChanged += (_, _) =>
        {
            if (theme.SelectedIndex < 0) return;
            Root.RequestedTheme = (ElementTheme)theme.SelectedIndex;
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(ThemeFile)!);
                File.WriteAllText(ThemeFile, Root.RequestedTheme.ToString());
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { Notify("화면 모드 설정을 저장하지 못했어요.", true); }
        };
        var page = new StackPanel { Spacing = 0, Padding = new Thickness(32, 24, 32, 32), MaxWidth = 980, HorizontalAlignment = HorizontalAlignment.Left };
        page.Children.Add(TitleText("설정"));
        page.Children.Add(Divider());
        page.Children.Add(Card(theme));
        page.Children.Add(Card(Muted("WorkTool · Windows 네이티브\nWinUI 3 / 로컬 처리 · Mica · Segoe UI Variable", 13)));
        return page;
    }
}
