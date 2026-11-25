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
            ShowDispatchStage(DispatchStage.Save);
            Assert(dispatchStage == DispatchStage.Input && !dispatchSteps[(int)DispatchStage.Save].IsEnabled,
                "Dispatch starts at input and gates result actions until lookup");
            await SetVehicleText("123가4567\r\n123가4567\r\n45나6789\r\n확인 필요");
            Assert(vehicleCount.Text == "입력 차량 2대 · 중복 1줄 · 확인 필요 1줄", $"Vehicle input counts unique valid plates ({vehicleCount.Text})");
            await SetVehicleText("");
            Assert(vehicleCount.Text == "입력 차량 0대", "Clearing vehicle input resets the count");
            await SetVehicleText(await File.ReadAllTextAsync(Path.Combine(fixtures, "차량목록.txt")));
            foreach (var path in Directory.GetFiles(fixtures, "*.xlsx")) files.Add(rules.ReadSheets(Spreadsheets.Read(path), Path.GetFileName(path)));
            QueryDispatch(); Assert(results.Count > 0, "Native dispatch renders synthetic vendor files");
            Assert(dispatchStage == DispatchStage.Review && dispatchInputStage.Visibility == Microsoft.UI.Xaml.Visibility.Collapsed
                && dispatchSaveActions.Visibility == Microsoft.UI.Xaml.Visibility.Collapsed, "Successful lookup opens the review stage");
            var firstVehicleId = results[0].Text("id");
            selected.Add(firstVehicleId);
            await SetTextAndWait(search, results[0].Text("plate"));
            ShowDispatchStage(DispatchStage.Save);
            Assert(dispatchStage == DispatchStage.Save && dispatchSaveActions.Visibility == Microsoft.UI.Xaml.Visibility.Visible
                && VisibleResults().Count == 1 && selected.Contains(firstVehicleId), "Save stage retains selected rows and search");
            ShowDispatchStage(DispatchStage.Review);
            await SetTextAndWait(search, "");
            selected.Clear();
            for (var i = 0; i < results.Count; i++)
                foreach (var field in new[] { "schedule", "fee" })
                    if (results[i]!.Flag(field + "Conflict")) results[i] = rules.Choose(results[i]!, field, results[i]![field + "Candidates"]![0].Text("id"));
            RenderDispatch();
            var allMatches = VisibleResults().Count;
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
            ShowDispatchStage(DispatchStage.Input);
            await SetVehicleText("123가4567");
            Assert(dispatchStage == DispatchStage.Input && results.Count == 0
                && !dispatchSteps[(int)DispatchStage.Review].IsEnabled, "Editing source invalidates prior dispatch results");
            await SetVehicleText(await File.ReadAllTextAsync(Path.Combine(fixtures, "차량목록.txt")));
            QueryDispatch();
            ShowDispatchStage(DispatchStage.Slack);
            await SetTextAndWait(slackReplies, "08구8701 - X\n77다1234 - 확인 중");
            ApplySlackReplies();
            Assert(slackDispatchStage.Visibility == Microsoft.UI.Xaml.Visibility.Visible
                && slackReplyReport.Text.StartsWith("2대 반영")
                && results.Any(row => row.Text("plate") == "08구8701" && row.Text("amount") == "0")
                && results.Any(row => row.Text("plate") == "77다1234" && Money(row, "amount") == "확인 중"),
                "Slack reply stage applies zero and retains pending in dispatch results");
            Root.RequestedTheme = Microsoft.UI.Xaml.ElementTheme.Dark;
            Assert(Root.RequestedTheme == Microsoft.UI.Xaml.ElementTheme.Dark, "Dark theme applies");
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
