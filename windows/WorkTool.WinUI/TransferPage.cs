using System.Text.Json.Nodes;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using WorkTool.Core;

namespace WorkTool.WinUI;

public sealed partial class MainWindow
{
    private static readonly double[] TransferColumns = [120, 178, 120, 0, 0, 95];
    private const int MaxTransferRows = 100;
    private JsonArray transfers = new();
    private readonly HashSet<string> imported = new();
    private readonly List<TransferRowView> transferViews = [];
    private TextBlock transferSummary = null!;
    private StackPanel transferList = null!, previewRows = null!;
    private CheckBox acknowledge = null!;
    private Button download = null!;
    private Grid transferInput = null!, transferReview = null!, transferDownload = null!;
    private Button[] transferSteps = [];
    private bool refreshing, saving;
    private int step = 1;
    private string savedSignature = "", savedThread = "", acknowledgedSignature = "";

    private sealed record TransferRowView(FrameworkElement Card, CheckBox Selection, TextBlock Status, Border Badge);

    private FrameworkElement TransferPage()
    {
        LoadTransferWidths();
        threadInputs = new StackPanel { Spacing = 12 };
        AddThreadInput(first: true);
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

    private void RenderTransfers()
    {
        refreshing = true;
        transferList.Children.Clear();
        transferViews.Clear();
        groupedEditors.Clear();
        var banks = rules.Banks;
        foreach (var row in transfers.ToArray())
        {
            var bank = new ComboBox { Header = "입금은행", ItemsSource = banks, SelectedItem = row.Text("bank"), Width = TransferWidth("bank") };
            bank.SelectionChanged += (_, _) => UpdateTransferField(row!, "bank", bank.SelectedItem?.ToString() ?? "");
            groupedEditors[(row.Text("id"), "bank")] = value => bank.SelectedItem = value;
            var account = Edit(row!, "account", "입금계좌번호", TransferWidth("account"));
            var amount = Edit(row!, "amount", "입금액(원)", TransferWidth("amount"));
            amount.TextAlignment = TextAlignment.Right;
            var plate = Edit(row!, "plate", "차량번호", TransferWidth("plate"));
            var main = WrapActions(bank, account, amount, plate);
            var id = row.Text("id");
            var pick = new CheckBox { Content = $"{transferViews.Count + 1}번 선택", IsChecked = selectedTransfers.Contains(id) };
            pick.Checked += (_, _) => selectedTransfers.Add(id);
            pick.Unchecked += (_, _) => selectedTransfers.Remove(id);
            var remove = Button("삭제", () => { selectedTransfers.Remove(id); transfers.Remove(row); RenderTransfers(); });
            var memo = WrapActions(Edit(row!, "outMemo", "출금통장 표시", TransferWidth("outMemo")),
                Edit(row!, "inMemo", "입금통장 표시", TransferWidth("inMemo")), Edit(row!, "cms", "CMS코드", TransferWidth("cms")));
            var status = Text("");
            var statusTag = StatusTag(status);
            var original = new TextBox { Header = "Slack 원문", Text = row.Text("ctx").Length > 0 ? row.Text("ctx") : row.Text("source"), IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MaxHeight = 220 };
            var details = new Expander { Header = "예금주·메모·CMS 및 원문", Content = Stack(Edit(row!, "holder", "예금주", TransferWidth("holder")),
                memo, original), HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch };
            pick.VerticalAlignment = remove.VerticalAlignment = status.VerticalAlignment = VerticalAlignment.Center;
            var cardContent = Stack(main, WrapControls(center: true, pick, statusTag, remove), details);
            cardContent.Spacing = 10;
            var card = Card(cardContent);
            transferViews.Add(new TransferRowView(card, pick, status, statusTag));
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
        for (var i = 0; i < checks.Count && i < transferViews.Count; i++)
        {
            var (card, _, status, tag) = transferViews[i];
            card.Visibility = ShowTransfer(transfers[i], checks[i].Text("state")) ? Visibility.Visible : Visibility.Collapsed;
            status.Text = string.Join(" · ", checks[i]!["errors"]!.AsArray().Concat(checks[i]!["reviews"]!.AsArray()).Select(x => x!.ToString()));
            if (status.Text.Length == 0) status.Text = "확인 완료";
            TintTag(tag, status, checks[i].Text("state") switch
            {
                "bad" => errorTone,
                "warn" => attentionTone,
                _ => successTone
            });
        }
        download.IsEnabled = !saving && transfers.Count is > 0 and <= MaxTransferRows && bad == 0 && (warn == 0 || acknowledge.IsChecked == true);
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
        var signature = Signature;
        var needsReview = checks.Any(x => x.Text("state") == "warn");
        bool IsReviewed() => !needsReview || (acknowledge.IsChecked == true && acknowledgedSignature == signature);
        if (transfers.Count is 0 or > MaxTransferRows || checks.Any(x => x.Text("state") == "bad") || !IsReviewed())
            throw new InvalidOperationException("오류를 수정하고 검토 항목을 확인해 주세요.");
        saving = true;
        try
        {
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
            Notify("이체파일을 저장했어요. 은행 업로드 전에 계좌와 금액을 확인해 주세요.");
        }
        finally { saving = false; ValidateTransfers(); }
    }
    private async Task ResetTransfers()
    {
        if (await Dialog("전체 초기화", Text("붙여넣은 스레드와 이체 내역을 모두 지울까요?"), "모두 지우기").ShowAsync() != ContentDialogResult.Primary) return;
        transfers.Clear(); imported.Clear(); selectedTransfers.Clear(); threadBoxes.Clear(); threadInputs.Children.Clear();
        AddThreadInput(first: true);
        savedSignature = ""; RenderTransfers(); Step(1);
    }
}
