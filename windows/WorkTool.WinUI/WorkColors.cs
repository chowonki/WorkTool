using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Ellipse = Microsoft.UI.Xaml.Shapes.Ellipse;
using Windows.UI;
using Windows.UI.ViewManagement;

namespace WorkTool.WinUI;

// Fluent 색: 면은 구분선(#E5E5E5)으로만 나누고, 아이콘·상태·제목에 색을 입혀요.
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
    private readonly SolidColorBrush dropHighlight = new();
    private readonly SolidColorBrush onTile = new();
    private readonly SolidColorBrush transparent = new(Colors.Transparent);
    private readonly LinearGradientBrush headingGradient = new()
    {
        StartPoint = new Windows.Foundation.Point(0, 0), EndPoint = new Windows.Foundation.Point(1, 0),
        GradientStops = new GradientStopCollection { new GradientStop { Offset = 0 }, new GradientStop { Offset = 0.5 }, new GradientStop { Offset = 1 } }
    };

    // 색 팔레트 (라이트 / 다크)
    private readonly Swatch green = new(0x1F7A4D, 0x6CCB98);
    private readonly Swatch accent = new(0x267358, 0x4FB38C);
    private readonly Swatch teal = new(0x0F8B8D, 0x3FC7C9);
    private readonly Swatch purple = new(0x7A4CC2, 0xB28CFF);
    private readonly Swatch pink = new(0xC23F7A, 0xFF7FB4);
    private readonly Swatch indigo = new(0x4B56D2, 0x8F97FF);
    private readonly Swatch brown = new(0x8A6233, 0xC9A06A);
    private readonly Swatch blue = new(0x1D4FA8, 0x8AB4F8);
    private readonly Swatch orange = new(0xA65A10, 0xE3A15B);
    private readonly Swatch yellow = new(0x8A5A00, 0xE0B44C);
    private readonly Swatch red = new(0xB42F2F, 0xF08A8A);
    private readonly Swatch gray = new(0x6B6B6B, 0xA8A8A8);
    private Swatch[] Swatches => [green, accent, teal, purple, pink, indigo, brown, blue, orange, yellow, red, gray];

    // 예전 이름 (상태 글자색)
    private SolidColorBrush successInk => green.Solid;
    private SolidColorBrush warningInk => orange.Solid;
    private SolidColorBrush dangerInk => red.Solid;
    private SolidColorBrush infoInk => blue.Solid;
    private SolidColorBrush accentInk => accent.Solid;

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
        dropHighlight.Color = Pick(0xE6F1EC, 0x24382F, foreground: false);
        onTile.Color = hc ? bg : Colors.White;
        foreach (var swatch in Swatches) swatch.Apply(dark, hc, fg, bg);
        headingGradient.GradientStops[0].Color = accent.Solid.Color;
        headingGradient.GradientStops[1].Color = teal.Solid.Color;
        headingGradient.GradientStops[2].Color = blue.Solid.Color;
    }

    private static Color Rgb(int value) => Color.FromArgb(255, (byte)(value >> 16), (byte)(value >> 8), (byte)value);

    /// <summary>한 가지 색: 글자·아이콘(Solid), 알약 배경(Soft, 14%), 색 타일(위가 살짝 밝은 그라데이션)</summary>
    private sealed class Swatch(int light, int dark)
    {
        public SolidColorBrush Solid { get; } = new();
        public SolidColorBrush Soft { get; } = new();
        public LinearGradientBrush Tile { get; } = new()
        {
            StartPoint = new Windows.Foundation.Point(0, 0), EndPoint = new Windows.Foundation.Point(0, 1),
            GradientStops = new GradientStopCollection { new GradientStop { Offset = 0 }, new GradientStop { Offset = 1 } }
        };

        public void Apply(bool darkTheme, bool highContrast, Color foreground, Color background)
        {
            var color = highContrast ? foreground : Rgb(darkTheme ? dark : light);
            Solid.Color = color;
            Soft.Color = highContrast ? background : Color.FromArgb(36, color.R, color.G, color.B);
            // 타일은 흰 아이콘이 잘 보이도록 두 테마 모두 진한 색을 써요 (Windows 설정 앱과 같은 방식).
            var tile = highContrast ? foreground : Rgb(light);
            Tile.GradientStops[0].Color = highContrast ? tile : Lighten(tile, 0.18);
            Tile.GradientStops[1].Color = tile;
        }

        private static Color Lighten(Color c, double amount) => Color.FromArgb(255,
            (byte)(c.R + (255 - c.R) * amount), (byte)(c.G + (255 - c.G) * amount), (byte)(c.B + (255 - c.B) * amount));
    }

    /// <summary>둥근 색 타일 위 흰 아이콘 (Windows 설정 앱 모양)</summary>
    private Border Tile(Swatch swatch, string glyph, double size = 20, double iconSize = 12, double radius = 5) => new()
    {
        Width = size, Height = size, CornerRadius = new CornerRadius(radius), Background = swatch.Tile,
        VerticalAlignment = VerticalAlignment.Center,
        Child = new FontIcon { Glyph = glyph, FontSize = iconSize, Foreground = onTile }
    };

    /// <summary>색 타일 + 그 색의 제목 글자 (오른쪽 패널 구역 제목)</summary>
    private StackPanel ColorHeading(string text, Swatch swatch, string glyph, double size = 14, double tile = 18)
    {
        var label = new TextBlock { Text = text, FontSize = size, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, Foreground = swatch.Solid, VerticalAlignment = VerticalAlignment.Center };
        var panel = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        panel.Children.Add(Tile(swatch, glyph, tile, tile * 0.6, tile / 4));
        panel.Children.Add(label);
        return panel;
    }

    /// <summary>조회 상태 색: 초록 확인 · 주황 미조회/검토/확인 중 · 노랑 진행 중 · 빨강 비용·미완료 · 보라 슬랙 문의 · 갈색 캐리어</summary>
    private Swatch DispatchStatusSwatch(JsonNodeView row)
    {
        if (row.SlackPending) return orange;
        return row.Status switch
        {
            "confirmed" => green,
            "carrier" => brown,
            "slack" => purple,
            "missing" or "review" or "duplicate" => orange,
            "incomplete" => row.Label.Contains("진행") ? yellow : red,
            "cost" => red,
            _ => gray
        };
    }

    private readonly record struct JsonNodeView(string Status, string Label, bool SlackPending);

    /// <summary>색 알약: 배경은 상태색 14%, 점·글자는 상태색 (12px semibold)</summary>
    private static Border Pill(TextBlock text, Ellipse dot)
    {
        dot.Width = dot.Height = 6;
        dot.VerticalAlignment = VerticalAlignment.Center;
        dot.Margin = new Thickness(0, 1, 6, 0);
        text.FontSize = 12;
        text.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
        text.TextWrapping = TextWrapping.NoWrap;
        text.VerticalAlignment = VerticalAlignment.Center;
        var panel = new StackPanel { Orientation = Orientation.Horizontal };
        panel.Children.Add(dot);
        panel.Children.Add(text);
        return new Border { Child = panel, CornerRadius = new CornerRadius(10), Padding = new Thickness(8, 2, 9, 3), HorizontalAlignment = HorizontalAlignment.Left };
    }

    private static void TintPill(Border pill, Swatch swatch)
    {
        pill.Background = swatch.Soft;
        var panel = (StackPanel)pill.Child;
        ((Ellipse)panel.Children[0]).Fill = swatch.Solid;
        ((TextBlock)panel.Children[1]).Foreground = swatch.Solid;
    }

    /// <summary>7px 색 점 + 글자 (패널 안 확인 사항)</summary>
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

    /// <summary>AccentButton을 다른 색으로 (홈의 '이체파일 만들기'는 파랑)</summary>
    private static void TintAccentButton(Button button, Swatch swatch)
    {
        button.Resources["AccentButtonBackground"] = swatch.Tile;
        button.Resources["AccentButtonBackgroundPointerOver"] = swatch.Tile;
        button.Resources["AccentButtonBackgroundPressed"] = swatch.Tile;
        button.Resources["AccentButtonForeground"] = new SolidColorBrush(Colors.White);
        button.Resources["AccentButtonForegroundPointerOver"] = new SolidColorBrush(Colors.White);
        button.Resources["AccentButtonForegroundPressed"] = new SolidColorBrush(Color.FromArgb(220, 255, 255, 255));
    }

    /// <summary>가로 1px 구분선</summary>
    private Border Divider() => new() { Height = 1, Background = divider, HorizontalAlignment = HorizontalAlignment.Stretch };
}
