using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using WorkTool.Core;

namespace WorkTool.WinUI;

public sealed partial class MainWindow
{
    private string dispatchVendor = "", dispatchState = "";
    private readonly Dictionary<string, string> dispatchSearches = [];
    private DateTimeOffset? dispatchFrom, dispatchTo;
    private string dispatchSort = "";
    private bool dispatchDescending;
    private StackPanel dispatchFiles = null!;
    private bool importingDispatch;

    private (string Key, string Label)[] DispatchFields =>
        [("plate", "차량번호"), ("schedule", "탁송일정"), ("amount", "주유·충전비"), ("dispatchFee", "탁송비"),
         ("vat", "부가세"), ("status", "조회 상태"), ("vendor", "업체"), ("origin", "출발지"), ("notes", "메모"),
         ("meaning", "비용 구분"), ("carrierType", "탁송 구분")];

    private static string DispatchWidthsFile => LocalState.DataFile("dispatch-columns.json");
    private const string WorkMarksFile = "dispatch-work-marks.json";

    // 복사함·완료 표시는 오늘 날짜로만 이 기기에 남겨요. 앱을 다시 열고 같은 목록을 조회해도 이어서 볼 수 있어요.
    private void LoadWorkMarks()
    {
        var saved = LocalState.ReadToday(WorkMarksFile);
        copiedDispatch.UnionWith(LocalState.Strings(saved["copied"]));
        completed.UnionWith(LocalState.Strings(saved["done"]));
    }

    private void SaveWorkMarks()
    {
        if (copiedDispatch.Count == 0 && completed.Count == 0) { LocalState.Delete(WorkMarksFile); return; }
        LocalState.WriteToday(WorkMarksFile, new JsonObject
        {
            ["copied"] = new JsonArray(copiedDispatch.Select(x => (JsonNode?)x).ToArray()),
            ["done"] = new JsonArray(completed.Select(x => (JsonNode?)x).ToArray())
        });
    }

    private void LoadDispatchWidths()
    {
        try
        {
            if (!File.Exists(DispatchWidthsFile)) return;
            var saved = JsonSerializer.Deserialize<double[]>(File.ReadAllText(DispatchWidthsFile));
            if (saved?.Length == dispatchColumns.Length && saved.All(width => width == 0 || width is >= 48 and <= 480))
                saved.CopyTo(dispatchColumns, 0);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException) { }
    }

    private void UpdateDispatchHeader()
    {
        if (dispatchTable.Children.Count > 1) dispatchTable.Children.RemoveAt(0);
        dispatchTable.MinWidth = Math.Max(1020, dispatchColumns.Sum() + 120);
        dispatchTable.Children.Insert(0, TableHeader(dispatchColumns, DispatchHeadings));
    }

    private async Task EditDispatchWidths()
    {
        var inputs = DispatchHeadings.Select((label, index) => new NumberBox
        {
            Header = label, Value = dispatchColumns[index], Minimum = index == 1 ? 0 : 48, Maximum = 480,
            SmallChange = 8, SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Compact
        }).ToArray();
        var content = Stack(Text("픽셀 단위로 조절할 수 있어요. 탁송일정 열의 0은 자동 너비예요."));
        for (var i = 0; i < inputs.Length; i += 3) content.Children.Add(Columns([0, 0, 0], inputs.Skip(i).Take(3).ToArray()));
        var dialog = Dialog("탁송 표 열 너비", content, "적용");
        dialog.SecondaryButtonText = "기본값";
        var result = await dialog.ShowAsync();
        if (result == ContentDialogResult.None) return;
        if (result == ContentDialogResult.Secondary) DefaultDispatchColumns.CopyTo(dispatchColumns, 0);
        else
            for (var i = 0; i < inputs.Length; i++)
                if (!double.IsNaN(inputs[i].Value)) dispatchColumns[i] = i == 1 && inputs[i].Value == 0 ? 0 : Math.Clamp(inputs[i].Value, 48, 480);
        UpdateDispatchHeader();
        RenderDispatch();
        Directory.CreateDirectory(Path.GetDirectoryName(DispatchWidthsFile)!);
        await File.WriteAllTextAsync(DispatchWidthsFile, JsonSerializer.Serialize(dispatchColumns));
    }

    private void CleanVehicles()
    {
        try
        {
            var input = rules.ParseVehicles(vehicles.Text);
            if (input["invalid"]!.AsArray().Count > 0)
            {
                Notify("확인 필요로 표시된 줄을 고친 후 목록을 정리해 주세요.", true);
                return;
            }
            vehicles.Text = input.Text("cleaned");
        }
        catch (Exception error) { Notify(error.Message, true); }
    }

    private void RenderDispatchFiles()
    {
        // 비어 있으면 안내 글자만, 있으면 파일명·업체·건수와 빼기(x) 단추
        fileSummary.Visibility = files.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        dispatchFiles.Children.Clear();
        var first = true;
        foreach (var file in files.ToArray())
        {
            var remove = new Button { Content = new FontIcon { Glyph = "\uE711", FontSize = 11 }, Padding = new Thickness(7), Background = transparent, BorderThickness = new Thickness(0), VerticalAlignment = VerticalAlignment.Center };
            remove.Click += (_, _) =>
            {
                if (importingDispatch) return;
                files.Remove(file);
                RenderDispatchFiles();
                InvalidateDispatch();
            };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(remove, file.Text("name") + " 빼기");
            ToolTipService.SetToolTip(remove, "빼기");
            var records = file!["records"]!.AsArray();
            var vendors = string.Join(" · ", records.Select(r => VendorName(r.Text("vendor"))).Where(v => v.Length > 0).Distinct());
            var name = Text(file.Text("name"), 13);
            name.TextWrapping = TextWrapping.NoWrap; name.TextTrimming = TextTrimming.CharacterEllipsis;
            ToolTipService.SetToolTip(name, file.Text("name"));
            var info = Stack(name, Muted($"{(vendors.Length > 0 ? vendors + " · " : "")}{records.Count:N0}건"));
            info.Spacing = 0;
            var warnings = LocalState.Strings(file["warnings"]).ToArray();
            if (warnings.Length > 0)
            {
                var warning = Text($"⚠ 양식 확인 필요 · {warnings.Length}건", 12);
                warning.Foreground = orange.Solid;
                ToolTipService.SetToolTip(warning, string.Join(Environment.NewLine, warnings));
                info.Children.Add(warning);
            }
            // 파일 아이콘: 24px 색 타일 (녹색·파랑·purple·주황 순환)
            Swatch[] fileColors = [green, blue, purple, orange];
            var icon = Tile(fileColors[dispatchFiles.Children.Count % 4], "\uE9F9", 24, 13, 6);
            var line = Columns([24, 0, 32], icon, info, remove);
            line.Padding = new Thickness(0, 6, 0, 6);
            line.ColumnSpacing = 8;
            dispatchFiles.Children.Add(new Border { Child = line, BorderBrush = divider, BorderThickness = new Thickness(0, first ? 0 : 1, 0, 0) });
            first = false;
        }
    }

    private static string VendorName(string vendor) => vendor switch { "A" => "한빛탁송", "B" => "오픈마일", "C" => "렌톡", _ => vendor };
    private void InvalidateDispatch()
    {
        if (results.Count == 0) return;
        results.Clear();
        selected.Clear();
        ClearSlackReplies();
        ShowSlackPanel(false);
        RecountDispatch();
        RenderDispatch();
    }

    private async Task ResetDispatch()
    {
        if (importingDispatch) return;
        if ((files.Count > 0 || vehicles.Text.Length > 0) &&
            await Dialog("탁송체크 전체 초기화", Text("차량목록과 첨부 파일을 모두 비울까요?"), "초기화").ShowAsync() != ContentDialogResult.Primary) return;
        files.Clear();
        completed.Clear();
        copiedDispatch.Clear();
        SaveWorkMarks();
        vehicles.Text = "";
        search.Text = "";
        dispatchVendor = dispatchState = dispatchSort = "";
        dispatchSearches.Clear();
        dispatchFrom = dispatchTo = null;
        dispatchDescending = false;
        scope.SelectedIndex = 0;
        dispatchFilter = "all";
        InvalidateDispatch();
        RenderDispatchFiles();
        RecountDispatch();
        RenderDispatch();
        Go("dispatch");
    }

    private JsonArray SelectedOrVisibleDispatch() => selected.Count == 0
        ? VisibleResults()
        : new JsonArray(results.Where(row => selected.Contains(row.Text("id"))).Select(row => row!.DeepClone()).ToArray());

    private void CopyDispatchNumbers()
    {
        var rows = SelectedOrVisibleDispatch().DistinctBy(row => row.Text("plate")).ToArray();
        if (rows.Length == 0) { Notify("현재 목록에 복사할 차량이 없어요.", true); return; }
        void MarkCopied()
        {
            copiedDispatch.UnionWith(rows.Select(row => row.Text("plate")));
            SaveWorkMarks();
            RenderDispatch();
        }
        if (dispatchFilter != "slack")
        {
            Copy(string.Join(Environment.NewLine, rows.Select(row => row.Text("plate"))), MarkCopied);
            return;
        }

        var groups = new[] { (true, "제주도출발차량"), (false, "일반차량") };
        var sections = groups.Select(group =>
        {
            var plates = rows.Where(row => row!.Flag("jeju") == group.Item1).Select(row => row.Text("plate")).ToArray();
            return plates.Length == 0 ? "" : $"[{group.Item2}]{Environment.NewLine}{string.Join(Environment.NewLine, plates)}";
        });
        Copy(string.Join(Environment.NewLine + Environment.NewLine, sections.Where(section => section.Length > 0)), MarkCopied);
    }

    private static ComboBox Options(string label, IEnumerable<string> values, string current)
    {
        var items = new[] { "전체" }.Concat(values.Where(value => value.Length > 0).Distinct().Order()).ToArray();
        return new ComboBox { Header = label, ItemsSource = items, SelectedItem = items.Contains(current) ? current : items[0] };
    }

    private async Task EditDispatchFilters()
    {
        var vendor = Options("업체", results.Select(row => row.Text("vendor")), dispatchVendor);
        var state = Options("원본 상태", results.Select(row => row.Text("rawState")), dispatchState);
        var from = new CalendarDatePicker { Header = "탁송일정 시작일", Date = dispatchFrom };
        var to = new CalendarDatePicker { Header = "탁송일정 종료일", Date = dispatchTo };
        var fields = DispatchFields;
        var searches = fields.ToDictionary(field => field.Key, field =>
        {
            var box = Input(field.Label, "검색어");
            box.Text = dispatchSearches.GetValueOrDefault(field.Key, "");
            return box;
        });
        var columnInputs = Stack();
        foreach (var pair in fields.Chunk(2)) columnInputs.Children.Add(Columns([0, 0], pair.Select(field => searches[field.Key]).ToArray()));
        var sortFields = new[] { "입력 순서" }.Concat(fields.Select(field => field.Label)).ToArray();
        var sort = new ComboBox { Header = "정렬 기준", ItemsSource = sortFields, SelectedIndex = Array.FindIndex(fields, field => field.Key == dispatchSort) + 1 };
        var descending = new CheckBox { Content = "내림차순", IsChecked = dispatchDescending };
        var columnFilters = new Expander { Header = "열별 검색", Content = columnInputs,
            IsExpanded = dispatchSearches.Count > 0, HorizontalContentAlignment = HorizontalAlignment.Stretch };
        var dialog = Dialog("탁송 결과 필터·정렬", new ScrollViewer
        {
            Content = Stack(Bar(vendor, state), Bar(from, to), Bar(sort, descending), columnFilters), MaxHeight = 460
        }, "적용");
        dialog.SecondaryButtonText = "초기화";
        var result = await dialog.ShowAsync();
        if (result == ContentDialogResult.None) return;
        if (result == ContentDialogResult.Secondary)
        {
            dispatchVendor = dispatchState = dispatchSort = "";
            dispatchSearches.Clear();
            dispatchFrom = dispatchTo = null;
            dispatchDescending = false;
        }
        else
        {
            if (from.Date > to.Date) { Notify("종료일은 시작일과 같거나 이후여야 해요.", true); return; }
            dispatchVendor = vendor.SelectedIndex > 0 ? (string)vendor.SelectedItem : "";
            dispatchState = state.SelectedIndex > 0 ? (string)state.SelectedItem : "";
            dispatchFrom = from.Date;
            dispatchTo = to.Date;
            dispatchSearches.Clear();
            foreach (var (key, box) in searches)
                if (box.Text.Trim() is { Length: > 0 } query) dispatchSearches[key] = query;
            dispatchSort = sort.SelectedIndex > 0 ? fields[sort.SelectedIndex - 1].Key : "";
            dispatchDescending = descending.IsChecked == true;
        }
        dispatchPage = 0;
        SyncDispatchBoxes();
        RenderDispatch();
    }

    private string DispatchValue(JsonNode row, string key) => key switch
    {
        "status" => rules.Status(row),
        "notes" => rules.Notes(row),
        _ => row.Text(key)
    };

    private bool MatchesDispatchOptions(JsonNode row)
    {
        if (dispatchVendor.Length > 0 && row.Text("vendor") != dispatchVendor) return false;
        if (dispatchState.Length > 0 && row.Text("rawState") != dispatchState) return false;
        if (dispatchSearches.Any(filter => !DispatchValue(row, filter.Key).Contains(filter.Value, StringComparison.OrdinalIgnoreCase))) return false;
        if (dispatchFrom is null && dispatchTo is null) return true;
        var schedule = row.Text("schedule");
        if (schedule.Length < 10 || !DateOnly.TryParseExact(schedule[..10], "yyyy-MM-dd", CultureInfo.InvariantCulture, DateTimeStyles.None, out var day)) return false;
        return (dispatchFrom is null || day >= DateOnly.FromDateTime(dispatchFrom.Value.Date))
            && (dispatchTo is null || day <= DateOnly.FromDateTime(dispatchTo.Value.Date));
    }

    private IEnumerable<JsonNode?> SortDispatch(IEnumerable<JsonNode?> rows)
    {
        if (dispatchSort.Length == 0) return rows;
        if (dispatchSort is "amount" or "dispatchFee" or "vat")
        {
            decimal? Number(JsonNode? row) => decimal.TryParse(row.Text(dispatchSort), NumberStyles.Any, CultureInfo.InvariantCulture, out var value) ? value : null;
            var present = rows.OrderBy(row => Number(row) is null);
            return dispatchDescending ? present.ThenByDescending(Number) : present.ThenBy(Number);
        }
        string Value(JsonNode? row) => DispatchValue(row!, dispatchSort);
        return dispatchDescending ? rows.OrderByDescending(Value) : rows.OrderBy(Value);
    }
}
