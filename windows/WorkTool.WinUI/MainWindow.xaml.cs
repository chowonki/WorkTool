using System.Globalization;
using System.Text.Json.Nodes;
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
    private readonly JsonArray files = new();
    private JsonArray results = new(), transfers = new();
    private readonly HashSet<string> imported = new();
    private readonly HashSet<string> selected = new();
    private readonly HashSet<string> completed = new();
    private TextBox vehicles = null!, thread = null!, search = null!;
    private TextBlock fileSummary = null!, dispatchSummary = null!, transferSummary = null!;
    private StackPanel dispatchList = null!, transferList = null!;
    private ComboBox filter = null!, scope = null!;
    private CheckBox acknowledge = null!;
    private Button download = null!;
    private Grid transferInput = null!, transferReview = null!, transferDownload = null!;
    private bool refreshing, allowClose, saving;
    private string savedSignature = "", savedThread = "", acknowledgedSignature = "";
    private string? smokeDirectory;
    private int step = 1;
    private int dispatchPage;
    private static readonly double[] DispatchColumns = [120, 0, 102, 106, 96, 145, 90, 48, 64];
    private static readonly double[] TransferColumns = [120, 178, 120, 0, 0, 95];
    private Button previousResults = null!, nextResults = null!;
    private ScrollViewer dispatchScroll = null!;
    private StackPanel previewRows = null!;
    private Button[] transferSteps = [];
    private readonly List<TextBlock> transferStatuses = [];
    private int visibleCount;
    private decimal visibleAmount;

    public MainWindow()
    {
        InitializeComponent();
        Title = "WorkTool";
        SystemBackdrop = new MicaBackdrop();
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(Header); // System caption buttons retain Snap Layouts, accessibility and DPI handling.
        AppWindow.Resize(new Windows.Graphics.SizeInt32(1440, 960));
        AppWindow.TitleBar.PreferredHeightOption = TitleBarHeightOption.Standard;
        AppWindow.TitleBar.ButtonBackgroundColor = Colors.Transparent;
        AppWindow.TitleBar.ButtonInactiveBackgroundColor = Colors.Transparent;
        Root.ActualThemeChanged += (_, _) => UpdateCaptionColors();
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
    private static Border Card(UIElement child) => new() { Child = child, Padding = new Thickness(20), CornerRadius = new CornerRadius(8), BorderThickness = new Thickness(1), BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"], Background = (Brush)Application.Current.Resources["CardBackgroundFillColorDefaultBrush"] };
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
    private static TextBox Input(string header, string placeholder = "", double width = double.NaN) => new() { Header = header, PlaceholderText = placeholder, Width = width, HorizontalAlignment = HorizontalAlignment.Stretch };
    private static TextBlock TitleText(string title) => new() { Text = title, FontSize = 28, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold };
    private FrameworkElement HomePage()
    {
        return Scroll(Stack(TitleText("매일의 업무를, 한곳에서"), Text("파일과 입력 내용은 이 기기 안에서만 처리해요. 로그인 없이 바로 시작하세요.", 16),
            Card(Stack(Text("차량 조회", 13), Text("탁송체크", 22), Text("한빛탁송 · 오픈마일 · 렌톡 엑셀을 대조하고 주유비용과 탁송 상태를 정리해요."), Button("탁송체크 열기", () => Go("dispatch"), true))),
            Card(Stack(Text("은행 업로드 파일", 13), Text("하나은행 이체파일", 22), Text("Slack 요청을 붙여넣고, 계좌와 금액을 검토한 뒤 다계좌이체 Excel 파일을 만들어요."), Button("이체파일 만들기", () => Go("transfer"), true))),
            Card(Stack(Text("시작하기 전에", 18), Text("은행에 올리기 전에는 금액과 계좌를 원문과 대조해 주세요. 파일 생성은 실제 송금을 실행하지 않아요."), Button("사용 방법", () => Go("help"))))));
    }

    private FrameworkElement DispatchPage()
    {
        vehicles = Input("차량목록", "2026-11-25 10:00  123가4567\n45나6789");
        vehicles.AcceptsReturn = true; vehicles.Height = 100;
        fileSummary = Text("업체 엑셀 파일을 첨부해 주세요. .xlsx / .xls · 암호 파일 지원");
        dispatchSummary = Text("차량목록과 업체 파일을 넣고 조회하세요.");
        dispatchList = new StackPanel();
        filter = new ComboBox { Header = "조회 상태", Width = 180 };
        foreach (var (label, key) in new[] { ("전체", "all"), ("확인 필요", "review"), ("금액 확인", "confirmed"), ("진행 중", "progress"), ("미조회", "missing"), ("제주 출발", "jeju"), ("캐리어탁송", "carrier"), ("슬랙봇 문의", "slack") })
            filter.Items.Add(new ComboBoxItem { Content = label, Tag = key });
        filter.SelectedIndex = 0; filter.SelectionChanged += (_, _) => { dispatchPage = 0; RenderDispatch(); };
        search = Input("차량번호 검색", "번호 일부", 180); search.TextChanged += (_, _) => { dispatchPage = 0; RenderDispatch(); };
        scope = new ComboBox { Header = "저장 범위", Width = 180, ItemsSource = new[] { "현재 표시 목록", "전체 조회 결과", "금액 확인 차량" }, SelectedIndex = 0 };
        var grid = new Grid { RowSpacing = 14 };
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        var export = new MenuFlyout();
        void ExportItem(string title, Func<Task> action)
        {
            var item = new MenuFlyoutItem { Text = title };
            item.Click += async (_, _) =>
            {
                try { await action(); }
                catch (Exception e) { Notify(e.Message, true); }
            };
            export.Items.Add(item);
        }
        ExportItem("Excel 저장", () => ExportDispatch(".xlsx"));
        ExportItem("TXT 저장", () => ExportDispatch(".txt"));
        ExportItem("PNG 저장", () => CaptureDispatch(false));
        ExportItem("PDF 저장", () => CaptureDispatch(false, true));
        previousResults = Button("이전", () => ChangeDispatchPage(-1));
        nextResults = Button("다음", () => ChangeDispatchPage(1));
        var inputs = SplitLayout(vehicles, Stack(Text("업체 파일", 14), fileSummary,
            Horizontal(Bar(AsyncButton("Excel 첨부", ImportFiles), Button("첨부 초기화", () =>
            {
                files.Clear(); results.Clear(); selected.Clear(); completed.Clear();
                fileSummary.Text = "첨부 파일을 모두 지웠어요.";
                RenderDispatch();
            }), Button("조회하기", QueryDispatch, true)))), 820);
        var inputPanel = new Expander { Header = "조회 자료", Content = inputs, IsExpanded = true, HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch };
        var actions = Horizontal(Bar(
            AsyncButton("번호 복사", () => { Copy(string.Join(Environment.NewLine, ExportScope().Select(x => x.Text("plate")).Distinct())); return Task.CompletedTask; }),
            AsyncButton("캡처 복사", () => CaptureDispatch(true)),
            new DropDownButton { Content = "파일 저장", Flyout = export }));
        var top = Stack(TitleText("탁송체크"), inputPanel, Horizontal(Bar(filter, search, scope)), actions);
        top.Spacing = 9;
        grid.Children.Add(top);
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var table = new Grid { MinWidth = 1010 };
        table.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        table.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        table.Children.Add(TableHeader(DispatchColumns, "차량번호", "탁송일정", "주유·충전비", "탁송비", "부가세", "조회 상태", "업체", "완료", "상세"));
        dispatchScroll = new ScrollViewer { Content = dispatchList, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollMode = ScrollMode.Enabled };
        Grid.SetRow(dispatchScroll, 1);
        table.Children.Add(dispatchScroll);
        var list = Horizontal(table);
        Grid.SetRow(list, 1);
        grid.Children.Add(list);
        var footer = SplitLayout(dispatchSummary, Bar(previousResults, nextResults), 820);
        Grid.SetRow(footer, 2);
        grid.Children.Add(footer);
        RenderDispatch();
        return grid;
    }
    private async Task ImportFiles()
    {
        var picker = new FileOpenPicker(); InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        picker.FileTypeFilter.Add(".xlsx"); picker.FileTypeFilter.Add(".xls");
        var picked = await picker.PickMultipleFilesAsync();
        foreach (var file in picked)
        {
            if (files.Any(f => f.Text("path") == file.Path)) { Notify($"{file.Name}: 이미 첨부한 파일이에요."); continue; }
            try
            {
                JsonArray sheets;
                try { sheets = await Task.Run(() => Spreadsheets.Read(file.Path)); }
                catch (ExcelDataReader.Exceptions.InvalidPasswordException)
                {
                    var password = new PasswordBox { Header = file.Name, PlaceholderText = "엑셀 암호" };
                    var dialog = Dialog("암호가 설정된 파일", password, "불러오기");
                    if (await dialog.ShowAsync() != ContentDialogResult.Primary) continue;
                    sheets = await Task.Run(() => Spreadsheets.Read(file.Path, password.Password));
                    password.Password = "";
                }
                var parsed = rules.ReadSheets(sheets, file.Name); parsed["path"] = file.Path; files.Add(parsed);
            }
            catch (Exception e) { Notify($"{file.Name}: {e.Message}", true); }
        }
        fileSummary.Text = string.Join("   ·   ", files.Select(f => $"{f.Text("name")} ({f!["records"]!.AsArray().Count:N0}건)"));
    }
    private void QueryDispatch()
    {
        try
        {
            var input = rules.ParseVehicles(vehicles.Text);
            if (input["invalid"]!.AsArray().Count > 0)
            { Notify("차량번호나 날짜를 확인해 주세요: " + string.Join(", ", input["invalid"]!.AsArray().Select(x => x!.ToString())), true); return; }
            if (input["vehicles"]!.AsArray().Count == 0) { Notify("차량목록을 입력해 주세요.", true); return; }
            if (files.Count == 0) { Notify("업체 파일을 첨부해 주세요.", true); return; }
            results = rules.Match(input["vehicles"]!.AsArray(), files);
            selected.Clear(); completed.Clear(); dispatchPage = 0; RenderDispatch();
        }
        catch (Exception e) { Notify(e.Message, true); }
    }
    private JsonArray VisibleResults()
    {
        var visible = rules.Filter(results, (filter.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "all");
        return new JsonArray(visible.Where(r => r.Text("plate").Contains(search.Text.Trim(), StringComparison.OrdinalIgnoreCase)).Select(r => r!.DeepClone()).ToArray());
    }
    private JsonArray ExportScope()
    {
        var rows = selected.Count > 0 ? new JsonArray(results.Where(r => selected.Contains(r.Text("id"))).Select(r => r!.DeepClone()).ToArray())
            : scope.SelectedIndex == 1 ? results : scope.SelectedIndex == 2 ? rules.Filter(results, "confirmed") : VisibleResults();
        if (rows.Count == 0) throw new InvalidOperationException("복사하거나 저장할 조회 결과가 없어요.");
        if (rows.Any(r => r!.Flag("scheduleConflict") || r!.Flag("feeConflict"))) throw new InvalidOperationException("겹친 탁송일정과 비용을 먼저 선택해 주세요.");
        return rows;
    }
    private void RenderDispatch()
    {
        if (dispatchList is null || search is null) return;
        dispatchList.Children.Clear();
        var visible = VisibleResults();
        dispatchPage = Math.Min(dispatchPage, Math.Max(0, (visible.Count - 1) / 100));
        visibleCount = visible.Count;
        visibleAmount = Sum(visible, "amount");
        UpdateDispatchSummary();
        previousResults.IsEnabled = dispatchPage > 0;
        nextResults.IsEnabled = (dispatchPage + 1) * 100 < visibleCount;
        foreach (var row in visible.Skip(dispatchPage * 100).Take(100))
        {
            var id = row.Text("id");
            var check = new CheckBox { Content = row.Text("plate"), IsChecked = selected.Contains(id), MinWidth = 0 };
            check.Checked += (_, _) => { selected.Add(id); UpdateDispatchSummary(); };
            check.Unchecked += (_, _) => { selected.Remove(id); UpdateDispatchSummary(); };
            var done = new CheckBox { IsChecked = completed.Contains(id), MinWidth = 0, Padding = new Thickness(0) };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(done, row.Text("plate") + " 완료");
            done.Checked += (_, _) => completed.Add(id); done.Unchecked += (_, _) => completed.Remove(id);
            var content = Stack(Text(string.Join(" · ", new[] { row.Text("rawState"), row.Text("carrierType"), row.Text("origin") }.Where(value => value.Length > 0))));
            foreach (var (field, label) in new[] { ("schedule", "탁송일정"), ("fee", "탁송비용") })
            {
                if (!row!.Flag(field + "Conflict")) continue;
                var candidates = row![field + "Candidates"]!.AsArray();
                var combo = new ComboBox { Header = label + "이 달라요. 사용할 기록을 선택하세요.", HorizontalAlignment = HorizontalAlignment.Stretch };
                foreach (var candidate in candidates) combo.Items.Add(new ComboBoxItem { Content = $"{candidate.Text("value")} · {candidate.Text("source")}", Tag = candidate.Text("id") });
                combo.SelectionChanged += (_, _) =>
                {
                    if (combo.SelectedItem is not ComboBoxItem item) return;
                    var index = results.Select((r, i) => (r, i)).First(p => p.r.Text("id") == id).i;
                    results[index] = rules.Choose(results[index]!, field, item.Tag.ToString()!); RenderDispatch();
                };
                content.Children.Add(combo);
            }
            var notes = rules.Notes(row!); if (notes.Length > 0) content.Children.Add(Text(notes));
            content.Children.Add(Text(row.Text("sources"), 12));
            var status = Stack(Text(rules.Status(row!)));
            status.Spacing = 0;
            content.MaxWidth = 420;
            var needsChoice = row!.Flag("scheduleConflict") || row!.Flag("feeConflict");
            if (needsChoice)
            {
                var flagged = Text("일정·비용 선택 필요", 12);
                flagged.Foreground = ThemeBrush("SystemFillColorCautionBrush");
                status.Children.Add(flagged);
            }
            var details = new Button { Content = needsChoice ? "선택" : "보기", Flyout = new Flyout { Content = content } };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(details, row.Text("plate") + " 상세");
            var entry = Columns(DispatchColumns, check, Text(row.Text("schedule")), Amount(row, "amount"),
                Amount(row, "dispatchFee"), Amount(row, "vat"), status, Text(row.Text("vendor")), done, details);
            dispatchList.Children.Add(TableRow(entry));
        }
        if (visible.Count == 0) dispatchList.Children.Add(Text(results.Count == 0 ? "조회 결과가 여기에 표시돼요." : "현재 조건에 맞는 차량이 없어요."));
    }
    private void ChangeDispatchPage(int offset)
    {
        dispatchPage += offset;
        RenderDispatch();
        dispatchScroll.ChangeView(null, 0, null);
    }
    private void UpdateDispatchSummary() => dispatchSummary.Text =
        $"전체 {results.Count:N0}대 · 표시 {visibleCount:N0}대 · 선택 {selected.Count:N0}대 · 주유·충전비 {visibleAmount:N0}원 · {dispatchPage + 1}/{Math.Max(1, (visibleCount + 99) / 100)}페이지";
    private static string Money(JsonNode? row, string field) => decimal.TryParse(row.Text(field), NumberStyles.Any, CultureInfo.InvariantCulture, out var n) ? $"{n:N0}원" : "—";
    private static TextBlock Amount(JsonNode? row, string field)
    {
        var text = Text(Money(row, field));
        text.TextAlignment = TextAlignment.Right;
        return text;
    }
    private static decimal Sum(JsonArray rows, string field) => rows.Sum(r => decimal.TryParse(r.Text(field), NumberStyles.Any, CultureInfo.InvariantCulture, out var n) ? n : 0);
    private async Task ExportDispatch(string extension)
    {
        var data = ExportScope();
        var file = await SavePicker("탁송체크_" + DateTime.Now.ToString("yyyyMMdd_HHmm"), extension);
        if (file is null) return;
        if (extension == ".xlsx") { var values = rules.ExportRows(data); await Task.Run(() => Spreadsheets.Write(file.Path, values)); }
        else await File.WriteAllTextAsync(file.Path, rules.ExportText(data));
        Notify("조회 결과를 저장했어요.");
    }

    private FrameworkElement TransferPage()
    {
        thread = Input("Slack 스레드", "차량번호, 은행, 계좌번호, 금액이 포함된 요청을 붙여넣으세요.");
        thread.AcceptsReturn = true; thread.Height = 280; thread.MaxLength = 50_000;
        transferSummary = Text("이체 요청을 붙여넣고 추출해 주세요.");
        transferList = new StackPanel { Spacing = 12 };
        acknowledge = new CheckBox { Content = "검토 항목과 원문을 대조했고, 중복 송금 여부를 확인했어요." };
        acknowledge.Checked += (_, _) => { acknowledgedSignature = Signature; ValidateTransfers(); };
        acknowledge.Unchecked += (_, _) => { acknowledgedSignature = ""; ValidateTransfers(); };
        download = AsyncButton("하나은행 이체파일 저장", SaveTransfer, true);
        previewRows = new StackPanel();
        var previewTable = Stack(TableHeader(TransferColumns, "입금은행", "입금계좌번호", "입금액", "출금통장 표시", "입금통장 표시", "CMS코드"), previewRows);
        previewTable.Spacing = 0;
        previewTable.MinWidth = 790;
        transferInput = new Grid();
        transferInput.Children.Add(Scroll(Stack(Card(Stack(Text("① 이체 요청 입력", 20), thread, Button("이체정보 추출하기", ExtractTransfer, true))),
            Text("같은 요청을 다시 추출하면 이미 가져온 건은 건너뛰어요. 새 스레드를 붙여넣어 추가할 수 있어요."))));
        transferReview = new Grid();
        transferReview.Children.Add(Scroll(Stack(
            Horizontal(Bar(Button("내역 추가", AddTransfer), Button("1억 초과 분할", SplitTransfers), Button("파일 미리보기", () => Step(3), true))),
            new Expander { Header = "통장 메모·CMS 일괄 적용", Content = MemoControls(), HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch },
            transferList)));
        transferDownload = new Grid { RowSpacing = 12 };
        transferDownload.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        transferDownload.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        transferDownload.Children.Add(new ScrollViewer
        {
            Content = Card(Stack(Text("파일 미리보기", 20), Horizontal(previewTable))),
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollMode = ScrollMode.Disabled
        });
        acknowledge.Content = Text("검토 항목과 원문을 대조했고, 중복 송금 여부를 확인했어요.");
        var saveActions = Stack(acknowledge,
            Horizontal(Bar(download, Button("송금 코멘트 복사", () => Copy(rules.Comments(transfers))))));
        saveActions.Spacing = 6;
        Grid.SetRow(saveActions, 1);
        transferDownload.Children.Add(saveActions);
        var grid = new Grid { RowSpacing = 16 };
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        transferSteps = [Button("1. 요청 입력", () => Step(1)), Button("2. 확인·수정", () => Step(2)), Button("3. 미리보기·저장", () => Step(3))];
        grid.Children.Add(Stack(TitleText("하나은행 이체파일"),
            Horizontal(Bar([.. transferSteps, AsyncButton("전체 초기화", ResetTransfers)])), transferSummary));
        var host = new Grid(); host.Children.Add(transferInput); host.Children.Add(transferReview); host.Children.Add(transferDownload);
        Grid.SetRow(host, 1); grid.Children.Add(host); Step(1);
        return grid;
    }
    private UIElement MemoControls()
    {
        var outMemo = Input("출금통장 표시", "일괄 적용", 160);
        var inMemo = Input("입금통장 표시", "일괄 적용", 160);
        var cms = Input("CMS코드", "일괄 적용", 130);
        var onlyEmpty = new CheckBox { Content = "빈 칸만", IsChecked = true, VerticalAlignment = VerticalAlignment.Bottom };
        return Stack(Bar(outMemo, inMemo, cms, onlyEmpty), Button("메모 적용", () =>
        {
            foreach (var r in transfers)
                foreach (var (key, value) in new[] { ("outMemo", outMemo.Text), ("inMemo", inMemo.Text), ("cms", cms.Text) })
                    if (value.Length > 0 && (onlyEmpty.IsChecked != true || r.Text(key) == "")) r![key] = value;
            RenderTransfers();
        }));
    }
    private void ExtractTransfer()
    {
        try
        {
            if (thread.Text.Trim().Length == 0) { Notify("이체 요청을 붙여넣어 주세요.", true); return; }
            var parsed = rules.ParseTransfer(thread.Text); var added = 0;
            foreach (var item in parsed["items"]!.AsArray())
            {
                var key = item!.ToJsonString();
                if (!imported.Add(key)) continue;
                var row = item.DeepClone().AsObject();
                row["id"] = Guid.NewGuid().ToString("N"); row["ambiguous"] = parsed["ambiguous"]!.DeepClone();
                row["outMemo"] = row.Text("plate"); row["inMemo"] = row.Text("plate"); row["cms"] = "";
                row["holder"] = ""; transfers.Add(row); added++;
            }
            transfers = rules.Split(transfers); RenderTransfers(); Step(2);
            Notify($"{added}건을 추가했어요. 이미 가져온 건은 건너뛰었어요.");
        }
        catch (Exception e) { Notify(e.Message, true); }
    }
    private void AddTransfer()
    {
        transfers.Add(new JsonObject { ["id"] = Guid.NewGuid().ToString("N"), ["bank"] = "", ["account"] = "", ["amount"] = "", ["plate"] = "", ["outMemo"] = "", ["inMemo"] = "", ["cms"] = "", ["source"] = "" });
        RenderTransfers();
    }
    private void SplitTransfers() { transfers = rules.Split(transfers); RenderTransfers(); }
    private string Signature => transfers.ToJsonString();
    private void RenderTransfers()
    {
        refreshing = true; transferList.Children.Clear(); transferStatuses.Clear();
        foreach (var row in transfers.ToArray())
        {
            var bank = new ComboBox { Header = "입금은행", ItemsSource = rules.Banks, SelectedItem = row.Text("bank"), Width = 155 };
            bank.SelectionChanged += (_, _) => { if (!refreshing) { row!["bank"] = bank.SelectedItem?.ToString() ?? ""; ValidateTransfers(); } };
            var account = Edit(row!, "account", "입금계좌번호", 190);
            var amount = Edit(row!, "amount", "입금액(원)", 146);
            amount.TextAlignment = TextAlignment.Right;
            var plate = Edit(row!, "plate", "차량번호", 125);
            var main = SplitLayout(Horizontal(Bar(bank, account)), Horizontal(Bar(amount, plate)), 670);
            var remove = Button("삭제", () => { transfers.Remove(row); RenderTransfers(); });
            var memo = Bar(Edit(row!, "outMemo", "출금통장 표시", 180), Edit(row!, "inMemo", "입금통장 표시", 180), Edit(row!, "cms", "CMS코드", 140));
            var status = Text(""); status.Tag = row.Text("id");
            transferStatuses.Add(status);
            var original = new TextBox { Header = "Slack 원문", Text = row.Text("source"), IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MaxHeight = 220 };
            var details = new Expander { Header = "메모·CMS 및 원문", Content = Stack(Horizontal(memo), original), HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch };
            var card = Card(Stack(main, SplitLayout(Stack(Text($"{transferStatuses.Count}번", 13), status), remove, 670), details));
            transferList.Children.Add(card);
        }
        refreshing = false; ValidateTransfers();
    }
    private TextBox Edit(JsonNode row, string key, string label, double width)
    {
        var box = Input(label, "", width); box.Text = row.Text(key);
        box.TextChanged += (_, _) => { if (!refreshing) { row[key] = box.Text; ValidateTransfers(); } };
        return box;
    }
    private void ValidateTransfers()
    {
        if (refreshing || acknowledge is null) return;
        if (acknowledge.IsChecked == true && acknowledgedSignature != Signature) acknowledge.IsChecked = false;
        var checks = rules.Check(transfers); var bad = checks.Count(x => x.Text("state") == "bad"); var warn = checks.Count(x => x.Text("state") == "warn");
        transferSummary.Text = $"{transfers.Count:N0}건 · 합계 {Sum(transfers, "amount"):N0}원 · 오류 {bad}건 · 검토 {warn}건" + (transfers.Count > 100 ? " · 한 파일에 100건까지 가능해요." : "");
        for (var i = 0; i < checks.Count && i < transferStatuses.Count; i++)
        {
            var status = transferStatuses[i];
            status.Text = string.Join(" · ", checks[i]!["errors"]!.AsArray().Concat(checks[i]!["reviews"]!.AsArray()).Select(x => x!.ToString()));
            if (status.Text.Length == 0) status.Text = "확인 완료";
            status.Foreground = ThemeBrush(checks[i].Text("state") switch
            {
                "bad" => "SystemFillColorCriticalBrush",
                "warn" => "SystemFillColorCautionBrush",
                _ => "TextFillColorSecondaryBrush"
            });
        }
        download.IsEnabled = !saving && transfers.Count is > 0 and <= 100 && bad == 0 && (warn == 0 || acknowledge.IsChecked == true);
        if (step == 3) UpdatePreview();
    }
    private void Step(int n)
    {
        step = n;
        for (var i = 0; i < transferSteps.Length; i++)
            transferSteps[i].Style = i + 1 == n ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
        transferInput.Visibility = n == 1 ? Visibility.Visible : Visibility.Collapsed;
        transferReview.Visibility = n == 2 ? Visibility.Visible : Visibility.Collapsed;
        transferDownload.Visibility = n == 3 ? Visibility.Visible : Visibility.Collapsed;
        ValidateTransfers();
    }
    private void UpdatePreview()
    {
        previewRows.Children.Clear();
        if (transfers.Count == 0) previewRows.Children.Add(Text("이체 내역을 추가하면 여기에 표시돼요."));
        foreach (var row in transfers)
        {
            var amount = Amount(row, "amount");
            if (row.Text("amount").Trim().Length == 0) amount.Text = "미입력";
            previewRows.Children.Add(TableRow(Columns(TransferColumns, Text(row.Text("bank")),
                Text(row.Text("account")), amount, Text(row.Text("outMemo")),
                Text(row.Text("inMemo")), Text(row.Text("cms")))));
        }
    }
    private async Task SaveTransfer()
    {
        if (saving) return;
        // Validate again at the write boundary; UI enabled state is not the safety gate.
        var checks = rules.Check(transfers);
        if (transfers.Count is 0 or > 100 || checks.Any(x => x.Text("state") == "bad") || (checks.Any(x => x.Text("state") == "warn") && (acknowledge.IsChecked != true || acknowledgedSignature != Signature)))
            throw new InvalidOperationException("오류를 수정하고 검토 항목을 확인해 주세요.");
        saving = true;
        try
        {
            var signature = Signature; var values = Spreadsheets.TransferRows(transfers);
            var file = await SavePicker("하나은행_다계좌이체_" + DateTime.Now.ToString("yyyyMMdd_HHmm"), ".xlsx");
            if (file is null) return;
            await Task.Run(() => Spreadsheets.Write(file.Path, values, "다계좌이체"));
            savedSignature = signature; savedThread = thread.Text; Notify("이체파일을 저장했어요. 은행 업로드 전에 계좌와 금액을 확인해 주세요.");
        }
        finally { saving = false; ValidateTransfers(); }
    }
    private async Task ResetTransfers()
    {
        if (await Dialog("전체 초기화", Text("붙여넣은 스레드와 이체 내역을 모두 지울까요?"), "모두 지우기").ShowAsync() != ContentDialogResult.Primary) return;
        transfers.Clear(); imported.Clear(); thread.Text = ""; savedSignature = ""; RenderTransfers(); Step(1);
    }
    private ContentDialog Dialog(string title, UIElement content, string primary) => new() { XamlRoot = Root.XamlRoot, Title = title, Content = content, PrimaryButtonText = primary, CloseButtonText = "취소", DefaultButton = ContentDialogButton.Close, RequestedTheme = Root.ActualTheme };
    private async void Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (allowClose || (transfers.Count == 0 && string.IsNullOrWhiteSpace(thread?.Text)) || (transfers.Count > 0 && savedSignature == Signature && savedThread == thread.Text)) return;
        args.Cancel = true;
        if (await Dialog("저장하지 않은 내용이 있어요", Text("창을 닫으면 입력 내용과 이체 내역이 지워져요. 닫을까요?"), "닫기").ShowAsync() == ContentDialogResult.Primary) { allowClose = true; Close(); }
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
    private void Copy(string value)
    {
        try { var data = new DataPackage(); data.SetText(value); Clipboard.SetContent(data); Notify("클립보드에 복사했어요."); }
        catch (Exception e) { Notify(e.Message, true); }
    }
    private FrameworkElement HelpPage() => Scroll(Stack(TitleText("사용 방법"),
        Card(Stack(Text("탁송체크", 21), Text("1. 차량번호 목록을 붙여넣어요. 날짜·시간도 함께 넣을 수 있어요.\n2. 한빛탁송·오픈마일·렌톡에서 내려받은 업체 엑셀을 첨부해요. 암호 파일은 암호를 입력해요.\n3. 차량 조회하기를 누르고 ‘확인 필요’ 항목부터 검토해요.\n4. 일정·비용이 겹치면 원본 위치를 보고 사용할 값을 골라요.\n5. 필요한 줄을 선택하고 TXT·Excel·PNG로 저장하거나 캡처를 복사해요."))),
        Card(Stack(Text("하나은행 이체파일", 21), Text("1. Slack 스레드를 붙여넣고 이체정보를 추출해요.\n2. 은행·계좌·금액·차량번호를 원문과 대조해요. 메모와 CMS코드도 입력할 수 있어요.\n3. 오류를 모두 수정하고 검토 항목을 확인해요. 수정하면 확인 체크가 풀려요.\n4. 미리보기를 확인한 뒤 Excel 파일을 저장해요. 1억 원 초과 금액은 분할하며 파일당 100건까지예요.\n5. 은행 업로드 화면에서 받는 분과 금액을 다시 확인해 주세요."))),
        Text("입력값과 첨부 파일은 서버로 전송하지 않아요. 앱을 닫으면 작업 내용은 지워지고, 저장한 파일은 기기에 남아요.")));
    private FrameworkElement SettingsPage()
    {
        var theme = new ComboBox { Header = "화면 모드", ItemsSource = new[] { "시스템 설정", "라이트", "다크" }, SelectedIndex = (int)Root.RequestedTheme, Width = 220 };
        theme.SelectionChanged += (_, _) => Root.RequestedTheme = (ElementTheme)theme.SelectedIndex;
        return Stack(TitleText("설정"), Card(Stack(theme, Text("Pretendard 글꼴을 앱에 포함해 별도 설치 없이 사용해요."), Text("WorkTool · Windows 네이티브\nWinUI 3 / 로컬 처리"))));
    }
}
