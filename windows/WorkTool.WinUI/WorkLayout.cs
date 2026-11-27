using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace WorkTool.WinUI;

public sealed partial class MainWindow
{
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

    /// <summary>표 머리글: 배경 없이 12px 회색 글자, 아래 구분선</summary>
    private Grid TableHeader(double[] widths, params string[] labels)
    {
        var row = Columns(widths, labels.Select(label =>
        {
            var text = Text(label, 12);
            text.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
            text.Foreground = mutedInk;
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
        row.MinHeight = 36;
        row.BorderBrush = divider;
        row.BorderThickness = new Thickness(0, 0, 0, 1);
        return row;
    }

    private static Panel WrapControls(bool center, params UIElement[] controls)
    {
        var panel = new WorkWrapPanel { CenterVertically = center };
        foreach (var control in controls) panel.Children.Add(control);
        return panel;
    }
    private static ScrollViewer VerticalScroll(UIElement content) => new()
    {
        Content = content,
        HorizontalScrollMode = ScrollMode.Disabled,
        HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
        VerticalScrollBarVisibility = ScrollBarVisibility.Auto
    };

    private Border TableRow(UIElement content, int index = 0) => new()
    {
        Child = content,
        Background = index % 2 == 1 ? rowAlt : transparent
    };

    private static ScrollViewer Horizontal(UIElement content) => new()
    {
        Content = content,
        HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
        VerticalScrollMode = ScrollMode.Disabled,
        VerticalScrollBarVisibility = ScrollBarVisibility.Disabled
    };
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
