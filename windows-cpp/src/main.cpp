#include "pch.h"
#include "MainWindow.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Windows::UI::Color;

namespace
{
    Color Rgb(int value)
    {
        return { 255, static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value) };
    }

    // XAML 파일 없이 만드는 앱. WinUI 컨트롤의 형식 정보는 XamlControlsXamlMetaDataProvider에 맡겨요.
    struct App : ApplicationT<App, Markup::IXamlMetadataProvider>
    {
        void OnLaunched(LaunchActivatedEventArgs const&)
        {
            try
            {
                BuildResources();
                window = std::make_unique<wt::MainWindow>();
                window->Activate();
            }
            catch (hresult_error const& error)
            {
                MessageBoxW(nullptr, error.message().c_str(), L"WorkTool", MB_ICONERROR);
                Exit();
            }
        }

        void BuildResources()
        {
            auto resources = Resources();
            resources.MergedDictionaries().Append(XamlControlsResources());
            // 글꼴: 앱에 넣은 Pretendard(설치 없이 같은 모양). 차량목록·원문은 고정폭.
            hstring pretendard = L"ms-appx:///Assets/Fonts/Pretendard-Regular.otf#Pretendard";
            resources.Insert(box_value(L"AppFont"), Media::FontFamily(pretendard));
            resources.Insert(box_value(L"ContentControlThemeFontFamily"), Media::FontFamily(pretendard));
            resources.Insert(box_value(L"MonoFont"), Media::FontFamily(L"Cascadia Mono, Consolas, " + pretendard));
            // 강조색은 WorkTool 녹색. AccentButton·선택 막대·체크 상자가 모두 이 색을 써요.
            auto theme = [](int accent, int light1, int content, int border)
            {
                ResourceDictionary dictionary;
                auto color = [&dictionary](wchar_t const* key, int value) { dictionary.Insert(box_value(key), box_value(Rgb(value))); };
                color(L"SystemAccentColor", accent);
                color(L"SystemAccentColorDark1", 0x267358);
                color(L"SystemAccentColorDark2", 0x1F6049);
                color(L"SystemAccentColorDark3", 0x174A38);
                color(L"SystemAccentColorLight1", light1);
                color(L"SystemAccentColorLight2", 0x4FB38C);
                color(L"SystemAccentColorLight3", 0x7CCCAA);
                dictionary.Insert(box_value(L"NavigationViewContentBackground"), Media::SolidColorBrush(Rgb(content)));
                dictionary.Insert(box_value(L"NavigationViewContentGridBorderBrush"), Media::SolidColorBrush(Rgb(border)));
                return dictionary;
            };
            resources.ThemeDictionaries().Insert(box_value(L"Light"), theme(0x267358, 0x2E8A69, 0xFBFBFB, 0xE5E5E5));
            resources.ThemeDictionaries().Insert(box_value(L"Dark"), theme(0x4FB38C, 0x3E9C78, 0x272727, 0x3A3A3A));
        }

        Markup::IXamlType GetXamlType(Windows::UI::Xaml::Interop::TypeName const& type) { return provider.GetXamlType(type); }
        Markup::IXamlType GetXamlType(hstring const& name) { return provider.GetXamlType(name); }
        com_array<Markup::XmlnsDefinition> GetXmlnsDefinitions() { return provider.GetXmlnsDefinitions(); }

        XamlTypeInfo::XamlControlsXamlMetaDataProvider provider;
        std::unique_ptr<wt::MainWindow> window;
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment(apartment_type::single_threaded);
    Application::Start([](auto&&) { make<App>(); });
    return 0;
}
