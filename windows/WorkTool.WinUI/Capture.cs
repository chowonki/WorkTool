using System.Text;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.ApplicationModel.DataTransfer;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;
using WorkTool.Core;

namespace WorkTool.WinUI;

public sealed partial class MainWindow
{
    private async Task CaptureDispatch(bool clipboard, bool pdf = false)
    {
        var rows = ExportScope();
        if (rows.Count > 300) throw new InvalidOperationException("캡처는 한 번에 300대까지 가능해요. 저장 범위를 줄여 주세요.");
        var panel = new StackPanel { Width = 1380, Background = new SolidColorBrush(Colors.White), Padding = new Thickness(20), Spacing = 0 };
        var title = Text($"WorkTool 탁송체크 · {rows.Count:N0}대 · {DateTime.Now:yyyy-MM-dd HH:mm}", 22);
        title.Foreground = new SolidColorBrush(Colors.Black); title.Margin = new Thickness(0, 0, 0, 14); panel.Children.Add(title);
        void AddRow(string[] cells, bool heading = false)
        {
            var grid = new Grid { Height = 32, Background = new SolidColorBrush(heading ? Windows.UI.Color.FromArgb(255, 231, 237, 245) : Colors.White) };
            double[] widths = [130, 180, 150, 150, 120, 180, 190, 220];
            for (var j = 0; j < cells.Length; j++)
            {
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(widths[j]) });
                var cell = new TextBlock { Text = cells[j], FontSize = 13, Margin = new Thickness(6, 6, 0, 0), Foreground = new SolidColorBrush(Colors.Black), TextTrimming = TextTrimming.CharacterEllipsis };
                Grid.SetColumn(cell, j); grid.Children.Add(cell);
            }
            panel.Children.Add(grid);
        }
        AddRow(["차량번호", "탁송일정", "주유/충전비", "탁송비", "부가세", "조회 상태", "업체", "메모"], true);
        foreach (var row in rows) AddRow([row.Text("plate"), row.Text("schedule"), Money(row, "amount"), Money(row, "dispatchFee"), Money(row, "vat"), rules.Status(row!), row.Text("vendor"), rules.Notes(row!)]);
        AddRow(["합계", $"{rows.Count:N0}대", $"{Sum(rows, "amount"):N0}원", $"{Sum(rows, "dispatchFee"):N0}원", $"{Sum(rows, "vat"):N0}원", "", "", ""], true);
        var canvas = new Canvas { Width = 1, Height = 1, IsHitTestVisible = false };
        Canvas.SetLeft(panel, -20000); canvas.Children.Add(panel); Root.Children.Add(canvas);
        try
        {
            panel.Measure(new Windows.Foundation.Size(1380, double.PositiveInfinity));
            panel.Arrange(new Windows.Foundation.Rect(0, 0, 1380, panel.DesiredSize.Height)); panel.UpdateLayout();
            await Task.Delay(50);
            var bitmap = new RenderTargetBitmap(); await bitmap.RenderAsync(panel, 1380, (int)panel.ActualHeight);
            var pixels = await bitmap.GetPixelsAsync();
            using var stream = new InMemoryRandomAccessStream();
            var encoder = await BitmapEncoder.CreateAsync(pdf ? BitmapEncoder.JpegEncoderId : BitmapEncoder.PngEncoderId, stream);
            var reader = DataReader.FromBuffer(pixels); var bytes = new byte[pixels.Length]; reader.ReadBytes(bytes);
            encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Ignore, (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, bytes);
            await encoder.FlushAsync(); stream.Seek(0);
            if (clipboard)
            {
                var data = new DataPackage(); data.SetBitmap(RandomAccessStreamReference.CreateFromStream(stream)); Clipboard.SetContent(data);
                Clipboard.Flush(); Notify("결과 캡처를 클립보드에 복사했어요.");
            }
            else
            {
                var file = await SavePicker("탁송체크_" + DateTime.Now.ToString("yyyyMMdd_HHmm"), pdf ? ".pdf" : ".png");
                if (file is null) return;
                if (pdf)
                {
                    using var data = new MemoryStream(); await stream.AsStreamForRead().CopyToAsync(data);
                    await File.WriteAllBytesAsync(file.Path, ImagePdf(data.ToArray(), bitmap.PixelWidth, bitmap.PixelHeight));
                }
                else { using var output = File.Create(file.Path); await stream.AsStreamForRead().CopyToAsync(output); }
                Notify("조회 결과를 저장했어요.");
            }
        }
        finally { Root.Children.Remove(canvas); }
    }

    // Paginate a locally rendered table over landscape A4 pages. No external renderer or service.
    private static byte[] ImagePdf(byte[] jpeg, int width, int height)
    {
        using var output = new MemoryStream(); var offsets = new List<long> { 0 };
        void Write(string s) { var b = Encoding.ASCII.GetBytes(s); output.Write(b); }
        void Obj(int id, string content) { offsets.Add(output.Position); Write($"{id} 0 obj\n{content}\nendobj\n"); }
        const double pageW = 842, pageH = 595, margin = 20;
        var scale = (pageW - margin * 2) / width;
        var imageH = height * scale; var pageCount = (int)Math.Ceiling(imageH / (pageH - margin * 2));
        Write("%PDF-1.4\n");
        Obj(1, "<< /Type /Catalog /Pages 2 0 R >>");
        Obj(2, $"<< /Type /Pages /Count {pageCount} /Kids [{string.Join(" ", Enumerable.Range(0, pageCount).Select(i => $"{4 + i * 2} 0 R"))}] >>");
        offsets.Add(output.Position); Write($"3 0 obj\n<< /Type /XObject /Subtype /Image /Width {width} /Height {height} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length {jpeg.Length} >>\nstream\n"); output.Write(jpeg); Write("\nendstream\nendobj\n");
        string F(double n) => n.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture);
        for (var i = 0; i < pageCount; i++)
        {
            var id = 4 + i * 2;
            Obj(id, $"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 842 595] /Resources << /XObject << /Im0 3 0 R >> >> /Contents {id + 1} 0 R >>");
            var cmd = $"q\n20 20 802 555 re W n\n802 0 0 {F(imageH)} 20 {F(pageH - margin - imageH + i * (pageH - margin * 2))} cm\n/Im0 Do\nQ\n";
            Obj(id + 1, $"<< /Length {Encoding.ASCII.GetByteCount(cmd)} >>\nstream\n{cmd}endstream");
        }
        var xref = output.Position; Write($"xref\n0 {offsets.Count}\n0000000000 65535 f \n");
        foreach (var offset in offsets.Skip(1)) Write($"{offset:0000000000} 00000 n \n");
        Write($"trailer\n<< /Size {offsets.Count} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n"); return output.ToArray();
    }
}
