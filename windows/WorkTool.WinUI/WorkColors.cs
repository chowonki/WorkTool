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
    private readonly SolidColorBrush pageFill = new();
    private readonly SolidColorBrush cardFill = new();
    private readonly SolidColorBrush cardStroke = new();
    private readonly SolidColorBrush fieldFill = new();
    private readonly SolidColorBrush fieldFocusFill = new();
    private readonly WorkTone dispatchTone = new(0x194C4E, 0xB4CCC7, 0xC6D3D9, 0x678F8C, 0xB5DCD6, 0x213A38, 0x223641, 0x609891);
    private readonly WorkTone transferTone = new(0x194A3E, 0xB0CBBB, 0xC7D4C9, 0x658875, 0xC0DBCD, 0x233B32, 0x2E3C36, 0x759F8D);
    private readonly WorkTone inquiryTone = new(0x463364, 0xC7BDD9, 0xD3C8DA, 0x8A739F, 0xD9CBEA, 0x343044, 0x3B3141, 0x917CA7);
    private readonly SolidColorBrush transferButtonFill = new();
    private readonly SolidColorBrush transferButtonHover = new();
    private readonly SolidColorBrush transferButtonPressed = new();
    private readonly SolidColorBrush transferButtonInk = new();
    private readonly WorkTone successTone = new(0x194B39, 0xC3D7CA, 0xC3D7CA, 0x618774, 0xAFD6BE, 0x253E34, 0x253E34, 0x79A891);
    private readonly WorkTone activeTone = new(0x274C79, 0xC5D4E6, 0xC5D4E6, 0x6988AE, 0xB9CDEA, 0x2A3B55, 0x2A3B55, 0x7794BF);
    private readonly WorkTone attentionTone = new(0x683D14, 0xDACBAE, 0xDACBAE, 0x9D8154, 0xE6CB9D, 0x42372A, 0x42372A, 0xAA9269);
    private readonly WorkTone errorTone = new(0x732D45, 0xDCC1CA, 0xDCC1CA, 0xA16D80, 0xE7BACA, 0x46303A, 0x46303A, 0xAE778C);
    private readonly WorkTone neutralTone = new(0x3F4D63, 0xCED6E1, 0xCED6E1, 0x75869E, 0xC5CFDE, 0x333B49, 0x333B49, 0x8699B5);

    private void ApplyWorkSurfaces()
    {
        Root.Background = pageFill;
        PageHost.Background = pageFill;
        Root.Resources["NavigationViewContentBackground"] = pageFill;
        Root.Resources["TextControlBackground"] = fieldFill;
        Root.Resources["TextControlBackgroundPointerOver"] = fieldFocusFill;
        Root.Resources["TextControlBackgroundFocused"] = fieldFocusFill;
    }

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
        pageFill.Color = highContrast ? background : Rgb(dark ? 0x222428 : 0xDCE2E8);
        cardFill.Color = highContrast ? background : Rgb(dark ? 0x2B2D34 : 0xE6EBF0);
        cardStroke.Color = highContrast ? foreground : Rgb(dark ? 0x454957 : 0xC3CCD7);
        fieldFill.Color = highContrast ? background : Rgb(dark ? 0x30333C : 0xE9EDF1);
        fieldFocusFill.Color = highContrast ? background : Rgb(dark ? 0x343943 : 0xEEF1F4);
        transferButtonFill.Color = highContrast ? background : Rgb(dark ? 0x37675B : 0x276252);
        transferButtonHover.Color = highContrast ? background : Rgb(dark ? 0x3D7164 : 0x2C6A59);
        transferButtonPressed.Color = highContrast ? background : Rgb(dark ? 0x305A50 : 0x205344);
        transferButtonInk.Color = highContrast ? foreground : Rgb(0xEDF3EF);
        foreach (var tone in new[] { dispatchTone, transferTone, inquiryTone, successTone, activeTone, attentionTone, errorTone, neutralTone })
            tone.Apply(dark, highContrast, foreground, background);
    }

    private static Color Rgb(int value) => Color.FromArgb(255, (byte)(value >> 16), (byte)(value >> 8), (byte)value);

    private Button TintTransferAction(Button button)
    {
        button.Resources["AccentButtonBackground"] = transferButtonFill;
        button.Resources["AccentButtonBackgroundPointerOver"] = transferButtonHover;
        button.Resources["AccentButtonBackgroundPressed"] = transferButtonPressed;
        button.Resources["AccentButtonForeground"] = transferButtonInk;
        button.Resources["AccentButtonForegroundPointerOver"] = transferButtonInk;
        button.Resources["AccentButtonForegroundPressed"] = transferButtonInk;
        return button;
    }

    private FrameworkElement ToolCard(WorkTone tone, string glyph, string title, string description, string action, string route)
    {
        var label = Text(title, 24);
        label.Foreground = tone.Ink;
        label.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
        var icon = ToneIcon(tone, glyph);
        var heading = WrapControls(center: true, icon, label);
        var button = Button(action, () => Go(route), true);
        if (route == "transfer") TintTransferAction(button);
        return new Border
        {
            Background = tone.Surface, BorderBrush = tone.Accent, BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(12), Padding = new Thickness(22),
            Child = Stack(heading, Text(description), button)
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
        "slack" => inquiryTone,
        "carrier" => dispatchTone,
        _ => neutralTone
    };

    private sealed class WorkTone(int lightInk, int lightFill, int lightTail, int lightAccent,
        int darkInk, int darkFill, int darkTail, int darkAccent)
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

    }
}
