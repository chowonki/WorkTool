#pragma once

#include "Rules.h"
#include <windows.h>
#undef GetCurrentTime
#undef GetObject
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <functional>

namespace worktool
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Windows::Foundation;

    using Action = std::function<void()>;

    TextBlock Label(std::wstring_view text, double size = 14);
    StackPanel Column(double spacing = 12);
    StackPanel Line(double spacing = 8);
    Button ButtonFor(std::wstring_view text, Action action, bool accent = false);
    TextBox Input(std::wstring_view title, std::wstring_view value = {}, bool multiline = false);
    void Name(UIElement const& element, std::wstring_view name);
    ScrollViewer Scroll(UIElement const& content, bool horizontal = false);
    void Add(Grid const& grid, UIElement const& child, int column, int row = 0);
    void Place(Grid const& grid, UIElement const& child, double width);
    void ClipboardText(std::wstring_view text);
    void ClipboardPng(Bytes const& bytes);
    std::wstring Join(JsonArray const& items, std::wstring_view separator = L"\n");
    std::wstring Trim(std::wstring_view text);
    std::wstring NormalizeLines(std::wstring_view text);
    void Put(JsonObject const& row, std::wstring_view key, std::wstring_view value);
    std::wstring ErrorText();

    struct Ui
    {
        Window window{nullptr};
        Grid root{nullptr};
        HWND hwnd{};
        Rules rules;
        LocalState state;

        IAsyncOperation<ContentDialogResult> Ask(std::wstring_view title, IInspectable const& content,
            std::wstring_view primary = {}, std::wstring_view close = L"닫기");
        std::vector<fs::path> PickExcelFiles();
        fs::path PickSave(std::wstring const& name, std::wstring const& extension);
    };
}
