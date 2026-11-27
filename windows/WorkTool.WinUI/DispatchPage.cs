using System.Globalization;
using System.Text.Json.Nodes;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Ellipse = Microsoft.UI.Xaml.Shapes.Ellipse;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;
using Windows.Storage.Pickers;
using Windows.System;
using WinRT.Interop;
using WorkTool.Core;

namespace WorkTool.WinUI;

// 탁송체크: (탐색 창의 보기 필터) | 표 | 오른쪽 입력 패널.
// 예전 단계 단추(자료 입력·결과 확인·슬랙봇 확인·복사·저장)와 통계 카드는 없애고,
// 보기 필터는 탐색 창 하위 항목, 슬랙봇 회신은 오른쪽 패널, 복사·저장은 CommandBar로 옮겼어요.
public sealed partial class MainWindow
{
    private static readonly double[] DefaultDispatchColumns = [134, 0, 102, 106, 96, 160, 90, 48, 64];
    private static readonly string[] DispatchHeadings = ["차량번호", "탁송일정", "주유·충전비", "탁송비", "부가세", "조회 상태", "업체", "완료", "상세"];
    private readonly double[] dispatchColumns = (double[])DefaultDispatchColumns.Clone();
    private readonly JsonArray files = new();
    private JsonArray results = new();
    private readonly HashSet<string> selected = new();
    private readonly HashSet<string> completed = new();
    private readonly HashSet<string> copiedDispatch = new();
    private readonly Dictionary<string, int> dispatchCounts = new();
    private TextBox vehicles = null!, search = null!;
    private TextBlock fileSummary = null!, dispatchSummary = null!, vehicleCount = null!, dispatchSubtitle = null!, dispatchCountText = null!;
    private StackPanel dispatchList = null!, dispatchEmpty = null!;
    private TextBlock dispatchEmptyTitle = null!, dispatchEmptyText = null!;
    private ComboBox scope = null!, vendorBox = null!, stateBox = null!;
    private Grid dispatchTable = null!;
    private Button previousResults = null!, nextResults = null!;
    private StackPanel dispatchPageNumbers = null!;
    private ComboBox dispatchPageSizeBox = null!;
    private ScrollViewer dispatchScroll = null!;
    private Border dispatchPanel = null!;
    private FrameworkElement dispatchInputPanel = null!;
    private AppBarButton dispatchPanelToggle = null!;
    private string dispatchFilter = "all";
    private bool syncingDispatchBoxes;
    private int dispatchPage;
    // 한 페이지에 보여줄 건수: 웹과 같이 기본 30건, 고른 값은 이 기기에 기억해요.
    private static readonly int[] DispatchPageSizes = [10, 30, 50, 100];
    private int dispatchPageSize = 30;
    private static string DispatchPageSizeFile => LocalState.DataFile("dispatch-page-size.txt");
    private int visibleCount, visibleCars;
    private decimal visibleAmount;

    private FrameworkElement DispatchPage()
    {
        LoadWorkMarks();
        LoadDispatchPageSize();
        LoadDispatchWidths();

        // ── 오른쪽 입력 패널 ──
        vehicles = new TextBox
        {
            PlaceholderText = "2026-11-25 10:00  123가4567\n45나6789\n\n차량번호만 입력해도 돼요.", AcceptsReturn = true, TextWrapping = TextWrapping.NoWrap,
            FontFamily = (Microsoft.UI.Xaml.Media.FontFamily)Application.Current.Resources["MonoFont"], FontSize = 13, MinHeight = 160,
            VerticalAlignment = VerticalAlignment.Stretch, IsSpellCheckEnabled = false
        };
        ScrollViewer.SetVerticalScrollBarVisibility(vehicles, ScrollBarVisibility.Auto);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(vehicles, "차량목록");
        vehicleCount = Muted("0대 인식");
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetLiveSetting(vehicleCount, Microsoft.UI.Xaml.Automation.Peers.AutomationLiveSetting.Polite);
        vehicles.TextChanged += (_, _) => OnVehiclesChanged();
        // 여러 줄 입력칸이 Ctrl+Enter를 줄바꿈으로 먹으면 조회 뒤 결과가 지워져요. 여기서 먼저 처리해요.
        vehicles.PreviewKeyDown += (_, args) =>
        {
            if (args.Key != VirtualKey.Enter) return;
            var ctrl = Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Control).HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);
            if (!ctrl) return;
            args.Handled = true;
            QueryDispatch();
        };
        fileSummary = Muted("여기에 .xlsx 파일을 끌어다 놓으세요");
        fileSummary.HorizontalAlignment = HorizontalAlignment.Center;
        fileSummary.Margin = new Thickness(0, 8, 0, 8);
        dispatchFiles = new StackPanel();
        var clean = Button("정리", CleanVehicles);
        ToolTipService.SetToolTip(clean, "빈 줄·공백 정리");
        var pick = new HyperlinkButton { Content = "파일 선택" };
        pick.Click += async (_, _) => { try { await ImportFiles(); } catch (Exception e) { Notify(e.Message, true); } };
        var clearFiles = new HyperlinkButton { Content = "모두 빼기" };
        clearFiles.Click += (_, _) => { if (importingDispatch) return; files.Clear(); RenderDispatchFiles(); InvalidateDispatch(); };
        var run = Button("차량 조회하기", QueryDispatch, true);
        var shortcut = Text("Ctrl+Enter", 12); shortcut.Opacity = 0.75; shortcut.VerticalAlignment = VerticalAlignment.Center;
        run.Content = Bar(Text("차량 조회하기"), shortcut);
        ((StackPanel)run.Content).Spacing = 12;
        run.HorizontalAlignment = HorizontalAlignment.Stretch;
        run.Padding = new Thickness(12, 8, 12, 8);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(run, "차량 조회하기");

        var input = new Grid { Padding = new Thickness(20, 20, 20, 0), RowSpacing = 6 };
        foreach (var h in new[] { GridLength.Auto, GridLength.Auto, new GridLength(1, GridUnitType.Star), GridLength.Auto, GridLength.Auto, GridLength.Auto, GridLength.Auto })
            input.RowDefinitions.Add(new RowDefinition { Height = h, MinHeight = h.IsStar ? 160 : 0 });
        var title = ColorHeading("입력", accent, "\uE70F", 20, 22); title.Margin = new Thickness(0, 0, 0, 8);
        input.Children.Add(title);
        var listHead = Columns([0, 0], ColorHeading("차량목록", accent, "\uE804"), clean); listHead.Padding = new Thickness(0);
        clean.HorizontalAlignment = HorizontalAlignment.Right;
        Place(input, listHead, 1);
        Place(input, vehicles, 2);
        Place(input, vehicleCount, 3);
        var sep = Divider(); sep.Margin = new Thickness(-20, 8, -20, 8);
        Place(input, sep, 4);
        var filesHead = new Grid();
        filesHead.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        filesHead.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var filesTitle = ColorHeading("업체 파일", blue, "\uE8B7"); filesTitle.VerticalAlignment = VerticalAlignment.Center;
        filesHead.Children.Add(filesTitle);
        var fileLinks = Bar(pick, clearFiles); fileLinks.Spacing = 4;
        Place(filesHead, fileLinks, 0, 1);
        Place(input, filesHead, 5);
        var fileArea = Stack(new ScrollViewer { Content = dispatchFiles, MaxHeight = 220, VerticalScrollBarVisibility = ScrollBarVisibility.Auto }, fileSummary);
        fileArea.Spacing = 0;
        Place(input, fileArea, 6);
        var inputBody = new Grid();
        inputBody.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        inputBody.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        inputBody.Children.Add(input);
        var runBar = new Border { Child = run, Padding = new Thickness(20, 12, 20, 20), BorderBrush = divider, BorderThickness = new Thickness(0, 1, 0, 0) };
        Place(inputBody, runBar, 1);
        dispatchInputPanel = inputBody;
        slackDispatchStage = CreateSlackDispatchStage();
        slackDispatchStage.Visibility = Visibility.Collapsed;
        var panelHost = new Grid { AllowDrop = true, Background = transparent };
        panelHost.Children.Add(dispatchInputPanel);
        panelHost.Children.Add(slackDispatchStage);
        // 패널 전체가 파일 드롭 영역이에요.
        panelHost.DragOver += (_, args) =>
        {
            if (importingDispatch || !args.DataView.Contains(StandardDataFormats.StorageItems)) return;
            args.AcceptedOperation = DataPackageOperation.Copy;
            args.DragUIOverride.Caption = "업체 파일 첨부";
            panelHost.Background = dropHighlight;
        };
        panelHost.DragLeave += (_, _) => panelHost.Background = transparent;
        panelHost.Drop += async (_, args) =>
        {
            panelHost.Background = transparent;
            var deferral = args.GetDeferral();
            try
            {
                if (args.DataView.Contains(StandardDataFormats.StorageItems))
                {
                    ShowSlackPanel(false);
                    await AddDispatchFiles((await args.DataView.GetStorageItemsAsync()).OfType<StorageFile>());
                }
            }
            catch (Exception error) { Notify(error.Message, true); }
            finally { deferral.Complete(); }
        };
        dispatchPanel = SidePanel(panelHost);
        RenderDispatchFiles();

        // ── CommandBar ──
        MenuFlyout Menu(params (string Title, Func<Task> Action)[] items)
        {
            var menu = new MenuFlyout();
            foreach (var (text, action) in items)
            {
                var item = new MenuFlyoutItem { Text = text };
                item.Click += async (_, _) => { try { await action(); } catch (Exception e) { Notify(e.Message, true); } };
                menu.Items.Add(item);
            }
            return menu;
        }
        var capture = new AppBarButton { Label = "캡처", Icon = new FontIcon { Glyph = "\uE722", Foreground = indigo.Solid },
            Flyout = Menu(("클립보드에 복사", () => CaptureDispatch(true)), ("PNG 저장", () => CaptureDispatch(false))) };
        var save = new AppBarButton { Label = "저장", Icon = new FontIcon { Glyph = "\uE74E", Foreground = blue.Solid },
            Flyout = Menu(("Excel 저장", () => ExportDispatch(".xlsx")), ("PDF 저장", () => CaptureDispatch(false, true)), ("TXT 저장", () => ExportDispatch(".txt"))) };
        var commands = new CommandBar
        {
            DefaultLabelPosition = CommandBarDefaultLabelPosition.Right, Background = transparent, HorizontalAlignment = HorizontalAlignment.Left,
            IsDynamicOverflowEnabled = true, VerticalAlignment = VerticalAlignment.Center
        };
        commands.PrimaryCommands.Add(Command("차량번호 복사", "\uE8C8", CopyDispatchNumbers, teal));
        commands.PrimaryCommands.Add(capture);
        commands.PrimaryCommands.Add(save);
        commands.PrimaryCommands.Add(Command("슬랙봇 회신", "\uE8BD", () => ShowSlackPanel(true), purple));
        commands.SecondaryCommands.Add(Command("보이는 차량 모두 체크", "\uE762", () =>
        {
            foreach (var row in VisibleResults()) selected.Add(row.Text("id"));
            RenderDispatch();
        }));
        commands.SecondaryCommands.Add(Command("체크 해제", "\uE8E6", () => { selected.Clear(); RenderDispatch(); }));
        commands.SecondaryCommands.Add(new AppBarSeparator());
        commands.SecondaryCommands.Add(Command("필터·정렬", "\uE71C", EditDispatchFilters));
        commands.SecondaryCommands.Add(Command("열 너비", "\uE9A6", EditDispatchWidths));
        commands.SecondaryCommands.Add(new AppBarSeparator());
        commands.SecondaryCommands.Add(Command("전체 초기화", "\uE74D", ResetDispatch, red));

        scope = new ComboBox { Width = 160, ItemsSource = new[] { "현재 표시 목록", "전체 조회 결과", "금액 확인 차량" }, SelectedIndex = 0, VerticalAlignment = VerticalAlignment.Center };
        ToolTipService.SetToolTip(scope, "복사·캡처·저장 범위 (체크한 차량이 있으면 그 차량만)");
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(scope, "저장 범위");
        search = new TextBox { PlaceholderText = "차량번호 검색", Width = 200, VerticalAlignment = VerticalAlignment.Center, Padding = new Thickness(10, 5, 32, 6) };
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(search, "차량번호 검색");
        search.TextChanged += (_, _) => { dispatchPage = 0; RenderDispatch(); };
        var searchBox = new Grid { VerticalAlignment = VerticalAlignment.Center };
        searchBox.Children.Add(search);
        searchBox.Children.Add(new FontIcon { Glyph = "\uE721", FontSize = 14, Foreground = mutedInk, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 0, 11, 0), IsHitTestVisible = false });
        dispatchPanelToggle = PanelToggle("입력 패널", () => dispatchPanel);
        var right = Bar(scope, searchBox, dispatchPanelToggle);
        right.VerticalAlignment = VerticalAlignment.Center;
        var commandRow = new Grid { Margin = new Thickness(16, 0, 16, 0) };
        commandRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        commandRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        commandRow.Children.Add(commands);
        Place(commandRow, right, 0, 1);

        // ── 업체 / 상태 얇은 줄 ──
        vendorBox = new ComboBox { MinWidth = 140, PlaceholderText = "전체 업체", VerticalAlignment = VerticalAlignment.Center };
        stateBox = new ComboBox { MinWidth = 150, PlaceholderText = "전체 상태", VerticalAlignment = VerticalAlignment.Center };
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(vendorBox, "업체 필터");
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(stateBox, "탁송 상태 필터");
        vendorBox.SelectionChanged += (_, _) => { if (syncingDispatchBoxes) return; dispatchVendor = vendorBox.SelectedIndex > 0 ? (string)vendorBox.SelectedItem : ""; dispatchPage = 0; RenderDispatch(); };
        stateBox.SelectionChanged += (_, _) => { if (syncingDispatchBoxes) return; dispatchState = stateBox.SelectedIndex > 0 ? (string)stateBox.SelectedItem : ""; dispatchPage = 0; RenderDispatch(); };
        var vendorLabel = Muted("업체", 12); vendorLabel.VerticalAlignment = VerticalAlignment.Center;
        var stateLabel = Muted("상태", 12); stateLabel.VerticalAlignment = VerticalAlignment.Center; stateLabel.Margin = new Thickness(8, 0, 0, 0);
        var filterLine = Bar(vendorLabel, vendorBox, stateLabel, stateBox);
        filterLine.Margin = new Thickness(24, 4, 24, 10);

        // ── 표 ──
        dispatchList = new StackPanel();
        dispatchTable = new Grid();
        dispatchTable.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        dispatchTable.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        dispatchScroll = new ScrollViewer { Content = dispatchList, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollMode = ScrollMode.Enabled };
        Grid.SetRow(dispatchScroll, 1);
        dispatchTable.Children.Add(dispatchScroll);
        UpdateDispatchHeader();
        var table = new Grid { BorderBrush = divider, BorderThickness = new Thickness(0, 1, 0, 0) };
        var list = Horizontal(dispatchTable);
        list.Padding = new Thickness(12, 0, 0, 0);
        table.Children.Add(list);
        dispatchEmptyTitle = Text("조회할 차량목록을 준비해 주세요", 16);
        dispatchEmptyTitle.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
        dispatchEmptyTitle.HorizontalAlignment = HorizontalAlignment.Center;
        dispatchEmptyText = Muted("오른쪽 입력 패널에 차량목록을 넣고 업체 파일을 끌어다 놓으면\n완료 차량의 금액과 확인할 목록을 정리해 드려요.", 13);
        dispatchEmptyText.TextAlignment = TextAlignment.Center;
        var emptyIcon = new FontIcon { Glyph = "\uE804", FontSize = 36, Foreground = faintInk };
        dispatchEmpty = Stack(emptyIcon, dispatchEmptyTitle, dispatchEmptyText);
        dispatchEmpty.Spacing = 8;
        dispatchEmpty.HorizontalAlignment = HorizontalAlignment.Center;
        dispatchEmpty.VerticalAlignment = VerticalAlignment.Center;
        dispatchEmpty.IsHitTestVisible = false;
        table.Children.Add(dispatchEmpty);

        // ── 상태 줄: 왼쪽 메시지, 오른쪽 "n줄 · n대"와 페이지 ──
        dispatchSummary = Muted("");
        dispatchSummary.VerticalAlignment = VerticalAlignment.Center;
        dispatchCountText = Muted("");
        dispatchCountText.VerticalAlignment = VerticalAlignment.Center;
        previousResults = new Button { Content = new FontIcon { Glyph = "\uE76B", FontSize = 12 }, Padding = new Thickness(8, 5, 8, 5) };
        nextResults = new Button { Content = new FontIcon { Glyph = "\uE76C", FontSize = 12 }, Padding = new Thickness(8, 5, 8, 5) };
        previousResults.Click += (_, _) => ChangeDispatchPage(dispatchPage - 1);
        nextResults.Click += (_, _) => ChangeDispatchPage(dispatchPage + 1);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(previousResults, "이전 페이지");
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(nextResults, "다음 페이지");
        dispatchPageNumbers = Bar();
        dispatchPageNumbers.Spacing = 2;
        dispatchPageSizeBox = new ComboBox { MinWidth = 88, VerticalAlignment = VerticalAlignment.Center };
        foreach (var size in DispatchPageSizes) dispatchPageSizeBox.Items.Add(new ComboBoxItem { Content = $"{size}건", Tag = size });
        dispatchPageSizeBox.SelectedIndex = Array.IndexOf(DispatchPageSizes, dispatchPageSize);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(dispatchPageSizeBox, "한 페이지에 표시할 건수");
        dispatchPageSizeBox.SelectionChanged += (_, _) =>
        {
            if (dispatchPageSizeBox.SelectedItem is ComboBoxItem { Tag: int size }) SetDispatchPageSize(size);
        };
        var pageSizeLabel = Muted("페이지당");
        pageSizeLabel.VerticalAlignment = VerticalAlignment.Center;
        var pagination = Bar(dispatchCountText, previousResults, dispatchPageNumbers, nextResults, pageSizeLabel, dispatchPageSizeBox);
        pagination.Spacing = 6;
        var status = StatusLine(dispatchSummary, pagination);

        // ── 배치 ──
        dispatchSubtitle = Text("전체 · 0대");
        var main = new Grid();
        foreach (var h in new[] { GridLength.Auto, GridLength.Auto, GridLength.Auto, new GridLength(1, GridUnitType.Star), GridLength.Auto })
            main.RowDefinitions.Add(new RowDefinition { Height = h });
        main.Children.Add(PageTitle("탁송체크", dispatchSubtitle));
        Place(main, commandRow, 1);
        Place(main, filterLine, 2);
        Place(main, table, 3);
        Place(main, status, 4);
        var root = new Grid();
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star), MinWidth = 420 });
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        root.Children.Add(main);
        Place(root, dispatchPanel, 0, 1);

        // Ctrl+Enter: 차량 조회하기
        var accelerator = new KeyboardAccelerator { Key = VirtualKey.Enter, Modifiers = VirtualKeyModifiers.Control, ScopeOwner = root };
        accelerator.Invoked += (_, args) => { args.Handled = true; QueryDispatch(); };
        root.KeyboardAccelerators.Add(accelerator);

        RenderDispatch();
        return root;
    }

    private static void Place(Grid grid, FrameworkElement element, int row, int column = 0)
    {
        Grid.SetRow(element, row);
        Grid.SetColumn(element, column);
        grid.Children.Add(element);
    }

    /// <summary>오른쪽 패널: 입력 ↔ 슬랙봇 회신</summary>
    private void ShowSlackPanel(bool slack)
    {
        if (slack && results.Count == 0) { Notify("먼저 차량 조회를 해 주세요.", true); return; }
        if (slack) SetPanelVisible(dispatchPanelToggle, dispatchPanel, true);
        dispatchInputPanel.Visibility = slack ? Visibility.Collapsed : Visibility.Visible;
        slackDispatchStage.Visibility = slack ? Visibility.Visible : Visibility.Collapsed;
        if (slack) RefreshSlackDispatchList();
    }

    private bool SlackPanelOpen => slackDispatchStage.Visibility == Visibility.Visible;

    /// <summary>탐색 창에서 보기 필터(하위 항목)를 골랐을 때</summary>
    private void SetDispatchFilter(string key)
    {
        if (dispatchFilter == key) return;
        dispatchFilter = key;
        dispatchPage = 0;
        RenderDispatch();
    }

    private int DispatchCount(string key) => dispatchCounts.GetValueOrDefault(key);

    /// <summary>조회 결과가 바뀌면 보기별 대수(차량번호 중복 제거)를 다시 세요.</summary>
    private void RecountDispatch()
    {
        dispatchCounts.Clear();
        // '슬랙 회신 반영' 항목이 사라지면 그 보기에 머물지 않게 '전체'로 돌려요.
        if (dispatchFilter == "slack-replies" && !results.Any(row => row?.Flag("slackReviewed") == true)) { dispatchFilter = "all"; dispatchPage = 0; }
        if (results.Count > 0)
            foreach (var (key, _) in DispatchViews)
                dispatchCounts[key] = rules.Filter(results, key).Select(row => row.Text("plate")).Distinct().Count();
        UpdateDispatchNav();
        SyncDispatchBoxes();
    }

    /// <summary>업체·상태 상자 목록을 조회 결과에 맞춰요.</summary>
    private void SyncDispatchBoxes()
    {
        if (vendorBox is null) return;
        syncingDispatchBoxes = true;
        try
        {
            void Fill(ComboBox box, string all, IEnumerable<string> values, string current)
            {
                var items = new[] { all }.Concat(values.Where(v => v.Length > 0).Distinct().Order()).ToArray();
                box.ItemsSource = items;
                box.SelectedIndex = Math.Max(0, Array.IndexOf(items, current));
            }
            Fill(vendorBox, "전체 업체", results.Select(row => row.Text("vendor")), dispatchVendor);
            Fill(stateBox, "전체 상태", results.Select(row => row.Text("rawState")), dispatchState);
        }
        finally { syncingDispatchBoxes = false; }
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
                        dialog.DefaultButton = ContentDialogButton.Primary;
                        var entered = false;
                        password.KeyDown += (_, e) =>
                        {
                            if (e.Key != VirtualKey.Enter) return;
                            e.Handled = true; entered = true; dialog.Hide();
                        };
                        dialog.Opened += (_, _) => password.Focus(FocusState.Programmatic);
                        if (await dialog.ShowAsync() != ContentDialogResult.Primary && !entered) continue;
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
            vehicleCount.Text = "0대 인식";
            return;
        }

        try
        {
            var input = rules.ParseVehicles(vehicles.Text);
            var duplicates = input["duplicates"]!.GetValue<int>();
            var invalid = input["invalid"]!.AsArray().Count;
            var count = input["vehicles"]!.AsArray().Count - duplicates;
            var parts = new List<string> { $"{count:N0}대 인식" };
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
        // 조회 직전에도 빈 줄을 지우고 앞뒤 줄바꿈을 잘라요.
        var tidy = RemoveBlankLines(vehicles.Text).Trim('\n');
        if (tidy != NormalizeLines(vehicles.Text)) SetVehiclesQuietly(tidy, tidy.Length);
        try
        {
            var input = rules.ParseVehicles(vehicles.Text);
            if (input["invalid"]!.AsArray().Count > 0)
            { Notify("차량번호나 날짜를 확인해 주세요: " + string.Join(", ", input["invalid"]!.AsArray().Select(x => x!.ToString())), true); return; }
            if (input["vehicles"]!.AsArray().Count == 0) { Notify("차량목록을 입력해 주세요.", true); return; }
            if (files.Count == 0) { Notify("업체 파일을 첨부해 주세요.", true); return; }
            results = rules.Match(input["vehicles"]!.AsArray(), files);
            ClearSlackReplies();
            selected.Clear(); dispatchPage = 0;
            RecountDispatch();
            RenderDispatch();
            Go("dispatch");
        }
        catch (Exception e) { Notify(e.Message, true); }
    }
    private JsonArray VisibleResults()
    {
        var visible = rules.Filter(results, dispatchFilter);
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
        visibleCars = visible.Select(row => row.Text("plate")).Distinct().Count();
        var label = DispatchViews.FirstOrDefault(v => v.Key == dispatchFilter).Label ?? "전체";
        dispatchSubtitle.Text = $"{label} · {visibleCars:N0}대";
        UpdateDispatchSummary();
        RenderDispatchPager();
        dispatchEmpty.Visibility = visible.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        dispatchEmptyTitle.Text = results.Count == 0 ? "조회할 차량목록을 준비해 주세요" : "조건에 맞는 차량이 없어요";
        dispatchEmptyText.Text = results.Count == 0
            ? "오른쪽 입력 패널에 차량목록을 넣고 업체 파일을 끌어다 놓으면\n완료 차량의 금액과 확인할 목록을 정리해 드려요."
            : "탐색 창에서 다른 보기를 고르거나 검색어·업체·상태를 확인해 주세요.";
        var index = 0;
        foreach (var row in visible.Skip(dispatchPage * dispatchPageSize).Take(dispatchPageSize).OfType<JsonObject>())
        {
            var id = row.Text("id");
            var plate = row.Text("plate");
            Border? line = null;
            var lineIndex = index++;
            void Highlight() { if (line is not null) line.Background = selected.Contains(id) ? rowSelected : lineIndex % 2 == 1 ? rowAlt : transparent; }
            var check = new CheckBox { IsChecked = selected.Contains(id), MinWidth = 0, Padding = new Thickness(0) };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(check, plate + " 행 선택");
            check.Checked += (_, _) => { selected.Add(id); UpdateDispatchSummary(); Highlight(); };
            check.Unchecked += (_, _) => { selected.Remove(id); UpdateDispatchSummary(); Highlight(); };
            var done = new CheckBox { IsChecked = completed.Contains(plate), MinWidth = 0, Padding = new Thickness(0),
                HorizontalAlignment = HorizontalAlignment.Center };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(done, row.Text("plate") + " 완료");
            var workStatus = Text("", 11);
            workStatus.MinHeight = 14;
            Control? plateButton = null;
            void UpdateWorkStatus()
            {
                if (plateButton is not null)
                    plateButton.Foreground = completed.Contains(plate) ? green.Solid : copiedDispatch.Contains(plate) ? blue.Solid : indigo.Solid;
                workStatus.Text = completed.Contains(plate) ? (copiedDispatch.Contains(plate) ? "완료 · 복사함" : "완료")
                    : copiedDispatch.Contains(plate) ? "복사함" : "";
                workStatus.Foreground = completed.Contains(plate) ? green.Solid : blue.Solid;
            }
            done.Checked += (_, _) => { completed.Add(plate); SaveWorkMarks(); UpdateWorkStatus(); };
            done.Unchecked += (_, _) => { completed.Remove(plate); SaveWorkMarks(); UpdateWorkStatus(); };
            var content = Stack(Text(string.Join(" · ", new[] { row.Text("rawState"), row.Text("carrierType"), row.Text("origin") }.Where(value => value.Length > 0))));
            foreach (var (field, fieldLabel) in new[] { ("schedule", "탁송일정"), ("fee", "탁송비용") })
            {
                var candidates = row![field + "Candidates"]!.AsArray();
                if (!row.Flag(field + "Conflict") && candidates.Select(candidate => candidate.Text("value")).Distinct().Take(2).Count() < 2) continue;
                // 일정이 중첩이면 비용은 고른 일정의 기록을 따라가서 따로 고르지 않아요.
                if (field == "fee" && row["scheduleCandidates"]!.AsArray().Select(candidate => candidate.Text("value")).Distinct().Take(2).Count() > 1) continue;
                var combo = new ComboBox { Header = fieldLabel + " 원본 기록 선택", HorizontalAlignment = HorizontalAlignment.Stretch };
                foreach (var candidate in candidates) combo.Items.Add(new ComboBoxItem { Content = $"{candidate.Text("value")} · {candidate.Text("source")}", Tag = candidate.Text("id") });
                combo.SelectionChanged += (_, _) =>
                {
                    if (combo.SelectedItem is not ComboBoxItem item) return;
                    var at = results.Select((r, i) => (r, i)).First(p => p.r.Text("id") == id).i;
                    results[at] = rules.Choose(results[at]!, field, item.Tag.ToString()!);
                    RecountDispatch();
                    RenderDispatch();
                };
                content.Children.Add(combo);
            }
            var notes = rules.Notes(row!); if (notes.Length > 0) content.Children.Add(Text(notes));
            content.Children.Add(Text(row.Text("sources"), 12));
            // 조회 상태: 색 배지 대신 7px 점 + 글자
            var statusLabel = rules.Status(row!);
            var statusText = Text(statusLabel, 13);
            statusText.TextWrapping = TextWrapping.WrapWholeWords;
            var pill = Pill(statusText, new Ellipse());
            TintPill(pill, DispatchStatusSwatch(new StatusKey(row.Text("status"), statusLabel, row!.Flag("slackPending"))));
            var status = Stack(pill);
            status.Spacing = 2;
            if (row.Flag("slackReviewed")) status.Children.Add(Muted("슬랙봇 회신", 11));
            content.MaxWidth = 420;
            var needsChoice = row!.Flag("scheduleConflict") || row!.Flag("feeConflict");
            if (needsChoice)
            {
                var flagged = Text("일정·비용 선택 필요", 11);
                flagged.Foreground = orange.Solid;
                status.Children.Add(flagged);
            }
            var details = new Button { Content = needsChoice ? "선택" : "보기", Flyout = new Flyout { Content = content },
                HorizontalAlignment = HorizontalAlignment.Center };
            if (needsChoice) details.Style = (Style)Application.Current.Resources["AccentButtonStyle"];
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(details, row.Text("plate") + " 상세");
            void MarkCopied() { copiedDispatch.Add(plate); SaveWorkMarks(); UpdateWorkStatus(); }
            UpdateWorkStatus();
            var plateCell0 = CopyCell("차량번호", plate, plate, onCopy: MarkCopied);
            plateButton = plateCell0 as Control;
            if (plateButton is not null) plateButton.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
            UpdateWorkStatus();
            var plateLink = Bar(check, plateCell0);
            plateLink.VerticalAlignment = VerticalAlignment.Center;
            check.VerticalAlignment = VerticalAlignment.Center;
            workStatus.VerticalAlignment = VerticalAlignment.Bottom;
            workStatus.Margin = new Thickness(28, 0, 0, 0);
            workStatus.IsHitTestVisible = false;
            var plateCell = new Grid { MinHeight = 44 };
            plateCell.Children.Add(plateLink);
            plateCell.Children.Add(workStatus);
            var schedule = row.Text("schedule");
            var entry = Columns(dispatchColumns, plateCell, CopyCell("탁송일정", schedule, schedule, onCopy: MarkCopied),
                CopyCell("주유·충전비", row!.Flag("slackPending") ? Rules.SlackPending : row.Text("amount"), Money(row, "amount"), right: true, onCopy: MarkCopied),
                CopyCell("탁송비", row.Text("dispatchFee"), Money(row, "dispatchFee"), right: true, onCopy: MarkCopied),
                CopyCell("부가세", row.Text("vat"), Money(row, "vat"), right: true, onCopy: MarkCopied),
                status, Text(row.Text("vendor"), 13), done, details);
            line = TableRow(entry, lineIndex);
            Highlight();
            dispatchList.Children.Add(line);
        }
    }
    private FrameworkElement CopyCell(string label, string value, string display, bool right = false, Action? onCopy = null)
    {
        if (string.IsNullOrWhiteSpace(value) || value == "—")
        {
            var text = Text("—");
            text.Foreground = faintInk;
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
            button.MinWidth = 32;
            button.Padding = new Thickness(6, 4, 6, 4);
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
        dispatchSummary.Text = results.Count == 0
            ? "차량목록과 업체 파일을 넣고 조회하세요."
            : $"전체 {results.Count:N0}대 · 주유·충전비 {visibleAmount:N0}원 · {first:N0}–{last:N0}줄 ({dispatchPage + 1}/{DispatchPageCount}페이지)";
        dispatchCountText.Text = results.Count == 0 ? "" :
            $"{visibleCount:N0}줄 · {visibleCars:N0}대" + (selected.Count > 0 ? $" · 선택 {selected.Count:N0}대" : "");
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

public sealed partial class MainWindow
{
    /// <summary>오른쪽 패널 보이기/숨기기 단추 (채움 없는 아이콘 단추)</summary>
    private AppBarButton PanelToggle(string label, Func<FrameworkElement> panel)
    {
        var button = new AppBarButton { Label = label, Icon = new FontIcon { Glyph = "\uE8A0" }, LabelPosition = CommandBarLabelPosition.Collapsed, Width = 48 };
        ToolTipService.SetToolTip(button, label + " 보이기/숨기기");
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(button, label + " 숨기기");
        button.Click += (_, _) => SetPanelVisible(button, panel(), panel().Visibility != Visibility.Visible);
        return button;
    }

    private static void SetPanelVisible(AppBarButton button, FrameworkElement panel, bool visible)
    {
        panel.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;
        ((FontIcon)button.Icon).Glyph = visible ? "\uE8A0" : "\uE89F";
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(button, button.Label + (visible ? " 숨기기" : " 보이기"));
    }
}
public sealed partial class MainWindow
{
    // 차량목록: 붙여넣기처럼 한 번에 2글자 이상 바뀌면 빈 줄(공백·탭만 있는 줄 포함)을 바로 지워요.
    // 한 글자씩 칠 때(Enter 등)는 그대로 둬서 줄을 추가할 수 있어요. '정리' 단추(날짜·시간 맞추기)는 따로 있어요.
    private string lastVehicleText = "";
    private string? pendingVehicleText;

    /// <summary>CRLF·CR을 LF로 (WinUI 입력칸은 줄바꿈을 CR로 돌려줘요)</summary>
    private static string NormalizeLines(string text) => text.Replace("\r\n", "\n").Replace('\r', '\n');

    /// <summary>빈 줄을 지워요. 원래 끝이 줄바꿈이면 마지막 줄바꿈 하나는 남겨요.</summary>
    internal static string RemoveBlankLines(string text)
    {
        var normalized = NormalizeLines(text);
        var kept = string.Join("\n", normalized.Split('\n').Where(line => line.Trim().Length > 0));
        return kept.Length > 0 && normalized.EndsWith('\n') ? kept + "\n" : kept;
    }

    /// <summary>바뀐 글자 수 (앞뒤 같은 부분을 뺀 넣은·지운 글자 중 큰 쪽)</summary>
    private static int ChangedCharacters(string before, string after)
    {
        var prefix = 0;
        while (prefix < before.Length && prefix < after.Length && before[prefix] == after[prefix]) prefix++;
        var suffix = 0;
        while (suffix < before.Length - prefix && suffix < after.Length - prefix && before[^(suffix + 1)] == after[^(suffix + 1)]) suffix++;
        return Math.Max(before.Length - prefix - suffix, after.Length - prefix - suffix);
    }

    private void OnVehiclesChanged()
    {
        var now = vehicles.Text;
        // 프로그램이 정리해 넣은 글자면 조회 결과를 지우지 않아요.
        if (pendingVehicleText is not null && NormalizeLines(now) == pendingVehicleText)
        {
            pendingVehicleText = null;
            lastVehicleText = now;
            UpdateVehicleCount();
            return;
        }
        // 같은 글자로 다시 알려 오는 변경(정리해 넣은 뒤 늦게 오는 알림 등)은 무시해요.
        if (NormalizeLines(now) == NormalizeLines(lastVehicleText)) { pendingVehicleText = null; return; }
        var changed = ChangedCharacters(NormalizeLines(lastVehicleText), NormalizeLines(now));
        lastVehicleText = now;
        UpdateVehicleCount();
        InvalidateDispatch();
        if (changed < 2) return;
        var cleaned = RemoveBlankLines(now);
        if (cleaned == NormalizeLines(now)) return;
        // 커서 위치 유지: 커서 앞부분에서 지워진 빈 줄만큼 당겨요.
        var caret = Math.Clamp(vehicles.SelectionStart, 0, now.Length);
        SetVehiclesQuietly(cleaned, Math.Min(RemoveBlankLines(now[..caret]).Length, cleaned.Length));
    }

    private void SetVehiclesQuietly(string text, int caret)
    {
        pendingVehicleText = text;
        vehicles.Text = text;
        lastVehicleText = vehicles.Text;
        vehicles.SelectionStart = Math.Clamp(caret, 0, vehicles.Text.Length);
        vehicles.SelectionLength = 0;
    }
}