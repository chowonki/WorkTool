#include "pch.h"
#include "MainWindow.h"
#include <regex>

// 이체파일: 입력칸·보기 필터·칸 편집·열 너비·단축키

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Windows::System::VirtualKey;
using winrt::Windows::System::VirtualKeyModifiers;

namespace wt
{
    static wchar_t const TransferWidthsFile[] = L"transfer-columns.json";
    static wchar_t const* const SegLabels[]{ L"전체", L"정상", L"검토 필요", L"오류" };

    void MainWindow::LoadTransferWidths()
    {
        auto saved = LocalState::ReadNumbers(TransferWidthsFile);
        if (saved && saved->size() == transferWidths.size()
            && std::all_of(saved->begin(), saved->end(), [](double width) { return width >= 80 && width <= 480; }))
            transferWidths = *saved;
    }

    Async MainWindow::EditTransferWidths()
    {
        std::vector<NumberBox> boxes;
        for (size_t i = 0; i < TransferFields.size(); i++)
        {
            NumberBox box;
            box.Header(box_value(TransferFields[i].label));
            box.Value(transferWidths[i]);
            box.Minimum(80);
            box.Maximum(480);
            box.SmallChange(8);
            box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
            boxes.push_back(box);
        }
        auto content = Stack();
        for (size_t i = 0; i + 1 < boxes.size(); i += 2) content.Children().Append(Columns({ 0, 0 }, { boxes[i], boxes[i + 1] }));
        auto dialog = Dialog(L"이체 내역 열 너비", content, L"적용");
        dialog.SecondaryButtonText(L"기본값");
        auto result = co_await dialog.ShowAsync();
        if (result == ContentDialogResult::None) co_return;
        if (result == ContentDialogResult::Secondary) transferWidths = DefaultTransferWidths;
        else
            for (size_t i = 0; i < boxes.size(); i++)
                if (auto value = boxes[i].Value(); !std::isnan(value)) transferWidths[i] = std::clamp(value, 80.0, 480.0);
        BuildTransferHeader();
        RenderTransfers();
        LocalState::WriteNumbers(TransferWidthsFile, transferWidths);
    }

    // 이체 요청 입력칸: 상자 없이 입력칸 사이를 구분선으로 나눠요.
    TextBox MainWindow::AddThreadInput(bool first)
    {
        auto box = Input(L"", L"차량번호, 은행, 계좌번호, 금액이 포함된 Slack 스레드를 그대로 붙여넣으세요.");
        box.Tag(box_value(hstring(NewId())));
        box.AcceptsReturn(true);
        box.TextWrapping(TextWrapping::Wrap);
        box.Height(first ? 260 : 180);
        box.MaxLength(50000);
        auto number = std::to_wstring(threadBoxes.size() + 1);
        Name(box, L"이체 요청 " + number + L" 내용");
        Grid head;
        head.Children().Append(PanelHeading(L"이체 요청 " + number));
        auto section = Stack({ head, box });
        section.Spacing(8);
        Border border;
        border.Child(section);
        border.Padding(Th(0, first ? 0 : 16, 0, 16));
        border.BorderBrush(divider);
        border.BorderThickness(Th(0, first ? 0 : 1, 0, 0));
        if (!first)
        {
            HyperlinkButton remove;
            remove.Content(box_value(L"이 입력칸 삭제"));
            remove.HorizontalAlignment(HorizontalAlignment::Right);
            remove.Padding(Th(0));
            remove.Click([this, weakBox = make_weak(box), weakBorder = make_weak(border)](auto&&, auto&&)
            {
                auto target = weakBox.get();
                auto frame = weakBorder.get();
                if (!target || !frame) return;
                std::erase(threadBoxes, target);
                uint32_t index;
                if (threadInputs.Children().IndexOf(frame, index)) threadInputs.Children().RemoveAt(index);
            });
            head.Children().Append(remove);
        }
        threadBoxes.push_back(box);
        threadInputs.Children().Append(border);
        return box;
    }

    // 2단계 위: Pivot 모양 보기(전체 n | 정상 n | 검토 필요 n | 오류 n) + 열 검색
    Panel MainWindow::TransferFilters()
    {
        transferSeg = SelectorBar();
        for (auto label : SegLabels)
        {
            SelectorBarItem item;
            item.Text(label);
            transferSeg.Items().Append(item);
        }
        transferSeg.SelectedItem(transferSeg.Items().GetAt(0));
        transferSeg.SelectionChanged([this](auto&&, auto&&)
        {
            uint32_t index;
            auto item = transferSeg.SelectedItem();
            if (!item || !transferSeg.Items().IndexOf(item, index) || static_cast<int>(index) == transferFilterIndex) return;
            transferFilterIndex = static_cast<int>(index);
            Guard([this] { ValidateTransfers(); });
        });
        transferColumn = ComboBox();
        std::vector<std::wstring> columns{ L"모든 열" };
        for (auto const& field : TransferFields) columns.emplace_back(field.label);
        SetItems(transferColumn, columns);
        transferColumn.SelectedIndex(0);
        transferColumn.Width(120);
        transferColumn.VerticalAlignment(VerticalAlignment::Center);
        Name(transferColumn, L"검색할 열");
        transferSearch = Input(L"", L"내역 검색", 160);
        transferSearch.VerticalAlignment(VerticalAlignment::Center);
        Name(transferSearch, L"내역 검색");
        transferColumn.SelectionChanged([this](auto&&, auto&&)
        {
            loadingSearch = true;
            auto found = transferSearches.find(transferColumn.SelectedIndex());
            transferSearch.Text(found == transferSearches.end() ? L"" : found->second);
            loadingSearch = false;
        });
        transferSearch.TextChanged([this](auto&&, auto&&)
        {
            if (loadingSearch) return;
            auto query = Trim(transferSearch.Text());
            if (query.empty()) transferSearches.erase(transferColumn.SelectedIndex());
            else transferSearches[transferColumn.SelectedIndex()] = query;
            Guard([this] { ValidateTransfers(); });
        });
        auto wrap = make_self<WorkWrapPanel>();
        wrap->centerVertically = true;
        auto panel = wrap.as<Panel>();
        for (UIElement const& control : { UIElement(transferSeg), UIElement(transferColumn), UIElement(transferSearch) }) panel.Children().Append(control);
        return panel;
    }

    void MainWindow::UpdateTransferSeg(int all, int ok, int warn, int bad)
    {
        if (!transferSeg) return;
        int values[]{ all, ok, warn, bad };
        for (uint32_t i = 0; i < 4; i++)
            transferSeg.Items().GetAt(i).Text(std::wstring(SegLabels[i]) + L" " + N0(values[i]));
    }

    void MainWindow::ClearTransferFilters()
    {
        transferSearches.clear();
        transferSearch.Text(L"");
        transferColumn.SelectedIndex(0);
        transferFilterIndex = 0;
        transferSeg.SelectedItem(transferSeg.Items().GetAt(0));
        ValidateTransfers();
    }

    bool MainWindow::ShowTransfer(JObj const& row, std::wstring const& state)
    {
        wchar_t const* filterState[]{ L"", L"ok", L"warn", L"bad" };
        if (transferFilterIndex > 0 && state != filterState[transferFilterIndex]) return false;
        for (auto const& [column, query] : transferSearches)
        {
            if (column <= 0)
            {
                if (std::none_of(TransferFields.begin(), TransferFields.end(), [&](auto const& field) { return ContainsIgnoreCase(Str(row, field.key), query); })) return false;
            }
            else if (!ContainsIgnoreCase(Str(row, TransferFields[column - 1].key), query)) return false;
        }
        return true;
    }

    Async MainWindow::DeleteSelectedTransfers()
    {
        if (selectedTransfers.empty()) { Notify(L"삭제할 내역을 선택해 주세요.", true); co_return; }
        auto dialog = Dialog(L"선택한 이체 내역 삭제", Text(N0(static_cast<double>(selectedTransfers.size())) + L"건을 삭제할까요?"), L"삭제");
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        std::erase_if(transfers, [this](auto const& row) { return selectedTransfers.contains(Str(row, L"id")); });
        selectedTransfers.clear();
        RenderTransfers();
    }

    void MainWindow::SelectVisibleTransfers()
    {
        for (auto const& view : transferViews)
            if (IsShown(view.card)) view.selection.IsChecked(true);
    }

    std::wstring MainWindow::NormalizeTransferValue(std::wstring const& field, std::wstring value)
    {
        value = Trim(value);
        if (field == L"account" && IsOn(autoFixAccounts)) return std::regex_replace(value, std::wregex(LR"([\s-]+)"), L"");
        if (field == L"plate") return std::regex_replace(value, std::wregex(LR"(\s+)"), L"");
        if (field != L"amount") return value;
        static std::wregex const numeric(LR"(^₩?\s*([0-9]+|[0-9]{1,3}(?:,[0-9]{3})+)(?:\.0+)?\s*원?$)");
        std::wsmatch match;
        if (std::regex_match(value, match, numeric))
        {
            auto whole = Replace(match[1].str(), L",", L"");
            whole.erase(0, whole.find_first_not_of(L'0'));
            return whole.empty() ? L"0" : whole;
        }
        // Slack 원문 읽기는 앞뒤 글자를 봐주지만, 직접 고친 값은 애매한 구분자나 다른 글자를
        // 그대로 둬서 검사에서 걸리게 해요.
        static std::wregex const korean(LR"(^₩?[0-9영공일이삼사오육칠팔구십백천만억,\s]+원?$)");
        if (!std::regex_match(value, korean) || value.find_first_of(L"십백천만억") == std::wstring::npos) return value;
        auto parsed = rules.ParseTransferAmount(value);
        return parsed.empty() ? value : parsed;
    }

    void MainWindow::UpdateTransferField(JObj const& row, std::wstring const& field, std::wstring const& value)
    {
        if (refreshing || Str(row, field) == value) return;
        Put(row, field, value);
        // 나눈 줄은 금액만 따로, 나머지 칸은 함께 바꿔요.
        if (auto group = Str(row, L"splitGroup"); !group.empty() && field != L"amount")
        {
            refreshing = true;
            try
            {
                for (auto const& sibling : transfers)
                {
                    if (sibling == row || Str(sibling, L"splitGroup") != group) continue;
                    Put(sibling, field, value);
                    if (auto editor = groupedEditors.find({ Str(sibling, L"id"), field }); editor != groupedEditors.end()) editor->second(value);
                }
            }
            catch (...) {}
            refreshing = false;
        }
        ValidateTransfers();
    }

    void MainWindow::CommitTransferEdit(JObj const& row, std::wstring const& field, TextBox const& box)
    {
        auto index = TransferIndex(row);
        if (refreshing || index < 0) return;
        auto normalized = NormalizeTransferValue(field, std::wstring(box.Text()));
        UpdateTransferField(row, field, normalized);
        if (box.Text() != normalized) box.Text(normalized);
        if (field != L"amount" || normalized.empty() || normalized.find_first_not_of(L"0123456789") != std::wstring::npos) return;
        if (normalized.size() < 10 && std::stoll(normalized) <= 100'000'000) return;
        auto parts = rules.Split({ Clone(row) });
        if (parts.size() + transfers.size() - 1 > MaxTransferRows || parts.size() == 1)
        {
            Notify(L"금액을 나눌 수 없어요. 금액과 최대 100건 제한을 확인해 주세요.", true);
            return;
        }
        // 칸에서 나가는 처리 중에 표를 다시 그리면 그 칸이 없어지니, 끝난 뒤에 반영해요.
        queue.TryEnqueue([this, row, parts]
        {
            auto at = TransferIndex(row);
            if (at < 0) return;
            transfers.erase(transfers.begin() + at);
            transfers.insert(transfers.begin() + at, parts.begin(), parts.end());
            Guard([this] { RenderTransfers(); });
        });
    }

    void MainWindow::AddTransferShortcuts(Grid const& page)
    {
        auto shortcut = [this, &page](VirtualKey key, VirtualKeyModifiers modifiers, AsyncAction action)
        {
            Input::KeyboardAccelerator accelerator;
            accelerator.Key(key);
            accelerator.Modifiers(modifiers);
            accelerator.Invoked([this, action](auto&&, Input::KeyboardAcceleratorInvokedEventArgs const& args)
            {
                args.Handled(true);
                Run(action);
            });
            page.KeyboardAccelerators().Append(accelerator);
        };
        auto act = [](Action action) -> AsyncAction { return [action]() -> Async { action(); co_return; }; };
        auto altShift = VirtualKeyModifiers::Menu | VirtualKeyModifiers::Shift;
        shortcut(VirtualKey::Enter, VirtualKeyModifiers::Control, [this]() -> Async
        {
            auto self = this;
            if (self->step == 1) self->ExtractTransfer();
            else if (self->step == 2) self->Step(3);
            else if (self->step == 3 && self->download.IsEnabled()) co_await self->SaveTransfer();
        });
        shortcut(VirtualKey::Enter, VirtualKeyModifiers::Control | VirtualKeyModifiers::Shift, act([this] { if (step > 1) Step(step - 1); }));
        shortcut(VirtualKey::N, altShift, act([this] { if (step == 1) AddThreadInput().Focus(FocusState::Programmatic); else if (step == 2) AddTransfer(); }));
        shortcut(VirtualKey::C, altShift, act([this] { if (step == 3) Copy(Join(rules.Comments(transfers), L"\r\n")); }));
        shortcut(VirtualKey::D, altShift, [this]() -> Async { auto self = this; if (self->step == 2) co_await self->DeleteSelectedTransfers(); });
        shortcut(VirtualKey::K, altShift, [this] { return ShowTransferShortcuts(); });
    }

    Async MainWindow::ShowTransferShortcuts()
    {
        auto dialog = Dialog(L"이체파일 단축키", Text(
            L"Ctrl+Enter  추출·다음·저장\nCtrl+Shift+Enter  이전 단계\nAlt+Shift+N  입력칸·이체 내역 추가\nAlt+Shift+C  코멘트 전체 복사\nAlt+Shift+D  선택한 내역 삭제\nAlt+Shift+K  단축키 안내"),
            L"확인");
        co_await dialog.ShowAsync();
    }

    Async MainWindow::ShowTransferComments()
    {
        StackPanel list;
        list.Spacing(8);
        for (auto const& line : rules.Comments(transfers))
            if (!line.empty()) list.Children().Append(Columns({ 0, 70 }, { Text(line), MakeButton(L"복사", [this, line] { Copy(line); }) }));
        if (list.Children().Size() == 0) list.Children().Append(Text(L"이체 내역이 없어요."));
        ScrollViewer view;
        view.Content(list);
        view.MaxHeight(440);
        auto dialog = Dialog(L"송금 코멘트", view, L"닫기");
        co_await dialog.ShowAsync();
    }
}
