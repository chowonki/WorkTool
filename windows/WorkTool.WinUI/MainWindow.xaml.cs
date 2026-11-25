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
    private readonly HashSet<string> copiedDispatch = new();
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
    private TextBlock vehicleCount = null!;
    private static readonly double[] DefaultDispatchColumns = [134, 0, 102, 106, 96, 145, 90, 48, 64];
    private static readonly string[] DispatchHeadings = ["차량번호", "탁송일정", "주유·충전비", "탁송비", "부가세", "조회 상태", "업체", "완료", "상세"];
    private readonly double[] dispatchColumns = (double[])DefaultDispatchColumns.Clone();
    private Grid dispatchTable = null!;
    private static readonly double[] TransferColumns = [120, 178, 120, 0, 0, 95];
    private Button previousResults = null!, nextResults = null!;
    private ScrollViewer dispatchScroll = null!;
    private StackPanel previewRows = null!;
    private Button[] transferSteps = [];
    private readonly List<(TextBlock Label, Border Tag)> transferStatuses = [];
    private int visibleCount;
    private decimal visibleAmount;

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

    private FrameworkElement DispatchPage()
    {
        vehicles = Input("차량목록", "2026-11-25 10:00  123가4567\n45나6789");
        vehicles.AcceptsReturn = true; vehicles.Height = 100;
        vehicleCount = Text("입력 차량 0대", 13);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetLiveSetting(vehicleCount, Microsoft.UI.Xaml.Automation.Peers.AutomationLiveSetting.Polite);
        vehicles.TextChanged += (_, _) => { UpdateVehicleCount(); InvalidateDispatch(); };
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
        var vehicleActions = Columns([0, 90], vehicleCount, Button("목록 정리", CleanVehicles));
        vehicleActions.Padding = new Thickness(0);
        var vehicleInput = Stack(vehicles, vehicleActions);
        vehicleInput.Spacing = 5;
        dispatchFiles = new StackPanel();
        var fileList = new ScrollViewer { Content = dispatchFiles, MaxHeight = 112, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        var filePanel = Stack(Text("업체 파일", 14), fileSummary, fileList,
            WrapActions(AsyncButton("Excel 첨부", ImportFiles), Button("첨부 초기화", () =>
            {
                if (importingDispatch) return;
                files.Clear(); RenderDispatchFiles(); InvalidateDispatch();
            }), Button("조회하기", QueryDispatch, true)));
        filePanel.Spacing = 8;
        var dropArea = new Border { Child = filePanel, Background = cardFill,
            Padding = new Thickness(12, 0, 12, 8), CornerRadius = new CornerRadius(6), AllowDrop = true };
        dropArea.DragOver += (_, args) =>
        {
            if (!importingDispatch && args.DataView.Contains(StandardDataFormats.StorageItems)) args.AcceptedOperation = DataPackageOperation.Copy;
        };
        dropArea.Drop += async (_, args) =>
        {
            var deferral = args.GetDeferral();
            try
            {
                if (args.DataView.Contains(StandardDataFormats.StorageItems))
                    await AddDispatchFiles((await args.DataView.GetStorageItemsAsync()).OfType<StorageFile>());
            }
            catch (Exception error) { Notify(error.Message, true); }
            finally { deferral.Complete(); }
        };
        RenderDispatchFiles();
        var inputs = SplitLayout(vehicleInput, dropArea, 820);
        var inputPanel = new Expander { Header = "조회 자료", Content = inputs, IsExpanded = true, HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch };
        var actions = WrapActions(
            Button("번호 복사", CopyDispatchNumbers),
            Button("표시 차량 선택", () =>
            {
                foreach (var row in VisibleResults()) selected.Add(row.Text("id"));
                RenderDispatch();
            }),
            Button("선택 해제", () => { selected.Clear(); RenderDispatch(); }),
            AsyncButton("캡처 복사", () => CaptureDispatch(true)),
            new DropDownButton { Content = "파일 저장", Flyout = export });
        var top = Stack(PageHeading("탁송체크", "\uE804", dispatchTone, AsyncButton("전체 초기화", ResetDispatch)), inputPanel,
            WrapActions(filter, search, scope, AsyncButton("필터·정렬", EditDispatchFilters),
                AsyncButton("열 너비", EditDispatchWidths)), actions);
        top.Spacing = 9;
        grid.Children.Add(top);
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        dispatchTable = new Grid();
        dispatchTable.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        dispatchTable.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        LoadDispatchWidths();
        dispatchScroll = new ScrollViewer { Content = dispatchList, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollMode = ScrollMode.Enabled };
        Grid.SetRow(dispatchScroll, 1);
        dispatchTable.Children.Add(dispatchScroll);
        UpdateDispatchHeader();
        var list = Horizontal(dispatchTable);
        Grid.SetRow(list, 1);
        grid.Children.Add(list);
        dispatchSummary.VerticalAlignment = VerticalAlignment.Center;
        var pagination = Bar(previousResults, nextResults);
        pagination.HorizontalAlignment = HorizontalAlignment.Right;
        var footer = SplitLayout(dispatchSummary, pagination, 720);
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
        await AddDispatchFiles(picked);
    }

    private async Task AddDispatchFiles(IEnumerable<StorageFile> picked)
    {
        if (importingDispatch) return;
        importingDispatch = true;
        try
        {
            foreach (var file in picked)
            {
                if (file.FileType.ToLowerInvariant() is not (".xlsx" or ".xls")) { Notify($"{file.Name}: Excel 파일을 첨부해 주세요.", true); continue; }
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
                        var value = password.Password;
                        password.Password = "";
                        sheets = await Task.Run(() => Spreadsheets.Read(file.Path, value));
                    }
                    var parsed = rules.ReadSheets(sheets, file.Name);
                    parsed["path"] = file.Path;
                    files.Add(parsed);
                    InvalidateDispatch();
                }
                catch (Exception e) { Notify($"{file.Name}: {e.Message}", true); }
            }
        }
        finally { importingDispatch = false; RenderDispatchFiles(); }
    }
    private void UpdateVehicleCount()
    {
        if (string.IsNullOrWhiteSpace(vehicles.Text))
        {
            vehicleCount.Text = "입력 차량 0대";
            return;
        }

        try
        {
            var input = rules.ParseVehicles(vehicles.Text);
            var duplicates = input["duplicates"]!.GetValue<int>();
            var invalid = input["invalid"]!.AsArray().Count;
            var count = input["vehicles"]!.AsArray().Count - duplicates;
            var parts = new List<string> { $"입력 차량 {count:N0}대" };
            if (duplicates > 0) parts.Add($"중복 {duplicates:N0}줄");
            if (invalid > 0) parts.Add($"확인 필요 {invalid:N0}줄");
            vehicleCount.Text = string.Join(" · ", parts);
        }
        catch (Exception)
        {
            vehicleCount.Text = "차량 수를 계산할 수 없어요.";
        }
    }

    private void QueryDispatch()
    {
        if (importingDispatch) { Notify("업체 파일을 읽는 중이에요."); return; }
        try
        {
            var input = rules.ParseVehicles(vehicles.Text);
            if (input["invalid"]!.AsArray().Count > 0)
            { Notify("차량번호나 날짜를 확인해 주세요: " + string.Join(", ", input["invalid"]!.AsArray().Select(x => x!.ToString())), true); return; }
            if (input["vehicles"]!.AsArray().Count == 0) { Notify("차량목록을 입력해 주세요.", true); return; }
            if (files.Count == 0) { Notify("업체 파일을 첨부해 주세요.", true); return; }
            results = rules.Match(input["vehicles"]!.AsArray(), files);
            selected.Clear(); completed.Clear(); copiedDispatch.Clear(); dispatchPage = 0; RenderDispatch();
        }
        catch (Exception e) { Notify(e.Message, true); }
    }
    private JsonArray VisibleResults()
    {
        var visible = rules.Filter(results, (filter.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "all");
        var matching = visible.Where(row => row.Text("plate").Contains(search.Text.Trim(), StringComparison.OrdinalIgnoreCase)
            && MatchesDispatchOptions(row!));
        return new JsonArray(SortDispatch(matching).Select(row => row!.DeepClone()).ToArray());
    }
    private JsonArray ExportScope()
    {
        var rows = selected.Count > 0 ? SelectedOrVisibleDispatch()
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
            var plate = row.Text("plate");
            var check = new CheckBox { IsChecked = selected.Contains(id), MinWidth = 0, Padding = new Thickness(0) };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(check, plate + " 행 선택");
            check.Checked += (_, _) => { selected.Add(id); UpdateDispatchSummary(); };
            check.Unchecked += (_, _) => { selected.Remove(id); UpdateDispatchSummary(); };
            var done = new CheckBox { IsChecked = completed.Contains(id), MinWidth = 0, Padding = new Thickness(0),
                HorizontalAlignment = HorizontalAlignment.Center };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(done, row.Text("plate") + " 완료");
            var workStatus = Text("", 11);
            workStatus.MinHeight = 14;
            void UpdateWorkStatus()
            {
                workStatus.Text = completed.Contains(id) ? (copiedDispatch.Contains(plate) ? "완료 · 복사함" : "완료")
                    : copiedDispatch.Contains(plate) ? "복사함" : "";
                workStatus.Foreground = ThemeBrush(completed.Contains(id) ? "SystemFillColorSuccessBrush" : "AccentTextFillColorPrimaryBrush");
            }
            done.Checked += (_, _) => { completed.Add(id); UpdateWorkStatus(); };
            done.Unchecked += (_, _) => { completed.Remove(id); UpdateWorkStatus(); };
            var content = Stack(Text(string.Join(" · ", new[] { row.Text("rawState"), row.Text("carrierType"), row.Text("origin") }.Where(value => value.Length > 0))));
            foreach (var (field, label) in new[] { ("schedule", "탁송일정"), ("fee", "탁송비용") })
            {
                var candidates = row![field + "Candidates"]!.AsArray();
                if (!row.Flag(field + "Conflict") && candidates.Select(candidate => candidate.Text("value")).Distinct().Take(2).Count() < 2) continue;
                var combo = new ComboBox { Header = label + " 원본 기록 선택", HorizontalAlignment = HorizontalAlignment.Stretch };
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
            var statusText = Text(rules.Status(row!), 13);
            statusText.TextWrapping = TextWrapping.WrapWholeWords;
            var tag = StatusTag(statusText);
            TintTag(tag, statusText, DispatchStatusTone(row.Text("status")));
            var status = Stack(tag);
            status.Spacing = 3;
            content.MaxWidth = 420;
            var needsChoice = row!.Flag("scheduleConflict") || row!.Flag("feeConflict");
            if (needsChoice)
            {
                var flagged = Text("일정·비용 선택 필요", 12);
                flagged.Foreground = attentionTone.Ink;
                status.Children.Add(flagged);
            }
            var details = new Button { Content = needsChoice ? "선택" : "보기", Flyout = new Flyout { Content = content },
                HorizontalAlignment = HorizontalAlignment.Center };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(details, row.Text("plate") + " 상세");
            void MarkCopied() { copiedDispatch.Add(plate); UpdateWorkStatus(); }
            UpdateWorkStatus();
            var plateLink = Bar(check, CopyCell("차량번호", plate, plate, onCopy: MarkCopied));
            plateLink.VerticalAlignment = VerticalAlignment.Center;
            check.VerticalAlignment = VerticalAlignment.Center;
            workStatus.VerticalAlignment = VerticalAlignment.Bottom;
            workStatus.Margin = new Thickness(28, 0, 0, 0);
            workStatus.IsHitTestVisible = false;
            var plateCell = new Grid { MinHeight = 48 };
            plateCell.Children.Add(plateLink);
            plateCell.Children.Add(workStatus);
            var schedule = row.Text("schedule");
            var entry = Columns(dispatchColumns, plateCell, CopyCell("탁송일정", schedule, schedule, onCopy: MarkCopied),
                CopyCell("주유·충전비", row.Text("amount"), Money(row, "amount"), right: true, onCopy: MarkCopied),
                CopyCell("탁송비", row.Text("dispatchFee"), Money(row, "dispatchFee"), right: true, onCopy: MarkCopied),
                CopyCell("부가세", row.Text("vat"), Money(row, "vat"), right: true, onCopy: MarkCopied),
                status, Text(row.Text("vendor")), done, details);
            dispatchList.Children.Add(TableRow(entry));
        }
        if (visible.Count == 0) dispatchList.Children.Add(Text(results.Count == 0 ? "조회 결과가 여기에 표시돼요." : "현재 조건에 맞는 차량이 없어요."));
    }
    private FrameworkElement CopyCell(string label, string value, string display, bool right = false, Action? onCopy = null)
    {
        if (string.IsNullOrWhiteSpace(value) || value == "—")
        {
            var text = Text("—");
            if (right) text.TextAlignment = TextAlignment.Right;
            return text;
        }

        var button = new HyperlinkButton
        {
            Content = display,
            Padding = new Thickness(0, 4, 0, 4),
            HorizontalAlignment = right ? HorizontalAlignment.Right : HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Center
        };
        ToolTipService.SetToolTip(button, label + " 복사");
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(button, $"{label} 복사: {display}");
        button.Click += (_, _) => Copy(value, onCopy);
        return button;
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
        LoadTransferWidths();
        threadInputs = new StackPanel { Spacing = 12 };
        thread = AddThreadInput(first: true);
        transferSummary = Text("이체 요청을 붙여넣고 추출해 주세요.");
        transferList = new StackPanel { Spacing = 12 };
        acknowledge = new CheckBox { Content = "검토 항목과 원문을 대조했고, 중복 송금 여부를 확인했어요." };
        acknowledge.Checked += (_, _) => { acknowledgedSignature = Signature; ValidateTransfers(); };
        acknowledge.Unchecked += (_, _) => { acknowledgedSignature = ""; ValidateTransfers(); };
        download = TintTransferAction(AsyncButton("하나은행 이체파일 저장", SaveTransfer, true));
        previewRows = new StackPanel();
        var previewHeader = TableHeader(TransferColumns, "입금은행", "입금계좌번호", "입금액", "출금통장 표시", "입금통장 표시", "CMS코드");
        previewHeader.Background = transferTone.Surface;
        foreach (var label in previewHeader.Children.OfType<TextBlock>()) label.Foreground = transferTone.Ink;
        var previewTable = Stack(previewHeader, previewRows);
        previewTable.Spacing = 0;
        previewTable.MinWidth = 790;
        transferInput = new Grid();
        transferInput.Children.Add(VerticalScroll(Stack(Card(Stack(Text("① 이체 요청 입력", 20), threadInputs,
            WrapActions(Button("입력칸 추가", () => AddThreadInput().Focus(FocusState.Programmatic)),
                TintTransferAction(Button("이체정보 추출하기", ExtractTransfer, true))))),
            Text("같은 요청을 다시 추출하면 이미 가져온 건은 건너뛰어요. 새 스레드를 붙여넣어 추가할 수 있어요."))));
        transferReview = new Grid();
        autoFixAccounts = new CheckBox { Content = "계좌번호의 하이픈·공백 자동 제거", IsChecked = true };
        transferReview.Children.Add(VerticalScroll(Stack(
            WrapActions(Button("내역 추가", AddTransfer), Button("표시 내역 선택", SelectVisibleTransfers),
                AsyncButton("선택 삭제", DeleteSelectedTransfers), Button("1억 초과 분할", SplitTransfers), AsyncButton("열 너비", EditTransferWidths),
                TintTransferAction(Button("파일 미리보기", () => Step(3), true))),
            TransferFilters(), autoFixAccounts,
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
        var saveActions = Stack(acknowledge, WrapActions(download,
            Button("송금 코멘트 전체 복사", () => Copy(rules.Comments(transfers))),
            AsyncButton("코멘트 목록", ShowTransferComments)));
        saveActions.Spacing = 6;
        Grid.SetRow(saveActions, 1);
        transferDownload.Children.Add(saveActions);
        var grid = new Grid { RowSpacing = 16 };
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        transferSteps = [Button("1. 요청 입력", () => Step(1)), Button("2. 확인·수정", () => Step(2)), Button("3. 미리보기·저장", () => Step(3))];
        grid.Children.Add(Stack(PageHeading("하나은행 이체파일", "\uE8AB", transferTone, AsyncButton("전체 초기화", ResetTransfers)),
            WrapActions([.. transferSteps, AsyncButton("단축키", ShowTransferShortcuts)]), transferSummary));
        var host = new Grid(); host.Children.Add(transferInput); host.Children.Add(transferReview); host.Children.Add(transferDownload);
        Grid.SetRow(host, 1); grid.Children.Add(host); Step(1);
        AddTransferShortcuts(grid);
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
            var inputs = threadBoxes.Select(box => (Box: box, Value: box.Text.Trim())).Where(entry => entry.Value.Length > 0).ToArray();
            if (inputs.Length == 0) { Notify("이체 요청을 붙여넣어 주세요.", true); return; }
            var staged = new JsonArray();
            var stagedKeys = new HashSet<string>();
            foreach (var (box, value) in inputs)
            {
                var rawKey = "raw:" + value.Replace("\r\n", "\n").Replace('\r', '\n');
                if (imported.Contains(rawKey) || stagedKeys.Contains(rawKey)) continue;
                var parsed = rules.ParseTransferForReview(value);
                var items = parsed["items"]!.AsArray();
                if (items.Count == 0) continue;
                stagedKeys.Add(rawKey);
                var occurrences = new Dictionary<string, int>();
                foreach (var item in items)
                {
                    var signature = string.Join("|", new[] { item.Text("bank"), item.Text("account"), item.Text("amount") });
                    occurrences[signature] = occurrences.GetValueOrDefault(signature) + 1;
                    var key = $"{box.Tag}:{signature}:{occurrences[signature]}";
                    if (imported.Contains(key) || !stagedKeys.Add(key)) continue;
                    var row = item!.DeepClone().AsObject();
                    row["id"] = Guid.NewGuid().ToString("N");
                    row["ambiguous"] = parsed["ambiguous"]!.DeepClone();
                    row["outMemo"] = row.Text("plate");
                    row["inMemo"] = row.Text("plate");
                    row["cms"] = "";
                    staged.Add(row);
                }
            }
            var split = rules.Split(staged);
            if (transfers.Count + split.Count > 100) { Notify("한 파일에 100건까지 가능해요. 요청을 나눠서 처리해 주세요.", true); return; }
            imported.UnionWith(stagedKeys);
            foreach (var row in split) transfers.Add(row!.DeepClone());
            if (split.Count == 0) { Notify("추가할 새 이체 내역이 없어요."); return; }
            RenderTransfers();
            Step(2);
            Notify($"{split.Count}건을 추가했어요. 이미 가져온 건은 건너뛰었어요.");
        }
        catch (Exception e) { Notify(e.Message, true); }
    }
    private void AddTransfer()
    {
        if (transfers.Count >= 100) { Notify("한 파일에 100건까지 가능해요.", true); return; }
        transfers.Add(new JsonObject { ["id"] = Guid.NewGuid().ToString("N"), ["bank"] = "", ["account"] = "", ["amount"] = "", ["plate"] = "", ["outMemo"] = "", ["inMemo"] = "", ["cms"] = "", ["source"] = "" });
        RenderTransfers();
    }
    private void SplitTransfers()
    {
        var parts = rules.Split(transfers);
        if (parts.Count > 100) { Notify("금액을 나누면 100건을 넘어요. 다른 줄을 줄여 주세요.", true); return; }
        if (!JsonNode.DeepEquals(transfers, parts)) { transfers = parts; RenderTransfers(); }
    }
    private string Signature => transfers.ToJsonString();
    private void RenderTransfers()
    {
        refreshing = true;
        transferList.Children.Clear(); transferStatuses.Clear(); transferCards.Clear(); transferChecks.Clear(); groupedEditors.Clear();
        foreach (var row in transfers.ToArray())
        {
            var bank = new ComboBox { Header = "입금은행", ItemsSource = rules.Banks, SelectedItem = row.Text("bank"), Width = TransferWidth("bank") };
            bank.SelectionChanged += (_, _) => UpdateTransferField(row!, "bank", bank.SelectedItem?.ToString() ?? "");
            groupedEditors[(row.Text("id"), "bank")] = value => bank.SelectedItem = value;
            var account = Edit(row!, "account", "입금계좌번호", TransferWidth("account"));
            var amount = Edit(row!, "amount", "입금액(원)", TransferWidth("amount"));
            amount.TextAlignment = TextAlignment.Right;
            var plate = Edit(row!, "plate", "차량번호", TransferWidth("plate"));
            var main = WrapActions(bank, account, amount, plate);
            var id = row.Text("id");
            var pick = new CheckBox { Content = $"{transferStatuses.Count + 1}번 선택", IsChecked = selectedTransfers.Contains(id) };
            pick.Checked += (_, _) => selectedTransfers.Add(id);
            pick.Unchecked += (_, _) => selectedTransfers.Remove(id);
            var remove = Button("삭제", () => { selectedTransfers.Remove(id); transfers.Remove(row); RenderTransfers(); });
            var memo = WrapActions(Edit(row!, "outMemo", "출금통장 표시", TransferWidth("outMemo")),
                Edit(row!, "inMemo", "입금통장 표시", TransferWidth("inMemo")), Edit(row!, "cms", "CMS코드", TransferWidth("cms")));
            var status = Text("");
            var statusTag = StatusTag(status);
            transferStatuses.Add((status, statusTag));
            var original = new TextBox { Header = "Slack 원문", Text = row.Text("ctx").Length > 0 ? row.Text("ctx") : row.Text("source"), IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MaxHeight = 220 };
            var details = new Expander { Header = "예금주·메모·CMS 및 원문", Content = Stack(Edit(row!, "holder", "예금주", TransferWidth("holder")),
                memo, original), HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch };
            pick.VerticalAlignment = remove.VerticalAlignment = status.VerticalAlignment = VerticalAlignment.Center;
            var cardContent = Stack(main, WrapControls(center: true, pick, statusTag, remove), details);
            cardContent.Spacing = 10;
            var card = Card(cardContent);
            transferChecks.Add(pick);
            transferCards.Add(card);
            transferList.Children.Add(card);
        }
        refreshing = false; ValidateTransfers();
    }
    private TextBox Edit(JsonNode row, string key, string label, double width)
    {
        var box = Input(label, "", width); box.Text = row.Text(key);
        groupedEditors[(row.Text("id"), key)] = value => box.Text = value;
        box.TextChanged += (_, _) =>
        {
            if (!refreshing) UpdateTransferField(row, key, NormalizeTransferValue(key, box.Text));
        };
        box.LostFocus += (_, _) => CommitTransferEdit(row, key, box);
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
            var (status, tag) = transferStatuses[i];
            transferCards[i].Visibility = ShowTransfer(transfers[i], checks[i].Text("state")) ? Visibility.Visible : Visibility.Collapsed;
            status.Text = string.Join(" · ", checks[i]!["errors"]!.AsArray().Concat(checks[i]!["reviews"]!.AsArray()).Select(x => x!.ToString()));
            if (status.Text.Length == 0) status.Text = "확인 완료";
            TintTag(tag, status, checks[i].Text("state") switch
            {
                "bad" => errorTone,
                "warn" => attentionTone,
                _ => successTone
            });
        }
        download.IsEnabled = !saving && transfers.Count is > 0 and <= 100 && bad == 0 && (warn == 0 || acknowledge.IsChecked == true);
        if (step == 3) UpdatePreview();
    }
    private void Step(int n)
    {
        step = n;
        for (var i = 0; i < transferSteps.Length; i++)
        {
            var button = transferSteps[i];
            if (i + 1 == n)
            {
                button.Background = transferTone.Fill;
                button.Foreground = transferTone.Ink;
                button.BorderBrush = transferTone.Accent;
            }
            else
            {
                button.ClearValue(Control.BackgroundProperty);
                button.ClearValue(Control.ForegroundProperty);
                button.ClearValue(Control.BorderBrushProperty);
            }
        }
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
            var fileName = rules.TransferFileName(transfers, DateTime.Now.ToString("yyyyMMdd_HHmm"));
            var file = await SavePicker(Path.GetFileNameWithoutExtension(fileName), ".xlsx");
            if (file is null) return;
            await Task.Run(() => Spreadsheets.Write(file.Path, values, "다계좌이체"));
            savedSignature = signature; savedThread = ThreadSignature; Notify("이체파일을 저장했어요. 은행 업로드 전에 계좌와 금액을 확인해 주세요.");
        }
        finally { saving = false; ValidateTransfers(); }
    }
    private async Task ResetTransfers()
    {
        if (await Dialog("전체 초기화", Text("붙여넣은 스레드와 이체 내역을 모두 지울까요?"), "모두 지우기").ShowAsync() != ContentDialogResult.Primary) return;
        transfers.Clear(); imported.Clear(); selectedTransfers.Clear(); threadBoxes.Clear(); threadInputs.Children.Clear();
        thread = AddThreadInput(first: true);
        savedSignature = ""; RenderTransfers(); Step(1);
    }
    private ContentDialog Dialog(string title, UIElement content, string primary) => new() { XamlRoot = Root.XamlRoot, Title = title, Content = content, PrimaryButtonText = primary, CloseButtonText = "취소", DefaultButton = ContentDialogButton.Close, RequestedTheme = Root.ActualTheme };
    private async void Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (allowClose || (transfers.Count == 0 && threadBoxes.All(box => string.IsNullOrWhiteSpace(box.Text)))
            || (transfers.Count > 0 && savedSignature == Signature && savedThread == ThreadSignature)) return;
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

    private static string ThemeFile => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WorkTool", "theme.txt");

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
