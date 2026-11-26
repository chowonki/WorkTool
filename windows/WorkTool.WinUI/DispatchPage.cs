using System.Globalization;
using System.Text.Json.Nodes;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;
using Windows.Storage.Pickers;
using WinRT.Interop;
using WorkTool.Core;

namespace WorkTool.WinUI;

public sealed partial class MainWindow
{
    private static readonly double[] DefaultDispatchColumns = [134, 0, 102, 106, 96, 145, 90, 48, 64];
    private static readonly string[] DispatchHeadings = ["차량번호", "탁송일정", "주유·충전비", "탁송비", "부가세", "조회 상태", "업체", "완료", "상세"];
    private readonly double[] dispatchColumns = (double[])DefaultDispatchColumns.Clone();
    private readonly JsonArray files = new();
    private JsonArray results = new();
    private readonly HashSet<string> selected = new();
    private readonly HashSet<string> completed = new();
    private readonly HashSet<string> copiedDispatch = new();
    private TextBox vehicles = null!, search = null!;
    private TextBlock fileSummary = null!, dispatchSummary = null!, vehicleCount = null!;
    private StackPanel dispatchList = null!;
    private ComboBox filter = null!, scope = null!;
    private Grid dispatchTable = null!;
    private Button previousResults = null!, nextResults = null!;
    private StackPanel dispatchPageNumbers = null!;
    private ComboBox dispatchPageSizeBox = null!;
    private ScrollViewer dispatchScroll = null!;
    private int dispatchPage;
    // 한 페이지에 보여줄 건수: 웹과 같이 기본 30건, 고른 값은 이 기기에 기억해요.
    private static readonly int[] DispatchPageSizes = [10, 30, 50, 100];
    private int dispatchPageSize = 30;
    private static string DispatchPageSizeFile => LocalState.DataFile("dispatch-page-size.txt");
    private int visibleCount;
    private decimal visibleAmount;
    private enum DispatchStage { Input, Review, Slack, Save }
    private DispatchStage dispatchStage;
    private Button[] dispatchSteps = [];
    private FrameworkElement dispatchInputStage = null!, dispatchResultsStage = null!,
        dispatchSaveActions = null!, dispatchReviewNext = null!;

    private FrameworkElement DispatchPage()
    {
        vehicles = Input("차량목록", "2026-11-25 10:00  123가4567\n45나6789");
        vehicles.AcceptsReturn = true; vehicles.Height = 100;
        LoadWorkMarks();
        vehicleCount = Text("입력 차량 0대", 13);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetLiveSetting(vehicleCount, Microsoft.UI.Xaml.Automation.Peers.AutomationLiveSetting.Polite);
        vehicles.TextChanged += (_, _) => { UpdateVehicleCount(); InvalidateDispatch(); };
        fileSummary = Text("업체 엑셀 파일을 첨부해 주세요. .xlsx / .xls · 암호 파일 지원");
        dispatchSummary = Text("차량목록과 업체 파일을 넣고 조회하세요.");
        dispatchList = new StackPanel();
        filter = new ComboBox { Header = "조회 상태", Width = 180 };
        foreach (var (label, key) in new[] { ("전체", "all"), ("확인 필요", "review"), ("금액 확인", "confirmed"), ("진행 중", "progress"), ("미조회", "missing"), ("제주 출발", "jeju"), ("캐리어탁송", "carrier"), ("슬랙봇 문의", "slack"), ("슬랙봇 반영", "slack-replies") })
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
        previousResults = Button("‹ 이전", () => ChangeDispatchPage(dispatchPage - 1));
        nextResults = Button("다음 ›", () => ChangeDispatchPage(dispatchPage + 1));
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(previousResults, "이전 페이지");
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(nextResults, "다음 페이지");
        dispatchPageNumbers = Bar();
        dispatchPageNumbers.Spacing = 4;
        LoadDispatchPageSize();
        dispatchPageSizeBox = new ComboBox { Width = 104, VerticalAlignment = VerticalAlignment.Center };
        foreach (var size in DispatchPageSizes) dispatchPageSizeBox.Items.Add(new ComboBoxItem { Content = $"{size}건", Tag = size });
        dispatchPageSizeBox.SelectedIndex = Array.IndexOf(DispatchPageSizes, dispatchPageSize);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(dispatchPageSizeBox, "한 페이지에 표시할 건수");
        dispatchPageSizeBox.SelectionChanged += (_, _) =>
        {
            if (dispatchPageSizeBox.SelectedItem is ComboBoxItem { Tag: int size }) SetDispatchPageSize(size);
        };
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
            })));
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
        dispatchInputStage = VerticalScroll(Card(Stack(inputs,
            WrapControls(center: true, Button("차량 조회하기", QueryDispatch, true),
                Text("차량목록과 업체 Excel 파일을 넣어 주세요.")))));
        var selectionActions = WrapActions(
            Button("표시 차량 선택", () =>
            {
                foreach (var row in VisibleResults()) selected.Add(row.Text("id"));
                RenderDispatch();
            }),
            Button("선택 해제", () => { selected.Clear(); RenderDispatch(); }));
        dispatchReviewNext = Button("다음: 슬랙봇 확인", () => ShowDispatchStage(DispatchStage.Slack), true);
        selectionActions.Children.Add(dispatchReviewNext);
        dispatchSaveActions = WrapActions(scope, Button("번호 복사", CopyDispatchNumbers),
            AsyncButton("캡처 복사", () => CaptureDispatch(true)),
            new DropDownButton { Content = "파일 저장", Flyout = export });
        dispatchSteps =
        [
            Button("1. 자료 입력", () => ShowDispatchStage(DispatchStage.Input)),
            Button("2. 결과 확인", () => ShowDispatchStage(DispatchStage.Review)),
            Button("3. 슬랙봇 확인", () => ShowDispatchStage(DispatchStage.Slack)),
            Button("4. 복사·저장", () => ShowDispatchStage(DispatchStage.Save))
        ];
        var top = Stack(PageHeading("탁송체크", "\uE804", dispatchTone, AsyncButton("전체 초기화", ResetDispatch)),
            WrapActions(dispatchSteps));
        top.Spacing = 9;
        grid.Children.Add(top);

        var resultsStage = new Grid { RowSpacing = 10 };
        resultsStage.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        resultsStage.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        resultsStage.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var resultControls = Stack(WrapActions(filter, search, AsyncButton("필터·정렬", EditDispatchFilters),
            AsyncButton("열 너비", EditDispatchWidths)), selectionActions, dispatchSaveActions);
        resultControls.Spacing = 8;
        resultsStage.Children.Add(resultControls);
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
        resultsStage.Children.Add(list);
        dispatchSummary.VerticalAlignment = VerticalAlignment.Center;
        var pageSizeLabel = Text("페이지당", 13);
        pageSizeLabel.VerticalAlignment = VerticalAlignment.Center;
        var pagination = WrapControls(center: true, previousResults, dispatchPageNumbers, nextResults, pageSizeLabel, dispatchPageSizeBox);
        pagination.HorizontalAlignment = HorizontalAlignment.Right;
        var footer = SplitLayout(dispatchSummary, pagination, 720);
        Grid.SetRow(footer, 2);
        resultsStage.Children.Add(footer);
        dispatchResultsStage = resultsStage;
        Grid.SetRow(dispatchInputStage, 1);
        Grid.SetRow(dispatchResultsStage, 1);
        grid.Children.Add(dispatchInputStage);
        grid.Children.Add(dispatchResultsStage);
        slackDispatchStage = CreateSlackDispatchStage();
        Grid.SetRow(slackDispatchStage, 1);
        grid.Children.Add(slackDispatchStage);
        RenderDispatch();
        ShowDispatchStage(DispatchStage.Input);
        return grid;
    }

    private void ShowDispatchStage(DispatchStage stage)
    {
        if (stage != DispatchStage.Input && results.Count == 0) return;
        dispatchStage = stage;
        SetActiveStep(dispatchSteps, (int)stage, dispatchTone);
        dispatchSteps[(int)DispatchStage.Review].IsEnabled = results.Count > 0;
        dispatchSteps[(int)DispatchStage.Slack].IsEnabled = results.Count > 0;
        dispatchSteps[(int)DispatchStage.Save].IsEnabled = results.Count > 0;
        dispatchInputStage.Visibility = stage == DispatchStage.Input ? Visibility.Visible : Visibility.Collapsed;
        dispatchResultsStage.Visibility = stage is DispatchStage.Review or DispatchStage.Save ? Visibility.Visible : Visibility.Collapsed;
        slackDispatchStage.Visibility = stage == DispatchStage.Slack ? Visibility.Visible : Visibility.Collapsed;
        dispatchReviewNext.Visibility = stage == DispatchStage.Review ? Visibility.Visible : Visibility.Collapsed;
        dispatchSaveActions.Visibility = stage == DispatchStage.Save ? Visibility.Visible : Visibility.Collapsed;
        if (stage == DispatchStage.Slack) RefreshSlackDispatchList();
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
                    var warnings = LocalState.Strings(parsed["warnings"]).ToArray();
                    if (warnings.Length > 0)
                    {
                        var body = Stack(Text("파일은 첨부했지만 일부 열의 값이 평소와 달라요. 업체 사이트에서 받은 원본 시트인지, 열이 추가·이동되지 않았는지 확인한 뒤 조회해 주세요."),
                            Text(string.Join(Environment.NewLine, warnings), 13));
                        var dialog = Dialog("업체 양식이 바뀌었을 수 있어요", body, "확인");
                        dialog.CloseButtonText = "";
                        dialog.DefaultButton = ContentDialogButton.Primary;
                        await dialog.ShowAsync();
                    }
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
            ClearSlackReplies();
            selected.Clear(); dispatchPage = 0; RenderDispatch();
            ShowDispatchStage(DispatchStage.Review);
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
        dispatchPage = Math.Clamp(dispatchPage, 0, Math.Max(0, (visible.Count - 1) / dispatchPageSize));
        visibleCount = visible.Count;
        visibleAmount = Sum(visible, "amount");
        UpdateDispatchSummary();
        RenderDispatchPager();
        foreach (var row in visible.Skip(dispatchPage * dispatchPageSize).Take(dispatchPageSize).OfType<JsonObject>())
        {
            var id = row.Text("id");
            var plate = row.Text("plate");
            var check = new CheckBox { IsChecked = selected.Contains(id), MinWidth = 0, Padding = new Thickness(0) };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(check, plate + " 행 선택");
            check.Checked += (_, _) => { selected.Add(id); UpdateDispatchSummary(); };
            check.Unchecked += (_, _) => { selected.Remove(id); UpdateDispatchSummary(); };
            var done = new CheckBox { IsChecked = completed.Contains(plate), MinWidth = 0, Padding = new Thickness(0),
                HorizontalAlignment = HorizontalAlignment.Center };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(done, row.Text("plate") + " 완료");
            var workStatus = Text("", 11);
            workStatus.MinHeight = 14;
            void UpdateWorkStatus()
            {
                workStatus.Text = completed.Contains(plate) ? (copiedDispatch.Contains(plate) ? "완료 · 복사함" : "완료")
                    : copiedDispatch.Contains(plate) ? "복사함" : "";
                workStatus.Foreground = ThemeBrush(completed.Contains(plate) ? "SystemFillColorSuccessBrush" : "AccentTextFillColorPrimaryBrush");
            }
            done.Checked += (_, _) => { completed.Add(plate); SaveWorkMarks(); UpdateWorkStatus(); };
            done.Unchecked += (_, _) => { completed.Remove(plate); SaveWorkMarks(); UpdateWorkStatus(); };
            var content = Stack(Text(string.Join(" · ", new[] { row.Text("rawState"), row.Text("carrierType"), row.Text("origin") }.Where(value => value.Length > 0))));
            foreach (var (field, label) in new[] { ("schedule", "탁송일정"), ("fee", "탁송비용") })
            {
                var candidates = row![field + "Candidates"]!.AsArray();
                if (!row.Flag(field + "Conflict") && candidates.Select(candidate => candidate.Text("value")).Distinct().Take(2).Count() < 2) continue;
                // 일정이 중첩이면 비용은 고른 일정의 기록을 따라가서 따로 고르지 않아요.
                if (field == "fee" && row["scheduleCandidates"]!.AsArray().Select(candidate => candidate.Text("value")).Distinct().Take(2).Count() > 1) continue;
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
            TintTag(tag, statusText, row!.Flag("slackPending") ? attentionTone : DispatchStatusTone(row.Text("status")));
            var status = Stack(tag);
            status.Spacing = 3;
            if (row.Flag("slackReviewed"))
            {
                var source = Text("슬랙봇 회신", 12);
                source.Foreground = dispatchTone.Ink;
                status.Children.Add(source);
            }
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
            void MarkCopied() { copiedDispatch.Add(plate); SaveWorkMarks(); UpdateWorkStatus(); }
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
                CopyCell("주유·충전비", row!.Flag("slackPending") ? Rules.SlackPending : row.Text("amount"), Money(row, "amount"), right: true, onCopy: MarkCopied),
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

    private int DispatchPageCount => Math.Max(1, (visibleCount + dispatchPageSize - 1) / dispatchPageSize);

    private void ChangeDispatchPage(int page)
    {
        dispatchPage = page;
        RenderDispatch();
        dispatchScroll.ChangeView(null, 0, null);
    }

    // 페이지 번호: 현재 페이지 주변 7개와 첫·마지막 페이지를 보여줘요.
    private void RenderDispatchPager()
    {
        var pages = DispatchPageCount;
        previousResults.IsEnabled = dispatchPage > 0;
        nextResults.IsEnabled = dispatchPage + 1 < pages;
        dispatchPageNumbers.Children.Clear();
        if (pages <= 1) return;
        void Number(int page)
        {
            var button = Button((page + 1).ToString(CultureInfo.InvariantCulture), () => ChangeDispatchPage(page), page == dispatchPage);
            button.MinWidth = 36;
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(button, $"{page + 1}페이지");
            dispatchPageNumbers.Children.Add(button);
        }
        void Gap()
        {
            var text = Text("…");
            text.VerticalAlignment = VerticalAlignment.Center;
            dispatchPageNumbers.Children.Add(text);
        }
        var from = Math.Max(0, dispatchPage - 3);
        var to = Math.Min(pages - 1, from + 6);
        from = Math.Max(0, to - 6);
        if (from > 0) { Number(0); if (from > 1) Gap(); }
        for (var page = from; page <= to; page++) Number(page);
        if (to < pages - 1) { if (to < pages - 2) Gap(); Number(pages - 1); }
    }

    private void SetDispatchPageSize(int size)
    {
        if (!DispatchPageSizes.Contains(size) || size == dispatchPageSize) return;
        var firstRow = dispatchPage * dispatchPageSize;
        dispatchPageSize = size;
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(DispatchPageSizeFile)!);
            File.WriteAllText(DispatchPageSizeFile, size.ToString(CultureInfo.InvariantCulture));
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
        ChangeDispatchPage(firstRow / size);
    }

    private void LoadDispatchPageSize()
    {
        try
        {
            if (File.Exists(DispatchPageSizeFile) && int.TryParse(File.ReadAllText(DispatchPageSizeFile).Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var size)
                && DispatchPageSizes.Contains(size)) dispatchPageSize = size;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
    }

    private void UpdateDispatchSummary()
    {
        var first = visibleCount == 0 ? 0 : dispatchPage * dispatchPageSize + 1;
        var last = Math.Min(visibleCount, (dispatchPage + 1) * dispatchPageSize);
        dispatchSummary.Text = $"전체 {results.Count:N0}대 · 표시 {visibleCount:N0}대 · 선택 {selected.Count:N0}대 · 주유·충전비 {visibleAmount:N0}원 · {first:N0}–{last:N0}줄 ({dispatchPage + 1}/{DispatchPageCount}페이지)";
    }
    private static string Money(JsonNode? row, string field)
    {
        if (field == "amount" && row?.Flag("slackPending") == true) return Rules.SlackPending;
        return decimal.TryParse(row.Text(field), NumberStyles.Any, CultureInfo.InvariantCulture, out var number) ? $"{number:N0}원" : "—";
    }
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
}
