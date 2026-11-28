#include "pch.h"
#include "MainWindow.h"

// Fluent 색: 면은 구분선(#E5E5E5)으로만 나누고, 아이콘·상태·제목에 색을 입혀요.
// 코드로 만드는 화면도 앱의 화면 모드(시스템·라이트·다크)를 따르도록 붓의 색만 바꿔요.

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using winrt::Windows::UI::Color;

namespace wt
{
    static Color Rgb(int value)
    {
        return { 255, static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value) };
    }

    static Color Lighten(Color c, double amount)
    {
        auto mix = [amount](uint8_t v) { return static_cast<uint8_t>(v + (255 - v) * amount); };
        return { 255, mix(c.R), mix(c.G), mix(c.B) };
    }

    static void AddStop(LinearGradientBrush const& brush, double offset)
    {
        GradientStop stop;
        stop.Offset(offset);
        brush.GradientStops().Append(stop);
    }

    Swatch::Swatch(int light, int dark) : light(light), dark(dark)
    {
        Tile.StartPoint({ 0, 0 });
        Tile.EndPoint({ 0, 1 });
        AddStop(Tile, 0);
        AddStop(Tile, 1);
    }

    void Swatch::Apply(bool darkTheme, bool highContrast, Color foreground, Color background)
    {
        auto color = highContrast ? foreground : Rgb(darkTheme ? dark : light);
        Solid.Color(color);
        Soft.Color(highContrast ? background : Color{ 36, color.R, color.G, color.B });
        // 타일은 흰 아이콘이 잘 보이도록 두 테마 모두 진한 색을 써요 (Windows 설정 앱과 같은 방식).
        auto tile = highContrast ? foreground : Rgb(light);
        Tile.GradientStops().GetAt(0).Color(highContrast ? tile : Lighten(tile, 0.18));
        Tile.GradientStops().GetAt(1).Color(tile);
    }

    void MainWindow::InitColors()
    {
        transparent.Color(winrt::Microsoft::UI::Colors::Transparent());
        headingGradient.StartPoint({ 0, 0 });
        headingGradient.EndPoint({ 1, 0 });
        AddStop(headingGradient, 0);
        AddStop(headingGradient, 0.5);
        AddStop(headingGradient, 1);
    }

    void MainWindow::ObserveSystemColors()
    {
        try
        {
            auto token = colorSettings.ColorValuesChanged([this](auto&&, auto&&) { queue.TryEnqueue([this] { UpdateWorkColors(); }); });
            window.Closed([this, token](auto&&, auto&&) { colorSettings.ColorValuesChanged(token); });
        }
        catch (hresult_error const&)
        {
            // 일부 데스크톱 세션은 Windows 색 이벤트를 받을 수 없어요. 앱 화면 모드 이벤트로도 색을 바꿔요.
        }
    }

    void MainWindow::UpdateWorkColors()
    {
        using winrt::Windows::UI::ViewManagement::UIColorType;
        auto dark = root.ActualTheme() == ElementTheme::Dark;
        auto hc = accessibilitySettings.HighContrast();
        auto fg = colorSettings.GetColorValue(UIColorType::Foreground);
        auto bg = colorSettings.GetColorValue(UIColorType::Background);
        auto pick = [&](int light, int darkValue, bool foreground = true) { return hc ? (foreground ? fg : bg) : Rgb(dark ? darkValue : light); };
        divider.Color(pick(0xE5E5E5, 0x3A3A3A));
        rowAlt.Color(pick(0xF5F5F5, 0x2B2B2B, false));
        rowSelected.Color(pick(0xE3EFEA, 0x2F3D37, false));
        mutedInk.Color(pick(0x5F5F5F, 0xC8C8C8));
        faintInk.Color(pick(0x8A8A8A, 0x9D9D9D));
        dropHighlight.Color(pick(0xE6F1EC, 0x24382F, false));
        onTile.Color(hc ? bg : winrt::Microsoft::UI::Colors::White());
        for (auto swatch : { &green, &accent, &teal, &purple, &pink, &indigo, &brown, &blue, &orange, &yellow, &red, &gray })
            swatch->Apply(dark, hc, fg, bg);
        headingGradient.GradientStops().GetAt(0).Color(accent.Solid.Color());
        headingGradient.GradientStops().GetAt(1).Color(teal.Solid.Color());
        headingGradient.GradientStops().GetAt(2).Color(blue.Solid.Color());
    }

    /// 둥근 색 타일 위 흰 아이콘 (Windows 설정 앱 모양)
    Border MainWindow::Tile(Swatch& swatch, std::wstring_view glyph, double size, double iconSize, double radius)
    {
        auto icon = Icon(glyph, iconSize);
        icon.Foreground(onTile);
        Border tile;
        tile.Width(size);
        tile.Height(size);
        tile.CornerRadius(Radius(radius));
        tile.Background(swatch.Tile);
        tile.VerticalAlignment(VerticalAlignment::Center);
        tile.Child(icon);
        return tile;
    }

    /// 색 타일 + 그 색의 제목 글자 (오른쪽 패널 구역 제목)
    StackPanel MainWindow::ColorHeading(std::wstring_view text, Swatch& swatch, std::wstring_view glyph, double size, double tile)
    {
        TextBlock label;
        label.Text(hstring(text));
        label.FontSize(size);
        label.FontWeight(SemiBold());
        label.Foreground(swatch.Solid);
        label.VerticalAlignment(VerticalAlignment::Center);
        return Bar({ Tile(swatch, glyph, tile, tile * 0.6, tile / 4), label });
    }

    /// 조회 상태 색: 초록 확인 · 주황 미조회/검토/확인 중 · 노랑 진행 중 · 빨강 비용·미완료 · 보라 슬랙 문의 · 갈색 캐리어
    Swatch& MainWindow::DispatchStatusSwatch(StatusKey const& row)
    {
        if (row.slackPending) return orange;
        if (row.status == L"confirmed") return green;
        if (row.status == L"carrier") return brown;
        if (row.status == L"slack") return purple;
        if (row.status == L"missing" || row.status == L"review" || row.status == L"duplicate") return orange;
        if (row.status == L"incomplete") return row.label.find(L"진행") != std::wstring::npos ? yellow : red;
        if (row.status == L"cost") return red;
        return gray;
    }

    /// 색 알약: 배경은 상태색 14%, 점·글자는 상태색 (12px semibold)
    Border MainWindow::Pill(TextBlock const& text)
    {
        Ellipse dot;
        dot.Width(6);
        dot.Height(6);
        dot.VerticalAlignment(VerticalAlignment::Center);
        dot.Margin(Th(0, 1, 6, 0));
        text.FontSize(12);
        text.FontWeight(SemiBold());
        text.TextWrapping(TextWrapping::NoWrap);
        text.VerticalAlignment(VerticalAlignment::Center);
        StackPanel panel;
        panel.Orientation(Orientation::Horizontal);
        panel.Children().Append(dot);
        panel.Children().Append(text);
        Border pill;
        pill.Child(panel);
        pill.CornerRadius(Radius(10));
        pill.Padding(Th(8, 2, 9, 3));
        pill.HorizontalAlignment(HorizontalAlignment::Left);
        return pill;
    }

    void MainWindow::TintPill(Border const& pill, Swatch& swatch)
    {
        pill.Background(swatch.Soft);
        auto children = pill.Child().as<StackPanel>().Children();
        children.GetAt(0).as<Ellipse>().Fill(swatch.Solid);
        children.GetAt(1).as<TextBlock>().Foreground(swatch.Solid);
    }

    /// 7px 색 점 + 글자 (패널 안 확인 사항)
    StackPanel MainWindow::DotLabel(TextBlock const& text, Brush const& ink)
    {
        Ellipse dot;
        dot.Fill(ink);
        dot.Width(7);
        dot.Height(7);
        dot.VerticalAlignment(VerticalAlignment::Center);
        dot.Margin(Th(0, 1, 8, 0));
        text.VerticalAlignment(VerticalAlignment::Center);
        text.Foreground(ink);
        StackPanel panel;
        panel.Orientation(Orientation::Horizontal);
        panel.Children().Append(dot);
        panel.Children().Append(text);
        return panel;
    }

    /// AccentButton을 다른 색으로 (홈의 '이체파일 만들기'는 파랑)
    void MainWindow::TintAccentButton(Button const& button, Swatch& swatch)
    {
        auto resources = button.Resources();
        SolidColorBrush white(winrt::Microsoft::UI::Colors::White()), pressed(Color{ 220, 255, 255, 255 });
        resources.Insert(box_value(L"AccentButtonBackground"), swatch.Tile);
        resources.Insert(box_value(L"AccentButtonBackgroundPointerOver"), swatch.Tile);
        resources.Insert(box_value(L"AccentButtonBackgroundPressed"), swatch.Tile);
        resources.Insert(box_value(L"AccentButtonForeground"), white);
        resources.Insert(box_value(L"AccentButtonForegroundPointerOver"), white);
        resources.Insert(box_value(L"AccentButtonForegroundPressed"), pressed);
    }

    /// 가로 1px 구분선
    Border MainWindow::Divider()
    {
        Border line;
        line.Height(1);
        line.Background(divider);
        line.HorizontalAlignment(HorizontalAlignment::Stretch);
        return line;
    }
}
