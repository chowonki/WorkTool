using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;
using WorkTool.Core;

namespace WorkTool.WinUI;

public sealed partial class MainWindow
{
    private StackPanel threadInputs = null!;
    private readonly List<TextBox> threadBoxes = [];
    private readonly HashSet<string> selectedTransfers = [];
    private readonly Dictionary<(string Id, string Field), Action<string>> groupedEditors = new();
    private ComboBox transferColumn = null!;
    private SelectorBar transferSeg = null!;
    private int transferFilterIndex;
    private TextBox transferSearch = null!;
    private readonly Dictionary<int, string> transferSearches = [];
    private bool loadingSearch;
    private CheckBox autoFixAccounts = null!;
    private string ThreadSignature => JsonSerializer.Serialize(threadBoxes.Select(box => box.Text));
    private static readonly (string Key, string Label)[] TransferFields =
        [("bank", "은행"), ("account", "계좌번호"), ("holder", "예금주"), ("amount", "금액"), ("plate", "차량번호"),
         ("outMemo", "출금 표시"), ("inMemo", "입금 표시"), ("cms", "CMS코드")];
    private static readonly double[] DefaultTransferWidths = [155, 190, 205, 146, 125, 180, 180, 140];
    private readonly double[] transferWidths = (double[])DefaultTransferWidths.Clone();

    private double TransferWidth(string key) => transferWidths[Array.FindIndex(TransferFields, field => field.Key == key)];
    private static string TransferWidthsFile => LocalState.DataFile("transfer-columns.json");

    private void LoadTransferWidths()
    {
        try
        {
            if (!File.Exists(TransferWidthsFile)) return;
            var widths = JsonSerializer.Deserialize<double[]>(File.ReadAllText(TransferWidthsFile));
            if (widths?.Length == transferWidths.Length && widths.All(width => width is >= 80 and <= 480))
                widths.CopyTo(transferWidths, 0);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException) { }
    }

    private async Task EditTransferWidths()
    {
        var boxes = TransferFields.Select((field, index) => new NumberBox
        {
            Header = field.Label, Value = transferWidths[index], Minimum = 80, Maximum = 480, SmallChange = 8,
            SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Compact
        }).ToArray();
        var content = Stack();
        foreach (var group in boxes.Chunk(2)) content.Children.Add(Columns([0, 0], group));
        var dialog = Dialog("이체 내역 열 너비", content, "적용");
        dialog.SecondaryButtonText = "기본값";
        var result = await dialog.ShowAsync();
        if (result == ContentDialogResult.None) return;
        if (result == ContentDialogResult.Secondary) DefaultTransferWidths.CopyTo(transferWidths, 0);
        else
            for (var i = 0; i < boxes.Length; i++)
                if (!double.IsNaN(boxes[i].Value)) transferWidths[i] = Math.Clamp(boxes[i].Value, 80, 480);
        BuildTransferHeader();
        RenderTransfers();
        Directory.CreateDirectory(Path.GetDirectoryName(TransferWidthsFile)!);
        await File.WriteAllTextAsync(TransferWidthsFile, JsonSerializer.Serialize(transferWidths));
    }

    // 이체 요청 입력칸: 상자 없이 입력칸 사이를 구분선으로 나눠요.
    private TextBox AddThreadInput(bool first = false)
    {
        var box = Input("", "차량번호, 은행, 계좌번호, 금액이 포함된 Slack 스레드를 그대로 붙여넣으세요.");
        box.Header = null;
        box.Tag = Guid.NewGuid().ToString("N");
        box.AcceptsReturn = true;
        box.TextWrapping = TextWrapping.Wrap;
        box.Height = first ? 260 : 180;
        box.MaxLength = 50_000;
        var title = PanelHeading($"이체 요청 {threadBoxes.Count + 1}");
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(box, $"이체 요청 {threadBoxes.Count + 1} 내용");
        var head = new Grid();
        head.Children.Add(title);
        var section = Stack(head, box);
        section.Spacing = 8;
        var border = new Border { Child = section, Padding = new Thickness(0, first ? 0 : 16, 0, 16), BorderBrush = divider, BorderThickness = new Thickness(0, first ? 0 : 1, 0, 0) };
        if (!first)
        {
            var remove = new HyperlinkButton { Content = "이 입력칸 삭제", HorizontalAlignment = HorizontalAlignment.Right, Padding = new Thickness(0) };
            remove.Click += (_, _) =>
            {
                threadBoxes.Remove(box);
                threadInputs.Children.Remove(border);
            };
            head.Children.Add(remove);
        }
        threadBoxes.Add(box);
        threadInputs.Children.Add(border);
        return box;
    }

    // 2단계 위: Pivot 모양 보기(전체 n | 정상 n | 검토 필요 n | 오류 n) + 열 검색
    private UIElement TransferFilters()
    {
        transferSeg = new SelectorBar();
        foreach (var label in new[] { "전체", "정상", "검토 필요", "오류" }) transferSeg.Items.Add(new SelectorBarItem { Text = label });
        transferSeg.SelectedItem = transferSeg.Items[0];
        transferSeg.SelectionChanged += (_, _) =>
        {
            var index = transferSeg.Items.IndexOf(transferSeg.SelectedItem);
            if (index < 0 || index == transferFilterIndex) return;
            transferFilterIndex = index;
            ValidateTransfers();
        };
        transferColumn = new ComboBox { ItemsSource = new[] { "모든 열" }.Concat(TransferFields.Select(field => field.Label)).ToArray(), SelectedIndex = 0, Width = 120, VerticalAlignment = VerticalAlignment.Center };
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(transferColumn, "검색할 열");
        transferSearch = Input("", "내역 검색", 160);
        transferSearch.Header = null;
        transferSearch.VerticalAlignment = VerticalAlignment.Center;
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(transferSearch, "내역 검색");
        transferColumn.SelectionChanged += (_, _) =>
        {
            loadingSearch = true;
            transferSearch.Text = transferSearches.GetValueOrDefault(transferColumn.SelectedIndex, "");
            loadingSearch = false;
        };
        transferSearch.TextChanged += (_, _) =>
        {
            if (loadingSearch) return;
            var query = transferSearch.Text.Trim();
            if (query.Length == 0) transferSearches.Remove(transferColumn.SelectedIndex);
            else transferSearches[transferColumn.SelectedIndex] = query;
            ValidateTransfers();
        };
        return WrapControls(center: true, transferSeg, transferColumn, transferSearch);
    }

    private void UpdateTransferSeg(int all, int ok, int warn, int bad)
    {
        if (transferSeg is null) return;
        var values = new[] { all, ok, warn, bad };
        for (var i = 0; i < 4; i++)
            ((SelectorBarItem)transferSeg.Items[i]).Text = $"{new[] { "전체", "정상", "검토 필요", "오류" }[i]} {values[i]:N0}";
    }

    private void ClearTransferFilters()
    {
        transferSearches.Clear();
        transferSearch.Text = "";
        transferColumn.SelectedIndex = 0;
        transferFilterIndex = 0;
        transferSeg.SelectedItem = transferSeg.Items[0];
        ValidateTransfers();
    }
    private bool ShowTransfer(JsonNode? row, string state)
    {
        var filterState = transferFilterIndex switch { 1 => "ok", 2 => "warn", 3 => "bad", _ => "" };
        if (filterState.Length > 0 && filterState != state) return false;
        return transferSearches.All(filter => filter.Key <= 0
            ? TransferFields.Any(field => row.Text(field.Key).Contains(filter.Value, StringComparison.OrdinalIgnoreCase))
            : row.Text(TransferFields[filter.Key - 1].Key).Contains(filter.Value, StringComparison.OrdinalIgnoreCase));
    }

    private async Task DeleteSelectedTransfers()
    {
        if (selectedTransfers.Count == 0) { Notify("삭제할 내역을 선택해 주세요.", true); return; }
        if (await Dialog("선택한 이체 내역 삭제", Text($"{selectedTransfers.Count:N0}건을 삭제할까요?"), "삭제").ShowAsync() != ContentDialogResult.Primary) return;
        foreach (var row in transfers.Where(row => selectedTransfers.Contains(row.Text("id"))).ToArray()) transfers.Remove(row);
        selectedTransfers.Clear();
        RenderTransfers();
    }

    private void SelectVisibleTransfers()
    {
        foreach (var view in transferViews)
            if (view.Card.Visibility == Visibility.Visible) view.Selection.IsChecked = true;
    }

    private string NormalizeTransferValue(string field, string value)
    {
        value = value.Trim();
        if (field == "account" && autoFixAccounts.IsChecked == true)
            return Regex.Replace(value, @"[\s-]+", "");
        if (field == "plate") return Regex.Replace(value, @"\s+", "");
        if (field != "amount") return value;
        var numeric = Regex.Match(value, @"^₩?\s*(?<whole>[0-9]+|[0-9]{1,3}(?:,[0-9]{3})+)(?:\.0+)?\s*원?$");
        if (numeric.Success)
        {
            var whole = numeric.Groups["whole"].Value.Replace(",", "").TrimStart('0');
            return whole.Length > 0 ? whole : "0";
        }
        // The Slack parser accepts surrounding prose. Manual edits must keep
        // ambiguous separators and other input intact so validation can flag them.
        if (!Regex.IsMatch(value, @"^₩?[0-9영공일이삼사오육칠팔구십백천만억,\s]+원?$") ||
            !value.Any(character => character is '십' or '백' or '천' or '만' or '억'))
            return value;
        var parsed = rules.ParseTransferAmount(value);
        return parsed.Length > 0 ? parsed : value;
    }

    private void UpdateTransferField(JsonNode row, string field, string value)
    {
        if (refreshing || row.Text(field) == value) return;
        row[field] = value;
        var group = row.Text("splitGroup");
        if (group.Length > 0 && field != "amount")
        {
            refreshing = true;
            try
            {
                foreach (var sibling in transfers.Where(item => !ReferenceEquals(item, row) && item.Text("splitGroup") == group))
                {
                    sibling![field] = value;
                    if (groupedEditors.TryGetValue((sibling.Text("id"), field), out var setValue)) setValue(value);
                }
            }
            finally { refreshing = false; }
        }
        ValidateTransfers();
    }

    private void CommitTransferEdit(JsonNode row, string field, TextBox box)
    {
        if (refreshing || !transfers.Contains(row)) return;
        var normalized = NormalizeTransferValue(field, box.Text);
        UpdateTransferField(row, field, normalized);
        if (box.Text != normalized) box.Text = normalized;
        if (field != "amount" || !long.TryParse(normalized, out var amount) || amount <= 100_000_000) return;
        var parts = rules.Split(new JsonArray(row.DeepClone()));
        if (parts.Count + transfers.Count - 1 > MaxTransferRows || parts.Count == 1)
        {
            Notify("금액을 나눌 수 없어요. 금액과 최대 100건 제한을 확인해 주세요.", true);
            return;
        }
        var index = transfers.IndexOf(row);
        transfers.RemoveAt(index);
        for (var i = 0; i < parts.Count; i++) transfers.Insert(index + i, parts[i]!.DeepClone());
        RenderTransfers();
    }

    private void AddTransferShortcuts(Grid page)
    {
        void Shortcut(VirtualKey key, VirtualKeyModifiers modifiers, Func<Task> action)
        {
            var shortcut = new KeyboardAccelerator { Key = key, Modifiers = modifiers, ScopeOwner = page };
            shortcut.Invoked += async (_, args) =>
            {
                args.Handled = true;
                try { await action(); }
                catch (Exception error) { Notify(error.Message, true); }
            };
            page.KeyboardAccelerators.Add(shortcut);
        }
        Task Act(Action action) { action(); return Task.CompletedTask; }
        const VirtualKeyModifiers altShift = VirtualKeyModifiers.Menu | VirtualKeyModifiers.Shift;
        Shortcut(VirtualKey.Enter, VirtualKeyModifiers.Control, () => step switch
        {
            1 => Act(ExtractTransfer),
            2 => Act(() => Step(3)),
            3 when download.IsEnabled => SaveTransfer(),
            _ => Task.CompletedTask
        });
        Shortcut(VirtualKey.Enter, VirtualKeyModifiers.Control | VirtualKeyModifiers.Shift, () => Act(() => { if (step > 1) Step(step - 1); }));
        Shortcut(VirtualKey.N, altShift, () => Act(() => { if (step == 1) AddThreadInput().Focus(FocusState.Programmatic); else if (step == 2) AddTransfer(); }));
        Shortcut(VirtualKey.C, altShift, () => Act(() => { if (step == 3) Copy(rules.Comments(transfers)); }));
        Shortcut(VirtualKey.D, altShift, () => step == 2 ? DeleteSelectedTransfers() : Task.CompletedTask);
        Shortcut(VirtualKey.K, altShift, ShowTransferShortcuts);
    }

    private async Task ShowTransferShortcuts()
    {
        await Dialog("이체파일 단축키", Text(
            "Ctrl+Enter  추출·다음·저장\nCtrl+Shift+Enter  이전 단계\nAlt+Shift+N  입력칸·이체 내역 추가\nAlt+Shift+C  코멘트 전체 복사\nAlt+Shift+D  선택한 내역 삭제\nAlt+Shift+K  단축키 안내"),
            "확인").ShowAsync();
    }

    private async Task ShowTransferComments()
    {
        var list = new StackPanel { Spacing = 8 };
        foreach (var line in rules.Comments(transfers).Split(Environment.NewLine, StringSplitOptions.RemoveEmptyEntries))
            list.Children.Add(Columns([0, 70], Text(line), Button("복사", () => Copy(line))));
        if (list.Children.Count == 0) list.Children.Add(Text("이체 내역이 없어요."));
        var dialog = Dialog("송금 코멘트", new ScrollViewer { Content = list, MaxHeight = 440 }, "닫기");
        await dialog.ShowAsync();
    }
}
