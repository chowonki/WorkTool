using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI;
using Windows.UI.ViewManagement;

namespace WorkTool.WinUI;

public sealed partial class MainWindow
{
    private readonly AccessibilitySettings accessibilitySettings = new();
    private readonly UISettings colorSettings = new();
    private readonly SolidColorBrush cardFill = new();
    private readonly SolidColorBrush cardStroke = new();
    private readonly WorkTone dispatchTone = new(0x075361, 0xC9F5E9, 0xE2F6FF, 0x085D71, 0xACF0E3, 0x143E3B, 0x153546, 0x61D7D2);
    private readonly WorkTone transferTone = new(0x452989, 0xE3DBFF, 0xF4E4FF, 0x6944C4, 0xE4D8FF, 0x342551, 0x44294D, 0xBD9EFF);
    private readonly WorkTone successTone = new(0x105B40, 0xD7F5E5, 0xD7F5E5, 0x187653, 0xA6ECC8, 0x1B4234, 0x1B4234, 0x71D9A8);
    private readonly WorkTone activeTone = new(0x174F96, 0xDEEDFF, 0xDEEDFF, 0x2663AC, 0xBBD9FF, 0x233C60, 0x233C60, 0x8BBEFF);
    private readonly WorkTone attentionTone = new(0x754200, 0xFFECC2, 0xFFECC2, 0x8D5908, 0xF9D88C, 0x4A371E, 0x4A371E, 0xFFCE6D);
    private readonly WorkTone errorTone = new(0x8F2545, 0xFFE3EC, 0xFFE3EC, 0xA83157, 0xFFBCD0, 0x512537, 0x512537, 0xFBA1BC);
    private readonly WorkTone neutralTone = new(0x40506C, 0xE8EEF9, 0xE8EEF9, 0x576D93, 0xCCD8ED, 0x303B51, 0x303B51, 0xAFC2E3);

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
        var highContrast = accessibilitySettings.HighContrast;
        var foreground = colorSettings.GetColorValue(UIColorType.Foreground);
        var background = colorSettings.GetColorValue(UIColorType.Background);
        cardFill.Color = highContrast ? background : dark ? Color.FromArgb(255, 43, 45, 52) : Colors.White;
        cardStroke.Color = highContrast ? foreground : dark ? Color.FromArgb(255, 69, 73, 87) : Color.FromArgb(255, 224, 229, 239);
        foreach (var tone in new[] { dispatchTone, transferTone, successTone, activeTone, attentionTone, errorTone, neutralTone })
            tone.Apply(dark, highContrast, foreground, background);
    }

    private FrameworkElement ToolCard(WorkTone tone, string glyph, string title, string description, string action, string route)
    {
        var label = Text(title, 24);
        label.Foreground = tone.Ink;
        label.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
        var icon = ToneIcon(tone, glyph);
        var heading = WrapControls(center: true, icon, label);
        return new Border
        {
            Background = tone.Surface, BorderBrush = tone.Accent, BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(12), Padding = new Thickness(22),
            Child = Stack(heading, Text(description), Button(action, () => Go(route), true))
        };
    }

    private static Border ToneIcon(WorkTone tone, string glyph) => new()
    {
        Width = 33, Height = 33,
        Background = tone.Fill, BorderBrush = tone.Accent, BorderThickness = new Thickness(1),
        CornerRadius = new CornerRadius(8),
        Child = new FontIcon { Glyph = glyph, Foreground = tone.Ink, FontSize = 15 }
    };

    private static Border StatusTag(TextBlock text) => new()
    {
        Child = text, Padding = new Thickness(8, 3, 8, 3),
        CornerRadius = new CornerRadius(6), HorizontalAlignment = HorizontalAlignment.Left
    };

    private static void TintTag(Border tag, TextBlock text, WorkTone tone)
    {
        tag.Background = tone.Fill;
        text.Foreground = tone.Ink;
    }

    private WorkTone DispatchStatusTone(string status) => status switch
    {
        "confirmed" => successTone,
        "incomplete" => activeTone,
        "review" or "cost" or "duplicate" => attentionTone,
        "slack" => transferTone,
        "carrier" => dispatchTone,
        _ => neutralTone
    };

    private sealed class WorkTone(uint lightInk, uint lightFill, uint lightTail, uint lightAccent,
        uint darkInk, uint darkFill, uint darkTail, uint darkAccent)
    {
        public SolidColorBrush Ink { get; } = new();
        public SolidColorBrush Fill { get; } = new();
        public SolidColorBrush Accent { get; } = new();
        public LinearGradientBrush Surface { get; } = new()
        {
            StartPoint = new Windows.Foundation.Point(0, 0),
            EndPoint = new Windows.Foundation.Point(1, 0),
            GradientStops = new GradientStopCollection
            {
                new GradientStop { Offset = 0 },
                new GradientStop { Offset = 1 }
            }
        };

        public void Apply(bool dark, bool highContrast, Color foreground, Color background)
        {
            Ink.Color = highContrast ? foreground : Rgb(dark ? darkInk : lightInk);
            Fill.Color = highContrast ? background : Rgb(dark ? darkFill : lightFill);
            Accent.Color = highContrast ? foreground : Rgb(dark ? darkAccent : lightAccent);
            Surface.GradientStops[0].Color = Fill.Color;
            Surface.GradientStops[1].Color = highContrast ? background : Rgb(dark ? darkTail : lightTail);
        }

        private static Color Rgb(uint value) => Color.FromArgb(255, (byte)(value >> 16), (byte)(value >> 8), (byte)value);
    }
}
