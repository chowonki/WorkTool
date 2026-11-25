using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace WorkTool.WinUI;

public sealed partial class MainWindow
{
    private static Brush ThemeBrush(string key) => (Brush)Application.Current.Resources[key];

    private static Grid Columns(double[] widths, params FrameworkElement[] cells)
    {
        var grid = new Grid { ColumnSpacing = 12, Padding = new Thickness(12, 6, 12, 6) };
        for (var i = 0; i < cells.Length; i++)
        {
            grid.ColumnDefinitions.Add(new ColumnDefinition
            {
                Width = widths[i] == 0 ? new GridLength(1, GridUnitType.Star) : new GridLength(widths[i])
            });
            cells[i].VerticalAlignment = VerticalAlignment.Center;
            Grid.SetColumn(cells[i], i);
            grid.Children.Add(cells[i]);
        }
        return grid;
    }

    private static Grid TableHeader(double[] widths, params string[] labels)
    {
        var row = Columns(widths, labels.Select(label =>
        {
            var text = Text(label, 13);
            text.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
            if (label is "주유·충전비" or "탁송비" or "부가세" or "입금액")
                text.TextAlignment = TextAlignment.Right;
            if (label == "차량번호")
            {
                text.Margin = new Thickness(28, 0, 0, 0);
                text.TextWrapping = TextWrapping.NoWrap;
                text.TextTrimming = TextTrimming.CharacterEllipsis;
            }
            if (label is "완료" or "상세") text.TextAlignment = TextAlignment.Center;
            return text;
        }).ToArray());
        row.Background = ThemeBrush("LayerFillColorDefaultBrush");
        return row;
    }

    private static Panel WrapActions(params UIElement[] controls)
        => WrapControls(center: false, controls);

    private static void SetActiveStep(Button[] buttons, int activeIndex, WorkTone tone)
    {
        for (var i = 0; i < buttons.Length; i++)
        {
            var button = buttons[i];
            if (i == activeIndex)
            {
                button.Background = tone.Fill;
                button.Foreground = tone.Ink;
                button.BorderBrush = tone.Accent;
            }
            else
            {
                button.ClearValue(Control.BackgroundProperty);
                button.ClearValue(Control.ForegroundProperty);
                button.ClearValue(Control.BorderBrushProperty);
            }
        }
    }

    private static Panel WrapControls(bool center, params UIElement[] controls)
    {
        var panel = new WorkWrapPanel { CenterVertically = center };
        foreach (var control in controls) panel.Children.Add(control);
        return panel;
    }

    private static Border PageHeading(string title, string glyph, WorkTone tone, Button action)
    {
        action.HorizontalAlignment = HorizontalAlignment.Right;
        var text = TitleText(title);
        text.Foreground = tone.Ink;
        text.TextWrapping = TextWrapping.Wrap;
        var heading = Columns([33, 0, 102], ToneIcon(tone, glyph), text, action);
        heading.Padding = new Thickness(14, 9, 14, 9);
        return new Border
        {
            Child = heading, Background = tone.Surface, BorderBrush = tone.Accent,
            BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(10)
        };
    }

    private static ScrollViewer VerticalScroll(UIElement content) => new()
    {
        Content = content,
        HorizontalScrollMode = ScrollMode.Disabled,
        HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
        VerticalScrollBarVisibility = ScrollBarVisibility.Auto
    };

    private Border TableRow(UIElement content) => new()
    {
        Child = content,
        BorderThickness = new Thickness(0, 0, 0, 1),
        BorderBrush = cardStroke
    };

    private static ScrollViewer Horizontal(UIElement content) => new()
    {
        Content = content,
        HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
        VerticalScrollMode = ScrollMode.Disabled,
        VerticalScrollBarVisibility = ScrollBarVisibility.Disabled
    };

    private static Grid SplitLayout(FrameworkElement first, FrameworkElement second, double breakpoint)
    {
        var grid = new Grid { RowSpacing = 10, ColumnSpacing = 16 };
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(0) });
        Grid.SetRow(second, 1);
        grid.Children.Add(first);
        grid.Children.Add(second);
        grid.SizeChanged += (_, args) =>
        {
            var wide = args.NewSize.Width >= breakpoint;
            Grid.SetRow(second, wide ? 0 : 1);
            Grid.SetColumn(second, wide ? 1 : 0);
            grid.ColumnDefinitions[1].Width = new GridLength(wide ? 1 : 0, GridUnitType.Star);
        };
        return grid;
    }
}

// Keep controls together and wrap whole fields when the window gets narrower.
internal sealed class WorkWrapPanel : Panel
{
    private const double Gap = 8;
    public bool CenterVertically { get; init; }

    protected override Size MeasureOverride(Size availableSize)
    {
        foreach (var child in Children) child.Measure(new Size(availableSize.Width, double.PositiveInfinity));
        return Layout(availableSize.Width, arrange: false);
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        Layout(finalSize.Width, arrange: true);
        return finalSize;
    }

    private Size Layout(double availableWidth, bool arrange)
    {
        var line = new List<UIElement>();
        var lineWidth = 0d;
        var lineHeight = 0d;
        var totalHeight = 0d;
        var maxWidth = 0d;

        void FinishLine()
        {
            if (line.Count == 0) return;
            if (totalHeight > 0) totalHeight += Gap;
            var x = 0d;
            foreach (var item in line)
            {
                var size = item.DesiredSize;
                var width = Math.Min(size.Width, availableWidth);
                var offset = (lineHeight - size.Height) * (CenterVertically ? 0.5 : 1);
                if (arrange) item.Arrange(new Rect(x, totalHeight + offset, width, size.Height));
                x += width + Gap;
            }
            maxWidth = Math.Max(maxWidth, lineWidth);
            totalHeight += lineHeight;
            line.Clear();
            lineWidth = lineHeight = 0;
        }

        foreach (var child in Children)
        {
            if (child.Visibility == Visibility.Collapsed) continue;
            var width = Math.Min(child.DesiredSize.Width, availableWidth);
            if (line.Count > 0 && lineWidth + Gap + width > availableWidth) FinishLine();
            if (line.Count > 0) lineWidth += Gap;
            line.Add(child);
            lineWidth += width;
            lineHeight = Math.Max(lineHeight, child.DesiredSize.Height);
        }
        FinishLine();
        return new Size(maxWidth, totalHeight);
    }
}
