using System.Text.Json.Nodes;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Ellipse = Microsoft.UI.Xaml.Shapes.Ellipse;
using WorkTool.Core;

namespace WorkTool.WinUI;

// 하나은행 이체파일: 단계 표시줄 대신 탐색 창 하위 항목(1·2·3단계)으로 오가요.
// 단계마다 CommandBar와 녹색 주 단추, 오른쪽 정보 패널이 바뀌어요.
public sealed partial class MainWindow
{
    private static readonly double[] TransferColumns = [120, 178, 120, 0, 0, 95];
    private const int MaxTransferRows = 100;
    private JsonArray transfers = new();
    private readonly HashSet<string> imported = new();
    private readonly List<TransferRowView> transferViews = [];
    private TextBlock transferSummary = null!, transferSubtitle = null!, transferSum = null!, transferRowCount = null!, summary3 = null!;
    private StackPanel transferList = null!, previewRows = null!, commentList = null!, detailIssues = null!;
    private TextBlock detailTitle = null!, detailSource = null!;
    private TextBox cmsOnExtract = null!;
    private CheckBox acknowledge = null!;
    private Button download = null!, extractButton = null!, nextStepButton = null!, downloadCommand = null!;
    private Grid transferInput = null!, transferReview = null!, transferDownload = null!, transferHeaderHost = null!;
    private FrameworkElement transferPanel1 = null!, transferPanel2 = null!, transferPanel3 = null!, ackSection = null!;
    private CommandBar transferCommands1 = null!, transferCommands2 = null!, transferCommands3 = null!;
    private Border transferPanel = null!;
    private AppBarButton transferPanelToggle = null!;
    private JsonNode? detailRow;
    private bool refreshing, saving;
    private int step = 1;
    private int lastBad, lastWarn;
    private string savedSignature = "", savedThread = "", acknowledgedSignature = "";

    private sealed record TransferRowView(FrameworkElement Card, CheckBox Selection, TextBlock Status, Border Pill, TextBlock Why, JsonNode Row);

    private FrameworkElement TransferPage()
    {
        LoadTransferWidths();
        threadInputs = new StackPanel { Spacing = 0 };
        AddThreadInput(first: true);
        transferSummary = Muted("이체 요청을 붙여넣고 추출해 주세요.");
        transferSummary.VerticalAlignment = VerticalAlignment.Center;
        transferRowCount = Muted("");
        transferRowCount.VerticalAlignment = VerticalAlignment.Center;
        transferList = new StackPanel();
        acknowledge = new CheckBox { Content = Text("검토 항목과 원문을 대조했고, 중복 송금 여부를 확인했어요.", 13) };
        acknowledge.Checked += (_, _) => { acknowledgedSignature = Signature; ValidateTransfers(); };
        acknowledge.Unchecked += (_, _) => { acknowledgedSignature = ""; ValidateTransfers(); };
        download = AsyncButton("이체파일 다운로드", SaveTransfer, true);
        download.HorizontalAlignment = HorizontalAlignment.Stretch;
        download.Padding = new Thickness(12, 8, 12, 8);
        previewRows = new StackPanel();
        commentList = new StackPanel();

        // ── CommandBar (단계별) ──
        CommandBar Bar3(params ICommandBarElement[] commands)
        {
            var bar = new CommandBar { DefaultLabelPosition = CommandBarDefaultLabelPosition.Right, Background = transparent, HorizontalAlignment = HorizontalAlignment.Left, IsDynamicOverflowEnabled = true };
            foreach (var command in commands) bar.PrimaryCommands.Add(command);
            bar.SecondaryCommands.Add(Command("처음부터", "\uE777", ResetTransfers, purple));
            bar.SecondaryCommands.Add(Command("단축키", "\uE765", ShowTransferShortcuts));
            return bar;
        }
        transferCommands1 = Bar3(Command("입력칸 추가", "\uE710", () => AddThreadInput().Focus(FocusState.Programmatic), green));
        transferCommands2 = Bar3(Command("행 추가", "\uE710", AddTransfer, green), Command("선택 삭제", "\uE74D", DeleteSelectedTransfers, red),
            Command("표시 내역 선택", "\uE762", SelectVisibleTransfers), Command("1억 초과 분할", "\uE8C6", SplitTransfers),
            Command("필터 지우기", "\uE894", ClearTransferFilters), Command("열 너비", "\uE9A6", EditTransferWidths));
        transferCommands3 = Bar3(Command("2단계에서 고치기", "\uE70F", () => Step(2), orange), Command("코멘트 목록", "\uE8BD", ShowTransferComments));
        extractButton = Button("이체정보 추출하기", ExtractTransfer, true);
        nextStepButton = Button("다운로드 단계로 →", () => Step(3), true);
        downloadCommand = Button("이체파일 다운로드", () => { }, true);
        downloadCommand.Click += async (_, _) => { try { await SaveTransfer(); } catch (Exception e) { Notify(e.Message, true); } };
        foreach (var b in new[] { extractButton, nextStepButton, downloadCommand }) { ToolTipService.SetToolTip(b, "Ctrl+Enter"); b.VerticalAlignment = VerticalAlignment.Center; }
        transferPanelToggle = PanelToggle("정보 패널", () => transferPanel);
        var commandsLeft = new Grid();
        commandsLeft.Children.Add(transferCommands1); commandsLeft.Children.Add(transferCommands2); commandsLeft.Children.Add(transferCommands3);
        var commandRow = new Grid { Margin = new Thickness(16, 0, 16, 4) };
        commandRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        commandRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        commandRow.Children.Add(commandsLeft);
        var primary = Bar(extractButton, nextStepButton, downloadCommand, transferPanelToggle);
        primary.VerticalAlignment = VerticalAlignment.Center;
        Place(commandRow, primary, 0, 1);

        // ── 1단계 ──
        transferInput = new Grid { BorderBrush = divider, BorderThickness = new Thickness(0, 1, 0, 0) };
        var threadsBody = Stack(threadInputs, Muted("같은 요청을 다시 추출하면 이미 가져온 건은 건너뛰어요. 새 스레드를 붙여넣어 추가할 수 있어요.", 12));
        threadsBody.Padding = new Thickness(24, 16, 24, 16);
        transferInput.Children.Add(VerticalScroll(threadsBody));

        // ── 2단계: 보기(전체·정상·검토 필요·오류) + 자동 보정 + 총 입금액, 편집 가능한 표 ──
        autoFixAccounts = new CheckBox { Content = Text("계좌번호 자동 제거(하이픈·공백)", 13), IsChecked = true, VerticalAlignment = VerticalAlignment.Center };
        transferSum = Text("", 14);
        transferSum.VerticalAlignment = VerticalAlignment.Center;
        var reviewTop = new Grid { Margin = new Thickness(20, 0, 24, 6), ColumnSpacing = 16 };
        reviewTop.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        reviewTop.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var filters = (Panel)TransferFilters();
        filters.Children.Add(autoFixAccounts);
        reviewTop.Children.Add(filters);
        Place(reviewTop, transferSum, 0, 1);
        transferHeaderHost = new Grid();
        var table = new Grid();
        table.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        table.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        table.Children.Add(transferHeaderHost);
        var rows = new ScrollViewer { Content = transferList, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollMode = ScrollMode.Disabled };
        Place(table, rows, 1);
        var tableScroll = Horizontal(table);
        tableScroll.Padding = new Thickness(12, 0, 0, 0);
        transferReview = new Grid();
        transferReview.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        transferReview.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        transferReview.Children.Add(reviewTop);
        var tableHost = new Grid { BorderBrush = divider, BorderThickness = new Thickness(0, 1, 0, 0) };
        tableHost.Children.Add(tableScroll);
        Place(transferReview, tableHost, 1);

        // ── 3단계: 요약 한 줄 + 파일 미리보기 ──
        summary3 = Text("", 14);
        summary3.Margin = new Thickness(24, 0, 24, 10);
        var previewTable = Stack(TableHeader(TransferColumns, "입금은행", "입금계좌번호", "입금액", "출금통장 표시", "입금통장 표시", "CMS코드"), previewRows);
        previewTable.Spacing = 0;
        previewTable.MinWidth = 790;
        transferDownload = new Grid();
        transferDownload.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        transferDownload.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        transferDownload.Children.Add(summary3);
        var previewHost = new Grid { BorderBrush = divider, BorderThickness = new Thickness(0, 1, 0, 0) };
        var previewScroll = Scroll(previewTable);
        previewScroll.Padding = new Thickness(12, 0, 0, 0);
        previewHost.Children.Add(previewScroll);
        Place(transferDownload, previewHost, 1);

        // ── 오른쪽 정보 패널 ──
        cmsOnExtract = Input("CMS코드 (선택)", "비워 둬도 돼요");
        transferPanel1 = PanelSections(
            Stack(ColorHeading("추출 설정", blue, "\uE713", 16, 20), ColorHeading("CMS코드", indigo, "\uE8EC"), Muted("추출하는 모든 건에 같은 코드를 넣어요.", 12), cmsOnExtract),
            Stack(ColorHeading("이렇게 추출해요", purple, "\uE946"), Muted("1. Slack 이체 요청 스레드를 그대로 붙여넣어요.\n2. 스레드가 여러 개면 ‘입력칸 추가’로 나눠 넣어요.\n3. 이체정보 추출하기(Ctrl+Enter)를 누르면 은행·계좌번호·금액·차량번호를 뽑아 2단계로 가요.\n\n넣은 내용은 이 앱 안에서만 처리하고 서버로 보내지 않아요.", 13)));
        var detailHeading = ColorHeading("선택한 건 원문", blue, "\uE8A5");
        detailTitle = (TextBlock)detailHeading.Children[1];
        detailSource = new TextBlock { Text = "표에서 줄을 누르면 Slack 원문이 여기에 보여요.", TextWrapping = TextWrapping.Wrap, FontSize = 12, Foreground = mutedInk,
            FontFamily = (Microsoft.UI.Xaml.Media.FontFamily)Application.Current.Resources["MonoFont"], IsTextSelectionEnabled = true };
        detailIssues = new StackPanel { Spacing = 4 };
        transferPanel2 = PanelSections(
            Stack(detailHeading, new ScrollViewer { Content = detailSource, MaxHeight = 200, VerticalScrollBarVisibility = ScrollBarVisibility.Auto }),
            Stack(ColorHeading("확인 사항", orange, "\uE7BA"), detailIssues),
            Stack(ColorHeading("통장 메모·CMS 일괄 입력", teal, "\uE70F"), MemoControls()));
        var copyAll = new HyperlinkButton { Content = "모두 복사", Padding = new Thickness(0) };
        copyAll.Click += (_, _) => Copy(rules.Comments(transfers));
        ToolTipService.SetToolTip(copyAll, "Alt+Shift+C");
        var commentHead = Columns([0, 0], ColorHeading("송금 코멘트", purple, "\uE8BD"), copyAll); commentHead.Padding = new Thickness(0);
        copyAll.HorizontalAlignment = HorizontalAlignment.Right;
        var notReal = Text("이체파일 생성은 실제 송금이 아니에요. 은행에 올리기 전 받는 분·계좌번호·금액을 다시 확인해 주세요.", 12);
        notReal.Foreground = warningInk;
        ackSection = Stack(PanelHeading("검토 확인"), acknowledge);
        ((StackPanel)ackSection).Spacing = 6;
        transferPanel3 = PanelSections(
            Stack(ColorHeading("이체파일 받기", green, "\uE896"), ackSection, download, Muted("하나은행 기업뱅킹 [다계좌이체] 업로드용 .xlsx", 12), notReal),
            Stack(commentHead, Muted("송금을 마친 뒤 Slack에 남길 문구예요.", 12), commentList));
        var panelHost = new Grid();
        panelHost.Children.Add(transferPanel1); panelHost.Children.Add(transferPanel2); panelHost.Children.Add(transferPanel3);
        transferPanel = SidePanel(VerticalScroll(panelHost));

        // ── 배치 ──
        transferSubtitle = Text("1단계 · 이체 요청 입력");
        var host = new Grid(); host.Children.Add(transferInput); host.Children.Add(transferReview); host.Children.Add(transferDownload);
        var main = new Grid();
        foreach (var h in new[] { GridLength.Auto, GridLength.Auto, new GridLength(1, GridUnitType.Star), GridLength.Auto })
            main.RowDefinitions.Add(new RowDefinition { Height = h });
        main.Children.Add(PageTitle("하나은행 이체파일", transferSubtitle));
        Place(main, commandRow, 1);
        Place(main, host, 2);
        Place(main, StatusLine(transferSummary, transferRowCount), 3);
        var grid = new Grid();
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star), MinWidth = 420 });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.Children.Add(main);
        Place(grid, transferPanel, 0, 1);
        BuildTransferHeader();
        Step(1);
        AddTransferShortcuts(grid);
        return grid;
    }

    /// <summary>패널 안 구역들: 구분선으로만 나눠요.</summary>
    private StackPanel PanelSections(params StackPanel[] sections)
    {
        var panel = new StackPanel();
        for (var i = 0; i < sections.Length; i++)
        {
            sections[i].Spacing = Math.Max(sections[i].Spacing, 8);
            panel.Children.Add(new Border
            {
                Child = sections[i], Padding = new Thickness(20, 16, 20, 18),
                BorderBrush = divider, BorderThickness = new Thickness(0, 0, 0, i < sections.Length - 1 ? 1 : 0)
            });
        }
        return panel;
    }

    private UIElement MemoControls()
    {
        var outMemo = Input("출금통장 표시", "일괄 적용");
        var inMemo = Input("입금통장 표시", "일괄 적용");
        var cms = Input("CMS코드", "일괄 적용");
        var onlyEmpty = new CheckBox { Content = Text("빈 칸에만 적용", 13), IsChecked = true };
        var apply = Button("메모 적용", () =>
        {
            foreach (var r in transfers)
                foreach (var (key, value) in new[] { ("outMemo", outMemo.Text), ("inMemo", inMemo.Text), ("cms", cms.Text) })
                    if (value.Length > 0 && (onlyEmpty.IsChecked != true || r.Text(key) == "")) r![key] = value;
            RenderTransfers();
        });
        var stack = Stack(outMemo, inMemo, cms, onlyEmpty, apply);
        stack.Spacing = 8;
        return stack;
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
                    row["cms"] = cmsOnExtract?.Text.Trim() ?? "";
                    staged.Add(row);
                }
            }
            var split = rules.Split(staged);
            if (transfers.Count + split.Count > MaxTransferRows) { Notify("한 파일에 100건까지 가능해요. 요청을 나눠서 처리해 주세요.", true); return; }
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
        if (transfers.Count >= MaxTransferRows) { Notify("한 파일에 100건까지 가능해요.", true); return; }
        transfers.Add(new JsonObject { ["id"] = Guid.NewGuid().ToString("N"), ["bank"] = "", ["account"] = "", ["amount"] = "", ["plate"] = "", ["outMemo"] = "", ["inMemo"] = "", ["cms"] = "", ["source"] = "" });
        RenderTransfers();
    }
    private void SplitTransfers()
    {
        var parts = rules.Split(transfers);
        if (parts.Count > MaxTransferRows) { Notify("금액을 나누면 100건을 넘어요. 다른 줄을 줄여 주세요.", true); return; }
        if (!JsonNode.DeepEquals(transfers, parts)) { transfers = parts; RenderTransfers(); }
    }
    private string Signature => transfers.ToJsonString();
    private bool HasUnsavedTransfers() => transfers.Count == 0
        ? threadBoxes.Any(box => !string.IsNullOrWhiteSpace(box.Text))
        : savedSignature != Signature || savedThread != ThreadSignature;

    // 표 열: [선택][#][상태][은행][계좌번호][예금주][입금액][차량번호][출금 표시][입금 표시][CMS]
    private double[] TransferTableWidths() => [36, 72, 150, .. transferWidths];

    private void BuildTransferHeader()
    {
        transferHeaderHost.Children.Clear();
        transferHeaderHost.Children.Add(TableHeader(TransferTableWidths(), ["", "#", "상태", .. TransferFields.Select(f => f.Key == "amount" ? "입금액" : f.Label)]));
    }

    private void RenderTransfers()
    {
        refreshing = true;
        transferList.Children.Clear();
        transferViews.Clear();
        groupedEditors.Clear();
        var banks = rules.Banks;
        var widths = TransferTableWidths();
        foreach (var row in transfers.ToArray())
        {
            var id = row.Text("id");
            var index = transferViews.Count;
            var pick = new CheckBox { IsChecked = selectedTransfers.Contains(id), MinWidth = 0, Padding = new Thickness(0) };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(pick, $"{index + 1}번 선택");
            // # 아래 주황 '분할 1/2'
            var number = new StackPanel();
            number.Children.Add(Text((index + 1).ToString(), 13));
            var group = row.Text("splitGroup");
            if (group.Length > 0)
            {
                var siblings = transfers.Where(x => x.Text("splitGroup") == group).ToList();
                var split = Text($"분할 {siblings.IndexOf(row) + 1}/{siblings.Count}", 11);
                split.Foreground = warningInk;
                split.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
                number.Children.Add(split);
            }
            var statusText = Text("", 12);
            var pill = Pill(statusText, new Ellipse());
            var why = Muted("", 11);
            why.MaxWidth = 140;
            var status = Stack(pill, why);
            status.Spacing = 1;
            var bank = new ComboBox { ItemsSource = banks, SelectedItem = row.Text("bank"), HorizontalAlignment = HorizontalAlignment.Stretch, MinWidth = 0 };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(bank, "입금은행");
            bank.SelectionChanged += (_, _) => UpdateTransferField(row!, "bank", bank.SelectedItem?.ToString() ?? "");
            groupedEditors[(id, "bank")] = value => bank.SelectedItem = value;
            var cells = new List<FrameworkElement> { pick, number, status, bank };
            foreach (var (key, label) in TransferFields.Skip(1))
            {
                var box = Edit(row!, key, label, double.NaN, cell: true);
                if (key == "amount") box.TextAlignment = TextAlignment.Right;
                cells.Add(box);
            }
            var grid = Columns(widths, cells.ToArray());
            grid.ColumnSpacing = 8;
            grid.MinHeight = 48;
            var line = TableRow(grid, index);
            void Highlight() => line.Background = selectedTransfers.Contains(id) ? rowSelected : index % 2 == 1 ? rowAlt : transparent;
            pick.Checked += (_, _) => { selectedTransfers.Add(id); Highlight(); };
            pick.Unchecked += (_, _) => { selectedTransfers.Remove(id); Highlight(); };
            Highlight();
            // 줄을 누르거나 칸에 들어가면 오른쪽 패널에 원문·확인 사항을 보여 줘요.
            line.AddHandler(UIElement.PointerPressedEvent, new PointerEventHandler((_, _) => ShowTransferDetail(row!)), true);
            line.GotFocus += (_, _) => ShowTransferDetail(row!);
            transferViews.Add(new TransferRowView(line, pick, statusText, pill, why, row!));
            transferList.Children.Add(line);
        }
        if (transfers.Count == 0)
        {
            var empty = Muted("아직 이체 내역이 없어요. 1단계에서 스레드를 붙여넣고 추출해 주세요.", 13);
            empty.Margin = new Thickness(12, 16, 0, 0);
            transferList.Children.Add(empty);
        }
        if (detailRow is not null && !transfers.Contains(detailRow)) detailRow = null;
        refreshing = false; ValidateTransfers();
    }
    private TextBox Edit(JsonNode row, string key, string label, double width, bool cell = false)
    {
        var box = Input(cell ? "" : label, "", width); box.Text = row.Text(key);
        if (cell)
        {
            box.Header = null;
            box.MinWidth = 0;
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(box, label);
        }
        groupedEditors[(row.Text("id"), key)] = value => box.Text = value;
        box.TextChanged += (_, _) =>
        {
            if (!refreshing) UpdateTransferField(row, key, NormalizeTransferValue(key, box.Text));
        };
        box.LostFocus += (_, _) => CommitTransferEdit(row, key, box);
        return box;
    }
    private (int Bad, int Warn) TransferCounts() => (lastBad, lastWarn);

    private void ValidateTransfers()
    {
        if (refreshing || acknowledge is null) return;
        if (acknowledge.IsChecked == true && acknowledgedSignature != Signature) acknowledge.IsChecked = false;
        var checks = rules.Check(transfers); var bad = checks.Count(x => x.Text("state") == "bad"); var warn = checks.Count(x => x.Text("state") == "warn");
        lastBad = bad; lastWarn = warn;
        var total = Sum(transfers, "amount");
        transferSummary.Text = transfers.Count == 0 ? "이체 요청을 붙여넣고 추출해 주세요."
            : $"{transfers.Count:N0}건 · 합계 {total:N0}원 · 오류 {bad}건 · 검토 {warn}건" + (transfers.Count > 100 ? " · 한 파일에 100건까지 가능해요." : "");
        transferSum.Inlines.Clear();
        transferSum.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = "총 입금액 ", Foreground = mutedInk });
        transferSum.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = $"{total:N0}원", FontWeight = Microsoft.UI.Text.FontWeights.Bold, Foreground = accent.Solid });
        UpdateTransferSeg(transfers.Count, transfers.Count - bad - warn, warn, bad);
        for (var i = 0; i < checks.Count && i < transferViews.Count; i++)
        {
            var view = transferViews[i];
            var state = checks[i].Text("state");
            view.Card.Visibility = ShowTransfer(transfers[i], state) ? Visibility.Visible : Visibility.Collapsed;
            view.Status.Text = state switch { "bad" => "오류", "warn" => "검토 필요", _ => "정상" };
            TintPill(view.Pill, state switch { "bad" => red, "warn" => orange, _ => green });
            view.Why.Text = string.Join(" · ", checks[i]!["errors"]!.AsArray().Concat(checks[i]!["reviews"]!.AsArray()).Select(x => x!.ToString()));
            view.Why.Visibility = view.Why.Text.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        }
        var shown = transferViews.Count(v => v.Card.Visibility == Visibility.Visible);
        transferRowCount.Text = transfers.Count == 0 ? "" : shown == transfers.Count ? $"이체 내역 {transfers.Count:N0}건" : $"이체 내역 {shown:N0} / {transfers.Count:N0}건";
        download.IsEnabled = !saving && transfers.Count is > 0 and <= MaxTransferRows && bad == 0 && (warn == 0 || acknowledge.IsChecked == true);
        downloadCommand.IsEnabled = download.IsEnabled;
        ackSection.Visibility = warn > 0 ? Visibility.Visible : Visibility.Collapsed;
        if (detailRow is not null) ShowTransferDetail(detailRow);
        if (step == 3) { UpdatePreview(); RenderComments(); }
        UpdateTransferNav();
    }

    /// <summary>오른쪽 패널: 고른 건의 원문과 확인 사항 (오류 빨강, 검토 주황)</summary>
    private void ShowTransferDetail(JsonNode row)
    {
        if (!transfers.Contains(row)) return;
        detailRow = row;
        detailTitle.Text = $"{transfers.IndexOf(row) + 1}번 건 원문";
        var source = row.Text("ctx").Length > 0 ? row.Text("ctx") : row.Text("source");
        detailSource.Text = source.Length > 0 ? source : "(원문 없음)";
        detailIssues.Children.Clear();
        var check = rules.Check(transfers)[transfers.IndexOf(row)]!;
        void Add(string text, Microsoft.UI.Xaml.Media.SolidColorBrush ink)
        {
            var label = Text(text, 13); label.Foreground = ink;
            var dot = DotLabel(label, new Ellipse { Fill = ink });
            dot.VerticalAlignment = VerticalAlignment.Top;
            detailIssues.Children.Add(dot);
        }
        foreach (var e in check["errors"]!.AsArray()) Add(e!.ToString(), dangerInk);
        foreach (var r in check["reviews"]!.AsArray()) Add(r!.ToString(), warningInk);
        if (detailIssues.Children.Count == 0) Add("확인할 사항이 없어요.", successInk);
    }

    public void Step(int n)
    {
        step = n;
        transferInput.Visibility = n == 1 ? Visibility.Visible : Visibility.Collapsed;
        transferReview.Visibility = n == 2 ? Visibility.Visible : Visibility.Collapsed;
        transferDownload.Visibility = n == 3 ? Visibility.Visible : Visibility.Collapsed;
        transferPanel1.Visibility = transferCommands1.Visibility = extractButton.Visibility = transferInput.Visibility;
        transferPanel2.Visibility = transferCommands2.Visibility = nextStepButton.Visibility = transferReview.Visibility;
        transferPanel3.Visibility = transferCommands3.Visibility = downloadCommand.Visibility = transferDownload.Visibility;
        transferSubtitle.Text = $"{n}단계 · {TransferStepNames[n - 1]}";
        ValidateTransfers();
        UpdateTransferNav();
    }
    private void UpdatePreview()
    {
        previewRows.Children.Clear();
        var c = (Total: transfers.Count, Sum: Sum(transfers, "amount"));
        summary3.Inlines.Clear();
        summary3.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = $"총 {c.Total:N0}건", FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        summary3.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = "  ·  ", Foreground = mutedInk });
        summary3.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = $"{c.Sum:N0}원", FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        summary3.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = "  ·  ", Foreground = mutedInk });
        summary3.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run
        {
            Text = "● " + (lastBad > 0 ? $"오류 {lastBad}건" : lastWarn > 0 ? $"검토 필요 {lastWarn}건" : "오류 없음"),
            Foreground = lastBad > 0 ? dangerInk : lastWarn > 0 ? warningInk : successInk
        });
        if (transfers.Count == 0) { var empty = Muted("이체 내역을 추가하면 여기에 표시돼요.", 13); empty.Margin = new Thickness(12, 12, 0, 0); previewRows.Children.Add(empty); }
        var i = 0;
        foreach (var row in transfers)
        {
            var amount = Amount(row, "amount");
            if (row.Text("amount").Trim().Length == 0) amount.Text = "미입력";
            previewRows.Children.Add(TableRow(Columns(TransferColumns, Text(row.Text("bank")),
                Text(row.Text("account")), amount, Text(row.Text("outMemo")),
                Text(row.Text("inMemo")), Text(row.Text("cms"))), i++));
        }
    }

    /// <summary>송금 코멘트: 줄마다 복사 아이콘</summary>
    private void RenderComments()
    {
        commentList.Children.Clear();
        var lines = rules.Comments(transfers).Split(Environment.NewLine, StringSplitOptions.RemoveEmptyEntries);
        if (lines.Length == 0) { commentList.Children.Add(Muted("이체 내역이 없어요.", 13)); return; }
        var first = true;
        foreach (var line in lines)
        {
            var copy = new Button { Content = new FontIcon { Glyph = "\uE8C8", FontSize = 14 }, Background = transparent, BorderThickness = new Thickness(0), Padding = new Thickness(6), VerticalAlignment = VerticalAlignment.Top };
            ToolTipService.SetToolTip(copy, "복사");
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(copy, "복사: " + line);
            var text = line;
            copy.Click += (_, _) => Copy(text);
            var grid = Columns([0, 36], Text(line, 13), copy);
            grid.Padding = new Thickness(0, 4, 0, 4);
            commentList.Children.Add(new Border { Child = grid, BorderBrush = divider, BorderThickness = new Thickness(0, first ? 0 : 1, 0, 0) });
            first = false;
        }
    }
    private async Task SaveTransfer()
    {
        if (saving) return;
        // Validate again at the write boundary; UI enabled state is not the safety gate.
        var checks = rules.Check(transfers);
        var signature = Signature;
        var needsReview = checks.Any(x => x.Text("state") == "warn");
        bool IsReviewed() => !needsReview || (acknowledge.IsChecked == true && acknowledgedSignature == signature);
        if (transfers.Count is 0 or > MaxTransferRows || checks.Any(x => x.Text("state") == "bad") || !IsReviewed())
            throw new InvalidOperationException("오류를 수정하고 검토 항목을 확인해 주세요.");
        saving = true;
        try
        {
            if (smokeDirectory is null && savedSignature != signature && !await ConfirmSentAgain()) return;
            var sourceSignature = ThreadSignature;
            var values = Spreadsheets.TransferRows(transfers);
            var fileName = rules.TransferFileName(transfers, DateTime.Now.ToString("yyyyMMdd_HHmm"));
            var file = await SavePicker(Path.GetFileNameWithoutExtension(fileName), ".xlsx");
            if (file is null) return;
            if (signature != Signature || sourceSignature != ThreadSignature || !IsReviewed())
            {
                Notify("저장 창을 연 뒤 내역이 바뀌었어요. 변경 내용을 확인하고 다시 저장해 주세요.", true);
                return;
            }
            await Task.Run(() => Spreadsheets.Write(file.Path, values, "다계좌이체"));
            savedSignature = signature;
            savedThread = sourceSignature;
            if (smokeDirectory is null) RememberSent(DateTime.Now.ToString("HH:mm"));
            Notify("이체파일을 저장했어요. 은행 업로드 전에 계좌와 금액을 확인해 주세요.");
        }
        finally { saving = false; ValidateTransfers(); }
    }
    private const string SentTodayFile = "transfer-sent-today.json";

    // 오늘 이미 받은 이체파일과 같은 은행·계좌·금액이 있으면 한 번 더 물어봐요 (앱을 다시 연 뒤 두 번 송금 방지).
    private async Task<bool> ConfirmSentAgain()
    {
        var sent = LocalState.ReadToday(SentTodayFile)["items"] as JsonArray ?? new JsonArray();
        var matches = rules.SentMatches(transfers, sent);
        if (matches.Count == 0) return true;
        var lines = matches.Take(10).Select(m =>
        {
            var row = transfers[m!["row"]!.GetValue<int>()]!;
            return $"{row.Text("bank")} {row.Text("account")} {decimal.Parse(row.Text("amount")):N0}원 · {m.Text("at")}에 받음";
        }).ToList();
        if (matches.Count > 10) lines.Add($"외 {matches.Count - 10}건");
        var dialog = Dialog("오늘 이미 받은 이체 건이 있어요",
            Stack(Text($"같은 은행·계좌·금액 {matches.Count}건이 오늘 받은 이체파일에 이미 들어 있어요. 두 번 송금하지 않도록 확인해 주세요."),
                Text(string.Join(Environment.NewLine, lines), 13)), "그래도 저장");
        return await dialog.ShowAsync() == ContentDialogResult.Primary;
    }

    private void RememberSent(string at)
    {
        var saved = LocalState.ReadToday(SentTodayFile);
        var items = saved["items"] as JsonArray ?? new JsonArray();
        var have = new HashSet<string>(items.Select(x => x.Text("key")));
        foreach (var key in rules.SentKeys(transfers))
            if (have.Add(key)) items.Add(new JsonObject { ["key"] = key, ["at"] = at });
        while (items.Count > 2000) items.RemoveAt(0);
        saved["items"] = items.DeepClone();
        LocalState.WriteToday(SentTodayFile, saved);
    }

    private async Task ResetTransfers()
    {
        if (await Dialog("전체 초기화", Text("붙여넣은 스레드와 이체 내역을 모두 지울까요?"), "모두 지우기").ShowAsync() != ContentDialogResult.Primary) return;
        transfers.Clear(); imported.Clear(); selectedTransfers.Clear(); threadBoxes.Clear(); threadInputs.Children.Clear();
        detailRow = null;
        AddThreadInput(first: true);
        savedSignature = ""; RenderTransfers(); Step(1);
    }
}
