#pragma once
// 코드로 화면을 만드는 작은 도우미 (XAML 파일 없이 C++/WinRT로만 만들어요)

namespace wt
{
    namespace mux = winrt::Microsoft::UI::Xaml;
    namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
    namespace muxm = winrt::Microsoft::UI::Xaml::Media;
    using Async = winrt::Windows::Foundation::IAsyncAction;
    using IInspectable = winrt::Windows::Foundation::IInspectable;  // Windows SDK의 ::IInspectable과 헷갈리지 않게
    using Ellipse = winrt::Microsoft::UI::Xaml::Shapes::Ellipse;    // GDI의 ::Ellipse 함수와 헷갈리지 않게
    using winrt::hstring;
    using winrt::box_value;
    using mux::UIElement;
    using mux::FrameworkElement;
    using mux::Visibility;
    using mux::HorizontalAlignment;
    using mux::VerticalAlignment;
    using muxc::TextBlock;
    using muxc::StackPanel;
    using muxc::Grid;
    using muxc::Border;
    using muxc::Button;
    using muxc::TextBox;
    using muxc::CheckBox;
    using muxc::ComboBox;
    using muxc::ScrollViewer;
    using muxc::FontIcon;
    using muxc::AppBarButton;
    using muxc::HyperlinkButton;
    using muxc::ContentDialog;
    using muxc::ContentDialogResult;

    /// co_await ResumeOn(queue): 뒤쪽 스레드에서 하던 일을 화면 스레드로 되돌려요.
    struct ResumeOn
    {
        winrt::Microsoft::UI::Dispatching::DispatcherQueue queue;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) const
        {
            if (!queue.TryEnqueue([handle] { handle(); })) handle();
        }
        void await_resume() const noexcept {}
    };

    inline mux::Thickness Th(double left, double top, double right, double bottom) { return { left, top, right, bottom }; }
    inline mux::Thickness Th(double all) { return { all, all, all, all }; }
    inline mux::CornerRadius Radius(double all) { return { all, all, all, all }; }
    inline mux::GridLength Star(double value = 1) { return { value, mux::GridUnitType::Star }; }
    inline mux::GridLength Px(double value) { return { value, mux::GridUnitType::Pixel }; }
    inline mux::GridLength AutoSize() { return { 1, mux::GridUnitType::Auto }; }
    inline auto SemiBold() { return winrt::Microsoft::UI::Text::FontWeights::SemiBold(); }
    inline auto Bold() { return winrt::Microsoft::UI::Text::FontWeights::Bold(); }
    inline Visibility Shown(bool visible) { return visible ? Visibility::Visible : Visibility::Collapsed; }
    inline bool IsShown(UIElement const& element) { return element.Visibility() == Visibility::Visible; }

    inline bool IsOn(muxc::Primitives::ToggleButton const& box)
    {
        auto value = box.IsChecked();
        return value && value.Value();
    }

    inline void Name(mux::DependencyObject const& element, std::wstring_view name)
    {
        mux::Automation::AutomationProperties::SetName(element, hstring(name));
    }
    inline void Tip(mux::DependencyObject const& element, std::wstring_view text)
    {
        muxc::ToolTipService::SetToolTip(element, box_value(hstring(text)));
    }

    inline TextBlock Text(std::wstring_view value, double size = 14)
    {
        TextBlock text;
        text.Text(hstring(value));
        text.FontSize(size);
        text.TextWrapping(mux::TextWrapping::Wrap);
        return text;
    }

    inline StackPanel Stack(std::initializer_list<UIElement> children = {})
    {
        StackPanel stack;
        stack.Spacing(14);
        for (auto const& child : children) stack.Children().Append(child);
        return stack;
    }

    inline StackPanel Bar(std::initializer_list<UIElement> children = {})
    {
        StackPanel stack;
        stack.Orientation(muxc::Orientation::Horizontal);
        stack.Spacing(8);
        for (auto const& child : children) stack.Children().Append(child);
        return stack;
    }

    inline FontIcon Icon(std::wstring_view glyph, double size = 0)
    {
        FontIcon icon;
        icon.Glyph(hstring(glyph));
        if (size > 0) icon.FontSize(size);
        return icon;
    }

    inline mux::Style AccentStyle()
    {
        return mux::Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<mux::Style>();
    }

    inline muxm::FontFamily AppFont(wchar_t const* key = L"AppFont")
    {
        return mux::Application::Current().Resources().Lookup(box_value(key)).as<muxm::FontFamily>();
    }

    inline Button MakeButton(std::wstring_view title, std::function<void()> action, bool primary = false)
    {
        Button button;
        button.Content(box_value(hstring(title)));
        if (primary) button.Style(AccentStyle());
        if (action) button.Click([action](auto&&, auto&&) { action(); });
        return button;
    }

    inline ScrollViewer Scroll(UIElement const& content)
    {
        ScrollViewer view;
        view.Content(content);
        view.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
        view.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
        return view;
    }

    inline ScrollViewer VerticalScroll(UIElement const& content)
    {
        ScrollViewer view;
        view.Content(content);
        view.HorizontalScrollMode(muxc::ScrollMode::Disabled);
        view.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Disabled);
        view.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
        return view;
    }

    inline ScrollViewer Horizontal(UIElement const& content)
    {
        ScrollViewer view;
        view.Content(content);
        view.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
        view.VerticalScrollMode(muxc::ScrollMode::Disabled);
        view.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Disabled);
        return view;
    }

    inline TextBox Input(std::wstring_view header, std::wstring_view placeholder = L"", double width = 0)
    {
        TextBox box;
        if (!header.empty()) box.Header(box_value(hstring(header)));
        box.PlaceholderText(hstring(placeholder));
        if (width > 0) box.Width(width);
        box.HorizontalAlignment(HorizontalAlignment::Stretch);
        return box;
    }

    inline TextBlock TitleText(std::wstring_view title)
    {
        TextBlock text;
        text.Text(hstring(title));
        text.FontSize(24);
        text.FontWeight(SemiBold());
        return text;
    }

    /// 패널 안 소제목
    inline TextBlock PanelHeading(std::wstring_view title)
    {
        TextBlock text;
        text.Text(hstring(title));
        text.FontSize(14);
        text.FontWeight(SemiBold());
        return text;
    }

    inline void AddColumn(Grid const& grid, mux::GridLength width, double minWidth = 0)
    {
        muxc::ColumnDefinition column;
        column.Width(width);
        if (minWidth > 0) column.MinWidth(minWidth);
        grid.ColumnDefinitions().Append(column);
    }

    inline void AddRow(Grid const& grid, mux::GridLength height, double minHeight = 0)
    {
        muxc::RowDefinition row;
        row.Height(height);
        if (minHeight > 0) row.MinHeight(minHeight);
        grid.RowDefinitions().Append(row);
    }

    inline void Place(Grid const& grid, FrameworkElement const& element, int row, int column = 0)
    {
        Grid::SetRow(element, row);
        Grid::SetColumn(element, column);
        grid.Children().Append(element);
    }

    /// 한 줄 격자. 너비 0은 남는 폭을 나눠 가져요.
    inline Grid Columns(std::vector<double> const& widths, std::vector<FrameworkElement> const& cells)
    {
        Grid grid;
        grid.ColumnSpacing(12);
        grid.Padding(Th(12, 6, 12, 6));
        for (size_t i = 0; i < cells.size(); i++)
        {
            AddColumn(grid, widths[i] == 0 ? Star() : Px(widths[i]));
            cells[i].VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(cells[i], static_cast<int>(i));
            grid.Children().Append(cells[i]);
        }
        return grid;
    }

    inline void SetItems(ComboBox const& box, std::vector<std::wstring> const& items)
    {
        box.Items().Clear();
        for (auto const& item : items) box.Items().Append(box_value(hstring(item)));
    }

    inline std::wstring SelectedText(ComboBox const& box)
    {
        return std::wstring(winrt::unbox_value_or<hstring>(box.SelectedItem(), L""));
    }

    inline mux::Documents::Run MakeRun(std::wstring_view text)
    {
        mux::Documents::Run run;
        run.Text(hstring(text));
        return run;
    }

    // 창이 좁아지면 칸을 통째로 다음 줄로 넘기는 패널
    struct WorkWrapPanel : muxc::PanelT<WorkWrapPanel>
    {
        bool centerVertically{};
        winrt::Windows::Foundation::Size MeasureOverride(winrt::Windows::Foundation::Size available)
        {
            for (auto const& child : Children()) child.Measure({ available.Width, std::numeric_limits<float>::infinity() });
            return Layout(available.Width, false);
        }
        winrt::Windows::Foundation::Size ArrangeOverride(winrt::Windows::Foundation::Size final)
        {
            Layout(final.Width, true);
            return final;
        }

    private:
        winrt::Windows::Foundation::Size Layout(float availableWidth, bool arrange)
        {
            constexpr float gap = 8;
            std::vector<UIElement> line;
            float lineWidth = 0, lineHeight = 0, totalHeight = 0, maxWidth = 0;
            auto finish = [&]
            {
                if (line.empty()) return;
                if (totalHeight > 0) totalHeight += gap;
                float x = 0;
                for (auto const& item : line)
                {
                    auto size = item.DesiredSize();
                    auto width = std::min(size.Width, availableWidth);
                    auto offset = (lineHeight - size.Height) * (centerVertically ? 0.5f : 1.0f);
                    if (arrange) item.Arrange({ x, totalHeight + offset, width, size.Height });
                    x += width + gap;
                }
                maxWidth = std::max(maxWidth, lineWidth);
                totalHeight += lineHeight;
                line.clear();
                lineWidth = lineHeight = 0;
            };
            for (auto const& child : Children())
            {
                if (child.Visibility() == Visibility::Collapsed) continue;
                auto width = std::min(child.DesiredSize().Width, availableWidth);
                if (!line.empty() && lineWidth + gap + width > availableWidth) finish();
                if (!line.empty()) lineWidth += gap;
                line.push_back(child);
                lineWidth += width;
                lineHeight = std::max(lineHeight, child.DesiredSize().Height);
            }
            finish();
            return { maxWidth, totalHeight };
        }
    };
}
