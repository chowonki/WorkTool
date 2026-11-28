#include "Ui.h"
#include <shobjidl.h>
#include <shlwapi.h>
#include <shcore.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.Streams.h>
#include <cwctype>

using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Storage::Streams;

namespace worktool
{
    TextBlock Label(std::wstring_view text, double size)
    {
        TextBlock label;
        label.Text(hstring(text));
        label.FontSize(size);
        label.TextWrapping(TextWrapping::Wrap);
        return label;
    }

    StackPanel Column(double spacing)
    {
        StackPanel column;
        column.Spacing(spacing);
        return column;
    }

    StackPanel Line(double spacing)
    {
        auto line = Column(spacing);
        line.Orientation(Orientation::Horizontal);
        return line;
    }

    Button ButtonFor(std::wstring_view title, Action action, bool accent)
    {
        Button button;
        button.Content(box_value(hstring(title)));
        if (accent)
            button.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
        button.Click([action = std::move(action)](auto const&, auto const&) { action(); });
        return button;
    }

    TextBox Input(std::wstring_view title, std::wstring_view value, bool multiline)
    {
        TextBox input;
        input.Header(box_value(hstring(title)));
        input.Text(hstring(value));
        input.MaxLength(multiline ? 500000 : 500);
        if (multiline)
        {
            input.AcceptsReturn(true);
            input.TextWrapping(TextWrapping::Wrap);
            input.Height(144);
        }
        return input;
    }

    void Name(UIElement const& element, std::wstring_view name)
    {
        Automation::AutomationProperties::SetName(element, hstring(name));
    }

    ScrollViewer Scroll(UIElement const& content, bool horizontal)
    {
        ScrollViewer scroll;
        scroll.Content(content);
        if (horizontal)
        {
            scroll.HorizontalScrollMode(ScrollMode::Enabled);
            scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
            scroll.VerticalScrollMode(ScrollMode::Disabled);
            scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
        }
        return scroll;
    }

    void Add(Grid const& grid, UIElement const& child, int column, int row)
    {
        Grid::SetColumn(child.as<FrameworkElement>(), column);
        Grid::SetRow(child.as<FrameworkElement>(), row);
        grid.Children().Append(child);
    }

    void Place(Grid const& grid, UIElement const& child, double width)
    {
        ColumnDefinition definition;
        definition.Width(GridLength{width, GridUnitType::Pixel});
        int column = static_cast<int>(grid.ColumnDefinitions().Size());
        grid.ColumnDefinitions().Append(definition);
        auto element = child.as<FrameworkElement>();
        element.Margin({3, 2, 3, 2});
        element.VerticalAlignment(VerticalAlignment::Center);
        Add(grid, child, column);
    }

    void ClipboardText(std::wstring_view text)
    {
        DataPackage data;
        data.SetText(hstring(text));
        Clipboard::SetContent(data);
        Clipboard::Flush();
    }

    void ClipboardPng(Bytes const& bytes)
    {
        com_ptr<IStream> stream;
        stream.attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
        if (!stream) throw hresult_error(E_OUTOFMEMORY);
        IRandomAccessStream randomAccess{nullptr};
        check_hresult(CreateRandomAccessStreamOverStream(stream.get(), BSOS_DEFAULT,
            guid_of<IRandomAccessStream>(), put_abi(randomAccess)));
        DataPackage data;
        data.SetBitmap(RandomAccessStreamReference::CreateFromStream(randomAccess));
        Clipboard::SetContent(data);
        Clipboard::Flush();
    }

    std::wstring Join(JsonArray const& items, std::wstring_view separator)
    {
        std::wstring result;
        bool first = true;
        for (auto const& item : items)
        {
            if (!first) result += separator;
            result += Text(item);
            first = false;
        }
        return result;
    }

    std::wstring Trim(std::wstring_view text)
    {
        while (!text.empty() && iswspace(text.front())) text.remove_prefix(1);
        while (!text.empty() && iswspace(text.back())) text.remove_suffix(1);
        return std::wstring(text);
    }

    std::wstring NormalizeLines(std::wstring_view text)
    {
        std::wstring result;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\r')
            {
                if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
                result += L'\n';
            }
            else result += text[i];
        }
        return result;
    }

    void Put(JsonObject const& row, std::wstring_view key, std::wstring_view value)
    {
        row.SetNamedValue(hstring(key), JsonValue::CreateStringValue(hstring(value)));
    }

    std::wstring ErrorText()
    {
        try { throw; }
        catch (hresult_error const& error) { return std::wstring(error.message()); }
        catch (std::exception const&) { return L"파일 또는 작업을 처리하지 못했어요."; }
    }

    IAsyncOperation<ContentDialogResult> Ui::Ask(std::wstring_view title, IInspectable const& content,
        std::wstring_view primary, std::wstring_view close)
    {
        ContentDialog dialog;
        dialog.XamlRoot(root.XamlRoot());
        dialog.Title(box_value(hstring(title)));
        dialog.Content(content);
        dialog.CloseButtonText(hstring(close));
        dialog.PrimaryButtonText(hstring(primary));
        if (!primary.empty()) dialog.DefaultButton(ContentDialogButton::Primary);
        return dialog.ShowAsync();
    }

    static fs::path ShellPath(IShellItem* item)
    {
        PWSTR raw{};
        check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &raw));
        std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path(raw, CoTaskMemFree);
        return fs::path(path.get());
    }

    std::vector<fs::path> Ui::PickExcelFiles()
    {
        auto dialog = create_instance<IFileOpenDialog>(CLSID_FileOpenDialog, CLSCTX_INPROC_SERVER);
        FILEOPENDIALOGOPTIONS options{};
        check_hresult(dialog->GetOptions(&options));
        check_hresult(dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM));
        COMDLG_FILTERSPEC fileType{L"Excel 통합 문서", L"*.xlsx"};
        check_hresult(dialog->SetFileTypes(1, &fileType));
        auto result = dialog->Show(hwnd);
        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
        check_hresult(result);
        com_ptr<IShellItemArray> items;
        check_hresult(dialog->GetResults(items.put()));
        DWORD count{};
        check_hresult(items->GetCount(&count));
        std::vector<fs::path> paths;
        for (DWORD i = 0; i < count; ++i)
        {
            com_ptr<IShellItem> item;
            check_hresult(items->GetItemAt(i, item.put()));
            paths.push_back(ShellPath(item.get()));
        }
        return paths;
    }

    fs::path Ui::PickSave(std::wstring const& name, std::wstring const& extension)
    {
        auto dialog = create_instance<IFileSaveDialog>(CLSID_FileSaveDialog, CLSCTX_INPROC_SERVER);
        auto pattern = L"*." + extension;
        COMDLG_FILTERSPEC fileType{name.c_str(), pattern.c_str()};
        check_hresult(dialog->SetFileTypes(1, &fileType));
        check_hresult(dialog->SetDefaultExtension(extension.c_str()));
        check_hresult(dialog->SetFileName(name.c_str()));
        auto result = dialog->Show(hwnd);
        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
        check_hresult(result);
        com_ptr<IShellItem> item;
        check_hresult(dialog->GetResult(item.put()));
        return ShellPath(item.get());
    }
}
