using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Ellipse = Microsoft.UI.Xaml.Shapes.Ellipse;
using Windows.UI;
using Windows.UI.ViewManagement;

namespace WorkTool.WinUI;

// Fluent 색: 면은 구분선(#E5E5E5)으로만 나누고, 상태는 7px 점 + 글자로 보여 줘요.
// 코드로 만드는 화면도 앱의 화면 모드(시스템·라이트·다크)를 따르도록 붓의 색만 바꿔요.
public sealed partial class MainWindow
{
    private readonly AccessibilitySettings accessibilitySettings = new();
    private readonly UISettings colorSettings = new();
    private readonly SolidColorBrush divider = new();
    private readonly SolidColorBrush rowAlt = new();
    private readonly SolidColorBrush rowSelected = new();
    private readonly SolidColorBrush mutedInk = new();
    private readonly SolidColorBrush faintInk = new();
    private readonly SolidColorBrush successInk = new();
    private readonly SolidColorBrush warningInk = new();
    private readonly SolidColorBrush dangerInk = new();
    private readonly SolidColorBrush infoInk = new();
    private readonly SolidColorBrush accentInk = new();
    private readonly SolidColorBrush dropHighlight = new();
    private readonly SolidColorBrush transparent = new(Colors.Transparent);

    private void ObserveSystemColors()
    {
        var queue = DispatcherQueue;
        void OnSystemColorsChanged(UISettings sender, object args) => queue.TryEnqueue(UpdateWorkColors);
        try
        {
            colorSettings.ColorValuesChanged += OnSystemColorsChanged;
            Closed += (_, _) => colorSettings.ColorValuesChanged -= OnSystemColorsChanged;
        }
        catch (System.Runtime.InteropServices.COMException)
        {
            // Some desktop sessions cannot subscribe to Windows color events.
            // The application theme event still updates the palette.
        }
    }

    private void UpdateWorkColors()
    {
        var dark = Root.ActualTheme == ElementTheme.Dark;
        var hc = accessibilitySettings.HighContrast;
        var fg = colorSettings.GetColorValue(UIColorType.Foreground);
        var bg = colorSettings.GetColorValue(UIColorType.Background);
        Color Pick(int light, int darkValue, bool foreground = true) => hc ? (foreground ? fg : bg) : Rgb(dark ? darkValue : light);
        divider.Color = Pick(0xE5E5E5, 0x3A3A3A);
        rowAlt.Color = Pick(0xF5F5F5, 0x2B2B2B, foreground: false);
        rowSelected.Color = Pick(0xE3EFEA, 0x2F3D37, foreground: false);
        mutedInk.Color = Pick(0x5F5F5F, 0xC8C8C8);
        faintInk.Color = Pick(0x8A8A8A, 0x9D9D9D);
        successInk.Color = Pick(0x1F7A4D, 0x6CCB98);
        warningInk.Color = Pick(0xA65A10, 0xE3A15B);
        dangerInk.Color = Pick(0xB42F2F, 0xF08A8A);
        infoInk.Color = Pick(0x1D4FA8, 0x8AB4F8);
        accentInk.Color = Pick(0x267358, 0x4FB38C);
        dropHighlight.Color = Pick(0xE6F1EC, 0x24382F, foreground: false);
    }

    private static Color Rgb(int value) => Color.FromArgb(255, (byte)(value >> 16), (byte)(value >> 8), (byte)value);

    /// <summary>조회 상태 점 색: 초록 확인 · 주황 미조회/진행/확인 중 · 빨강 비용·미완료 · 파랑 슬랙 문의</summary>
    private SolidColorBrush DispatchStatusInk(JsonNodeView row)
    {
        if (row.SlackPending) return warningInk;
        return row.Status switch
        {
            "confirmed" or "carrier" => successInk,
            "slack" => infoInk,
            "missing" or "review" or "duplicate" => warningInk,
            "incomplete" => row.Label.Contains("진행") ? warningInk : dangerInk,
            "cost" => dangerInk,
            _ => faintInk
        };
    }

    private readonly record struct JsonNodeView(string Status, string Label, bool SlackPending);

    /// <summary>7px 색 점 + 글자</summary>
    private static StackPanel DotLabel(TextBlock text, Ellipse dot)
    {
        dot.Width = dot.Height = 7;
        dot.VerticalAlignment = VerticalAlignment.Center;
        dot.Margin = new Thickness(0, 1, 8, 0);
        text.VerticalAlignment = VerticalAlignment.Center;
        var panel = new StackPanel { Orientation = Orientation.Horizontal };
        panel.Children.Add(dot);
        panel.Children.Add(text);
        return panel;
    }

    /// <summary>가로 1px 구분선</summary>
    private Border Divider() => new() { Height = 1, Background = divider, HorizontalAlignment = HorizontalAlignment.Stretch };
}
