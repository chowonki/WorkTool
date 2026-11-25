using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

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
            return text;
        }).ToArray());
        row.Background = ThemeBrush("LayerFillColorDefaultBrush");
        return row;
    }

    private static Border TableRow(UIElement content) => new()
    {
        Child = content,
        BorderThickness = new Thickness(0, 0, 0, 1),
        BorderBrush = ThemeBrush("CardStrokeColorDefaultBrush")
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
