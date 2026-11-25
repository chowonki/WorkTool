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
    private readonly List<FrameworkElement> transferCards = [];
    private readonly List<CheckBox> transferChecks = [];
    private readonly Dictionary<(string Id, string Field), Action<string>> groupedEditors = new();
    private ComboBox transferFilter = null!, transferColumn = null!;
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
    private static string TransferWidthsFile => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "WorkTool", "transfer-columns.json");

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
        RenderTransfers();
        Directory.CreateDirectory(Path.GetDirectoryName(TransferWidthsFile)!);
        await File.WriteAllTextAsync(TransferWidthsFile, JsonSerializer.Serialize(transferWidths));
    }

    private TextBox AddThreadInput(bool first = false)
    {
        var box = Input("Slack 스레드", "차량번호, 은행, 계좌번호, 금액이 포함된 요청을 붙여넣으세요.");
        box.Tag = Guid.NewGuid().ToString("N");
        box.AcceptsReturn = true;
        box.TextWrapping = TextWrapping.Wrap;
        box.Height = first ? 220 : 150;
        box.MaxLength = 50_000;
        var card = Stack(box);
        if (!first) card.Children.Add(Button("이 입력칸 삭제", () =>
        {
            threadBoxes.Remove(box);
            threadInputs.Children.Remove(card);
        }));
        threadBoxes.Add(box);
        threadInputs.Children.Add(card);
        return box;
    }

    private UIElement TransferFilters()
    {
        transferFilter = new ComboBox { Header = "검토 상태", ItemsSource = new[] { "전체", "정상", "검토 필요", "오류" }, SelectedIndex = 0, Width = 118 };
        transferColumn = new ComboBox { Header = "검색할 열", ItemsSource = new[] { "모든 열" }.Concat(TransferFields.Select(field => field.Label)).ToArray(), SelectedIndex = 0, Width = 120 };
        transferSearch = Input("내역 검색", "검색어", 174);
        transferFilter.SelectionChanged += (_, _) => ValidateTransfers();
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
        var clear = Button("필터 초기화", () =>
        {
            transferSearches.Clear();
            transferSearch.Text = "";
            transferFilter.SelectedIndex = transferColumn.SelectedIndex = 0;
            ValidateTransfers();
        });
        clear.VerticalAlignment = VerticalAlignment.Bottom;
        return WrapActions(transferFilter, transferColumn, transferSearch, clear);
    }

    private bool ShowTransfer(JsonNode? row, string state)
    {
        var filterState = transferFilter.SelectedIndex switch { 1 => "ok", 2 => "warn", 3 => "bad", _ => "" };
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
        for (var i = 0; i < transferCards.Count; i++)
            if (transferCards[i].Visibility == Visibility.Visible) transferChecks[i].IsChecked = true;
    }

    private string NormalizeTransferValue(string field, string value)
    {
        value = value.Trim();
        if (field == "account" && autoFixAccounts.IsChecked == true)
            return Regex.Replace(value, @"[\s-]+", "");
        if (field == "plate") return Regex.Replace(value, @"\s+", "");
        if (field != "amount") return value;
        var stripped = Regex.Replace(value, @"[,\s원₩]", "");
        if (Regex.IsMatch(stripped, @"^[0-9]+(?:\.0+)?$"))
        {
            var whole = stripped.Split('.')[0].TrimStart('0');
            return whole.Length > 0 ? whole : "0";
        }
        var parsed = rules.ParseTransferAmount(value);
        return parsed.Length > 0 ? parsed : stripped;
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
        if (box.Text != normalized) box.Text = normalized;
        if (field != "amount" || !long.TryParse(normalized, out var amount) || amount <= 100_000_000) return;
        var parts = (amount - 1) / 100_000_000 + 1;
        if (parts + transfers.Count - 1 <= 100) SplitTransfers();
        else Notify("금액을 나누면 100건을 넘어요. 다른 줄을 줄이거나 금액을 확인해 주세요.", true);
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
