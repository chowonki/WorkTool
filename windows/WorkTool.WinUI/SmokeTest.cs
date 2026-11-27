using System.Text.Json;
using WorkTool.Core;

namespace WorkTool.WinUI;

public sealed partial class MainWindow
{
    // Explicit opt-in development check, using synthetic fixtures only. No dialogs or external services.
    private async Task SmokeTest(string output, string fixtures)
    {
        var checks = new List<string>();
        void Assert(bool passed, string label) { if (!passed) throw new InvalidOperationException(label); checks.Add(label); }
        smokeDirectory = Path.GetFullPath(output); Directory.CreateDirectory(smokeDirectory);
        try
        {
            // 화면 사진이 같은 크기로 나오도록 1440×900(배율 반영)으로 맞춰요.
            var scale = Root.XamlRoot?.RasterizationScale ?? 1;
            AppWindow.Resize(new Windows.Graphics.SizeInt32((int)(1440 * scale), (int)(900 * scale)));
            await Task.Delay(400);
            await Screenshot("home");
            Go("transfer");
            threadBoxes[0].Text = "123가4567\r국민은행\r001234567890\r3만5천원";
            ExtractTransfer();
            Assert(transfers.Count == 1 && transfers[0].Text("amount") == "35000" && transfers[0].Text("plate") == "123가4567", "WinUI multiline input extracts correctly");
            ExtractTransfer(); Assert(transfers.Count == 1, "Repeated extraction does not duplicate transfer");
            Step(3); Assert(download.IsEnabled, "Valid transfer enables download");
            transfers[0]!["amount"] = "0"; ValidateTransfers(); Assert(!download.IsEnabled, "Zero amount blocks download");
            transfers[0]!["amount"] = "100000000"; RenderTransfers(); Assert(!download.IsEnabled, "Large amount requires review");
            acknowledge.IsChecked = true; Assert(download.IsEnabled, "Review enables download");
            transfers[0]!["inMemo"] = "수정"; ValidateTransfers(); Assert(acknowledge.IsChecked == false && !download.IsEnabled, "Editing invalidates review");
            transfers[0]!["amount"] = "35000"; RenderTransfers(); Step(3); await SaveTransfer();
            Assert(Directory.GetFiles(smokeDirectory, "하나은행*.xlsx").Length == 1, "Native transfer export writes Excel");
            var secondThread = AddThreadInput();
            secondThread.Text = "45나6789\r(정산 계좌)\r신한은행\r987654321012\r예금주: 테스트\r2만원";
            ExtractTransfer();
            Assert(transfers.Count == 2 && transfers[1].Text("holder") == "테스트"
                && transfers[1].Text("label") == "정산 계좌", "Second Slack input preserves holder and remittance context");
            ShowTransferDetail(transfers[1]!);
            await Screenshot("transfer-step2");
            Assert(NormalizeTransferValue("amount", "3만 5천원") == "35000"
                && NormalizeTransferValue("account", "001-234 567890") == "001234567890", "Manual amount and account edits follow web formatting");
            var malformed = NormalizeTransferValue("amount", "35,00원");
            var malformedRows = new System.Text.Json.Nodes.JsonArray(transfers[0]!.DeepClone());
            malformedRows[0]!["amount"] = malformed;
            Assert(malformed == "35,00원" && rules.Check(malformedRows)[0].Text("state") == "bad"
                && NormalizeTransferValue("amount", "35,000.00원") == "35000", "Malformed amounts remain invalid while standard formats normalize");
            await SetTextAndWait(transferSearch, "987654321012");
            Assert(transferViews.Count(view => view.Card.Visibility == Microsoft.UI.Xaml.Visibility.Visible) == 1, "Transfer search filters review rows");
            await SetTextAndWait(transferSearch, "");
            Go("dispatch");
            Assert(results.Count == 0 && dispatchEmpty.Visibility == Microsoft.UI.Xaml.Visibility.Visible
                && dispatchInputPanel.Visibility == Microsoft.UI.Xaml.Visibility.Visible && navCounts["d:all"].Text == "",
                "Dispatch starts with the input panel, an empty table and no navigation counts");
            await SetVehicleText("123가4567\r\n123가4567\r\n45나6789\r\n확인 필요");
            Assert(vehicleCount.Text == "2대 인식 · 중복 1줄 · 확인 필요 1줄", $"Vehicle input counts unique valid plates ({vehicleCount.Text})");
            await SetVehicleText("");
            Assert(vehicleCount.Text == "0대 인식", "Clearing vehicle input resets the count");
            const string pasted = "\n\n12가3456\n\n  \n34나5678\r\n\r\n56다7890\n\n";
            Assert(RemoveBlankLines(pasted) == "12가3456\n34나5678\n56다7890\n", "Blank-line cleanup keeps one trailing newline");
            await SetVehicleText(pasted);
            await Task.Delay(200);
            Assert(NormalizeLines(vehicles.Text) == "12가3456\n34나5678\n56다7890\n", $"Pasting removes blank lines ({NormalizeLines(vehicles.Text).Replace("\n", "⏎")})");
            await SetVehicleText(vehicles.Text + "\r");
            await Task.Delay(200);
            Assert(NormalizeLines(vehicles.Text) == "12가3456\n34나5678\n56다7890\n\n", "Typing a single Enter keeps the new empty line");
            await SetVehicleText("");
            await SetVehicleText(await File.ReadAllTextAsync(Path.Combine(fixtures, "차량목록.txt")));
            foreach (var path in Directory.GetFiles(fixtures, "*.xlsx")) files.Add(rules.ReadSheets(Spreadsheets.Read(path), Path.GetFileName(path)));
            await SetVehicleText(vehicles.Text + "\r\r");
            QueryDispatch(); Assert(results.Count > 0, "Native dispatch renders synthetic vendor files");
            await Task.Delay(200);
            var queried = NormalizeLines(vehicles.Text);
            Assert(results.Count > 0 && !queried.Contains("\n\n") && !queried.StartsWith('\n') && !queried.EndsWith('\n'),
                $"Lookup trims blank lines and edge newlines without clearing results (results={results.Count}, text={queried.Replace("\n", "⏎")})");
            Assert(dispatchEmpty.Visibility == Microsoft.UI.Xaml.Visibility.Collapsed && dispatchList.Children.Count > 0
                && navCounts["d:all"].Text.Length > 0 && currentNav == "d:all", "Successful lookup fills the table and the navigation counts");
            await Screenshot("dispatch");
            Select("d:confirmed");
            Assert(dispatchFilter == "confirmed" && VisibleResults().All(row => row.Text("status") == "confirmed"), "Navigation sub-item filters the table");
            Select("d:all");
            var firstVehicleId = results[0].Text("id");
            selected.Add(firstVehicleId);
            await SetTextAndWait(search, results[0].Text("plate"));
            Assert(VisibleResults().Count == 1 && selected.Contains(firstVehicleId), "Search keeps checked rows");
            await SetTextAndWait(search, "");
            selected.Clear();
            for (var i = 0; i < results.Count; i++)
                foreach (var field in new[] { "schedule", "fee" })
                    if (results[i]!.Flag(field + "Conflict")) results[i] = rules.Choose(results[i]!, field, results[i]![field + "Candidates"]![0].Text("id"));
            RenderDispatch();
            var allMatches = VisibleResults().Count;
            var savedPageSize = dispatchPageSize;
            SetDispatchPageSize(10);
            Assert(dispatchList.Children.Count == Math.Min(10, allMatches) && dispatchPage == 0, "Dispatch shows one page of the chosen size");
            if (allMatches > 10)
            {
                ChangeDispatchPage(1);
                Assert(dispatchList.Children.Count == Math.Min(10, allMatches - 10) && dispatchSummary.Text.Contains("11–"), "Dispatch moves to the next page");
            }
            SetDispatchPageSize(savedPageSize == 10 ? 30 : savedPageSize);
            Assert(dispatchPageSize != 10 && dispatchList.Children.Count == Math.Min(dispatchPageSize, allMatches), "Dispatch page size can be changed back");
            dispatchVendor = "한빛탁송";
            Assert(VisibleResults().Count < allMatches && VisibleResults().All(row => row.Text("vendor") == "한빛탁송"), "Dispatch filters by vendor");
            dispatchVendor = "";
            dispatchSort = "amount";
            dispatchDescending = true;
            var sorted = VisibleResults();
            Assert(sorted[0].Text("amount") == "20000" && sorted[^1].Text("amount") == "", "Dispatch sorts numeric values with empty values last");
            dispatchSort = "";
            dispatchDescending = false;
            await ExportDispatch(".xlsx"); await ExportDispatch(".txt");
            await CaptureDispatch(false); await CaptureDispatch(false, true);
            Assert(Directory.GetFiles(smokeDirectory, "*.png").Any(p => new FileInfo(p).Length > 1000), "Native PNG capture writes image");
            Assert(Directory.GetFiles(smokeDirectory, "*.pdf").Any(p => new FileInfo(p).Length > 1000), "Native PDF capture writes document");
            await SetVehicleText("123가4567");
            Assert(results.Count == 0 && dispatchEmpty.Visibility == Microsoft.UI.Xaml.Visibility.Visible && navCounts["d:all"].Text == "",
                "Editing source invalidates prior dispatch results");
            await SetVehicleText(await File.ReadAllTextAsync(Path.Combine(fixtures, "차량목록.txt")));
            QueryDispatch();
            dispatchVendor = "한빛탁송";
            ShowSlackPanel(true);
            Assert(SlackPanelOpen, "Slack reply panel opens in the right pane");
            await SetTextAndWait(slackReplies, "08구8701 - X\n77다1234 - 확인 중");
            ApplySlackReplies();
            var repliesInTable = VisibleResults();
            Assert(dispatchFilter == "slack-replies" && navItems["d:slack-replies"].Visibility == Microsoft.UI.Xaml.Visibility.Visible
                && slackReplyReport.Text.StartsWith("2대 반영")
                && repliesInTable.Count == 2
                && repliesInTable.Any(row => row.Text("plate") == "08구8701" && Money(row, "amount") == "0원")
                && repliesInTable.Any(row => row.Text("plate") == "77다1234" && Money(row, "amount") == "확인 중"),
                "Applying replies opens the results table and shows the updated fuel cells");
            Root.RequestedTheme = Microsoft.UI.Xaml.ElementTheme.Dark;
            Assert(Root.RequestedTheme == Microsoft.UI.Xaml.ElementTheme.Dark, "Dark theme applies");
            await Screenshot("dispatch-dark");
            Go("transfer"); Step(2);
            await Screenshot("transfer-step2-dark");
            Go("dispatch");
            await SetVehicleText(vehicles.Text + "\r12가3456");
            Assert(results.Count == 0 && dispatchFilter == "all" && navItems["d:slack-replies"].Visibility == Microsoft.UI.Xaml.Visibility.Collapsed,
                "Clearing results leaves the hidden 슬랙 회신 반영 view");
            Root.RequestedTheme = Microsoft.UI.Xaml.ElementTheme.Default;
            await File.WriteAllTextAsync(Path.Combine(smokeDirectory, "smoke-result.json"), JsonSerializer.Serialize(new { passed = true, checks }, new JsonSerializerOptions { WriteIndented = true }));
            Notify($"화면 검증 {checks.Count}개를 통과했어요.");
        }
        catch (Exception e)
        {
            await File.WriteAllTextAsync(Path.Combine(smokeDirectory, "smoke-result.json"), JsonSerializer.Serialize(new { passed = false, checks, error = e.ToString() }, new JsonSerializerOptions { WriteIndented = true }));
            Notify(e.Message, true);
        }
        finally { smokeDirectory = null; }
    }

    private Task SetVehicleText(string value) => SetTextAndWait(vehicles, value);

    // 검증용 화면 사진: 창 내용을 그대로 PNG로 남겨요. Mica는 시스템이 그려서 사진에는 단색(#F3F3F3/#202020)으로 대신 깔아요.
    private async Task Screenshot(string name)
    {
        if (smokeDirectory is null) return;
        Notice.IsOpen = false;
        var dark = Root.ActualTheme == Microsoft.UI.Xaml.ElementTheme.Dark;
        Root.Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(dark ? Windows.UI.Color.FromArgb(255, 0x20, 0x20, 0x20) : Windows.UI.Color.FromArgb(255, 0xF3, 0xF3, 0xF3));
        try
        {
            await Task.Delay(500);
            var bitmap = new Microsoft.UI.Xaml.Media.Imaging.RenderTargetBitmap();
            await bitmap.RenderAsync(Root);
            var pixels = System.Runtime.InteropServices.WindowsRuntime.WindowsRuntimeBufferExtensions.ToArray(await bitmap.GetPixelsAsync());
            var folder = Path.Combine(smokeDirectory, "screens");
            Directory.CreateDirectory(folder);
            using var stream = new Windows.Storage.Streams.InMemoryRandomAccessStream();
            var encoder = await Windows.Graphics.Imaging.BitmapEncoder.CreateAsync(Windows.Graphics.Imaging.BitmapEncoder.PngEncoderId, stream);
            encoder.SetPixelData(Windows.Graphics.Imaging.BitmapPixelFormat.Bgra8, Windows.Graphics.Imaging.BitmapAlphaMode.Premultiplied,
                (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, pixels);
            await encoder.FlushAsync();
            stream.Seek(0);
            using var file = File.Create(Path.Combine(folder, name + ".png"));
            await stream.AsStreamForRead().CopyToAsync(file);
        }
        finally { Root.ClearValue(Microsoft.UI.Xaml.Controls.Panel.BackgroundProperty); }
    }

    private static async Task SetTextAndWait(Microsoft.UI.Xaml.Controls.TextBox input, string value)
    {
        if (!input.IsLoaded)
        {
            var loaded = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            void OnLoaded(object sender, Microsoft.UI.Xaml.RoutedEventArgs args) => loaded.TrySetResult();
            input.Loaded += OnLoaded;
            try { await loaded.Task.WaitAsync(TimeSpan.FromSeconds(3)); }
            finally { input.Loaded -= OnLoaded; }
        }
        var changed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnChanged(object sender, Microsoft.UI.Xaml.Controls.TextChangedEventArgs args) => changed.TrySetResult();
        input.TextChanged += OnChanged;
        try
        {
            input.Text = value;
            await changed.Task.WaitAsync(TimeSpan.FromSeconds(3));
        }
        finally { input.TextChanged -= OnChanged; }
    }
}
