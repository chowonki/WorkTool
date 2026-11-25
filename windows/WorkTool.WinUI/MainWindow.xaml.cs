using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
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
    private bool allowClose;
    private bool confirmingClose;
    private string? smokeDirectory;

    public MainWindow()
    {
        InitializeComponent();
        Title = "WorkTool";
        LoadWorkToolTheme();
        Header.Background = dispatchTone.Surface;
        ApplyWorkSurfaces();
        UpdateWorkColors();
        ObserveSystemColors();
        SystemBackdrop = new MicaBackdrop();
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(Header); // System caption buttons retain Snap Layouts, accessibility and DPI handling.
        AppWindow.Resize(new Windows.Graphics.SizeInt32(1440, 960));
        AppWindow.TitleBar.PreferredHeightOption = TitleBarHeightOption.Standard;
        AppWindow.TitleBar.ButtonBackgroundColor = Colors.Transparent;
        AppWindow.TitleBar.ButtonInactiveBackgroundColor = Colors.Transparent;
        Root.ActualThemeChanged += (_, _) => { UpdateCaptionColors(); UpdateWorkColors(); };
        UpdateCaptionColors();
        AppWindow.Closing += Closing;
        Navigation.SelectedItem = Navigation.MenuItems[0];
        var arguments = Environment.GetCommandLineArgs();
        if (arguments.Length == 4 && arguments[1] == "--smoke-test")
            Root.Loaded += async (_, _) => await SmokeTest(arguments[2], arguments[3]);
    }

    private void UpdateCaptionColors()
    {
        AppWindow.TitleBar.ButtonForegroundColor = Root.ActualTheme == ElementTheme.Dark ? Colors.White : Colors.Black;
        AppWindow.TitleBar.ButtonInactiveForegroundColor = Root.ActualTheme == ElementTheme.Dark ? Colors.Gray : Colors.DimGray;
    }
    private void Navigate(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (PageHost is null) return;
        var key = args.IsSettingsSelected ? "settings" : (args.SelectedItem as NavigationViewItem)?.Tag?.ToString() ?? "home";
        if (!pages.TryGetValue(key, out var page))
        {
            page = key switch { "dispatch" => DispatchPage(), "transfer" => TransferPage(), "help" => HelpPage(), "settings" => SettingsPage(), _ => HomePage() };
            pages.Add(key, page);
        }
        PageHost.Children.Clear(); PageHost.Children.Add(page);
    }
    private void Go(string key) => Navigation.SelectedItem = Navigation.MenuItems.OfType<NavigationViewItem>().First(x => x.Tag?.ToString() == key);
    private static TextBlock Text(string value, double size = 14) => new() { Text = value, FontSize = size, TextWrapping = TextWrapping.Wrap };
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
    private Border Card(UIElement child) => new() { Child = child, Padding = new Thickness(20), CornerRadius = new CornerRadius(8), BorderThickness = new Thickness(1), BorderBrush = cardStroke, Background = cardFill };
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
    private static ScrollViewer Scroll(UIElement content) => new() { Content = content, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    private void Notify(string message, bool error = false)
    {
        Notice.Message = message; Notice.Severity = error ? InfoBarSeverity.Error : InfoBarSeverity.Success; Notice.IsOpen = true;
    }
    private TextBox Input(string header, string placeholder = "", double width = double.NaN) => new() { Header = header, PlaceholderText = placeholder, Width = width, HorizontalAlignment = HorizontalAlignment.Stretch, Background = fieldFill };
    private static TextBlock TitleText(string title) => new() { Text = title, FontSize = 28, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold };
    private FrameworkElement HomePage()
    {
        return VerticalScroll(Stack(TitleText("매일의 업무를, 한곳에서"), Text("파일과 입력 내용은 이 기기 안에서만 처리해요. 로그인 없이 바로 시작하세요.", 16),
            SplitLayout(
                ToolCard(dispatchTone, "\uE804", "탁송체크", "한빛탁송 · 오픈마일 · 렌톡 엑셀을 대조하고 주유비용과 탁송 상태를 정리해요.", "탁송체크 열기", "dispatch"),
                ToolCard(transferTone, "\uE8AB", "하나은행 이체파일", "Slack 요청을 붙여넣고, 계좌와 금액을 검토한 뒤 다계좌이체 Excel 파일을 만들어요.", "이체파일 만들기", "transfer"), 720),
            Card(Stack(Text("시작하기 전에", 18), Text("은행에 올리기 전에는 금액과 계좌를 원문과 대조해 주세요. 파일 생성은 실제 송금을 실행하지 않아요."), Button("사용 방법", () => Go("help"))))));
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
    private FrameworkElement HelpPage() => Scroll(Stack(TitleText("사용 방법"),
        Card(Stack(Text("탁송체크", 21), Text("1. 차량번호 목록을 붙여넣어요. 날짜·시간도 함께 넣을 수 있어요.\n2. 한빛탁송·오픈마일·렌톡에서 내려받은 업체 엑셀을 첨부해요. 암호 파일은 암호를 입력해요.\n3. 차량 조회하기를 누르고 ‘확인 필요’ 항목부터 검토해요.\n4. 일정·비용이 겹치면 원본 위치를 보고 사용할 값을 골라요.\n5. 필요한 줄을 선택하고 TXT·Excel·PNG로 저장하거나 캡처를 복사해요."))),
        Card(Stack(Text("하나은행 이체파일", 21), Text("1. Slack 스레드를 붙여넣고 이체정보를 추출해요.\n2. 은행·계좌·금액·차량번호를 원문과 대조해요. 메모와 CMS코드도 입력할 수 있어요.\n3. 오류를 모두 수정하고 검토 항목을 확인해요. 수정하면 확인 체크가 풀려요.\n4. 미리보기를 확인한 뒤 Excel 파일을 저장해요. 1억 원 초과 금액은 분할하며 파일당 100건까지예요.\n5. 은행 업로드 화면에서 받는 분과 금액을 다시 확인해 주세요."))),
        Card(Stack(Text("기존 웹 안내", 20),
            Horizontal(Bar(GuideLink("따라 해 보기", "quick-guide.html"), GuideLink("탁송체크 그림 안내", "dispatch-guide.html"),
                GuideLink("이체파일 그림 안내", "transfer-guide.html"), GuideLink("변경 이력", "changelog.html"))))),
        Text("입력값과 첨부 파일은 서버로 전송하지 않아요. 앱을 닫으면 작업 내용은 지워지고, 저장한 파일은 기기에 남아요.")));

    private static HyperlinkButton GuideLink(string title, string page) => new()
    {
        Content = title,
        NavigateUri = new Uri("https://chowonki.github.io/WorkTool/" + page)
    };

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
        return Stack(TitleText("설정"), Card(Stack(theme, Text("Pretendard 글꼴을 앱에 포함해 별도 설치 없이 사용해요."), Text("WorkTool · Windows 네이티브\nWinUI 3 / 로컬 처리"))));
    }
}
