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
            thread.Text = "123가4567\r국민은행\r001234567890\r3만5천원";
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
            Go("dispatch");
            if (!vehicles.IsLoaded)
            {
                var loaded = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                void OnLoaded(object sender, Microsoft.UI.Xaml.RoutedEventArgs args) => loaded.TrySetResult();
                vehicles.Loaded += OnLoaded;
                try { await loaded.Task.WaitAsync(TimeSpan.FromSeconds(3)); }
                finally { vehicles.Loaded -= OnLoaded; }
            }
            await SetVehicleText("123가4567\r\n123가4567\r\n45나6789\r\n확인 필요");
            Assert(vehicleCount.Text == "입력 차량 2대 · 중복 1줄 · 확인 필요 1줄", $"Vehicle input counts unique valid plates ({vehicleCount.Text})");
            await SetVehicleText("");
            Assert(vehicleCount.Text == "입력 차량 0대", "Clearing vehicle input resets the count");
            vehicles.Text = await File.ReadAllTextAsync(Path.Combine(fixtures, "차량목록.txt"));
            foreach (var path in Directory.GetFiles(fixtures, "*.xlsx")) files.Add(rules.ReadSheets(Spreadsheets.Read(path), Path.GetFileName(path)));
            QueryDispatch(); Assert(results.Count > 0, "Native dispatch renders synthetic vendor files");
            for (var i = 0; i < results.Count; i++)
                foreach (var field in new[] { "schedule", "fee" })
                    if (results[i]!.Flag(field + "Conflict")) results[i] = rules.Choose(results[i]!, field, results[i]![field + "Candidates"]![0].Text("id"));
            RenderDispatch();
            await ExportDispatch(".xlsx"); await ExportDispatch(".txt");
            await CaptureDispatch(false); await CaptureDispatch(false, true);
            Assert(Directory.GetFiles(smokeDirectory, "*.png").Any(p => new FileInfo(p).Length > 1000), "Native PNG capture writes image");
            Assert(Directory.GetFiles(smokeDirectory, "*.pdf").Any(p => new FileInfo(p).Length > 1000), "Native PDF capture writes document");
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

    private async Task SetVehicleText(string value)
    {
        var changed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnChanged(object sender, Microsoft.UI.Xaml.Controls.TextChangedEventArgs args) => changed.TrySetResult();
        vehicles.TextChanged += OnChanged;
        try
        {
            vehicles.Text = value;
            await changed.Task.WaitAsync(TimeSpan.FromSeconds(3));
        }
        finally { vehicles.TextChanged -= OnChanged; }
    }
}
