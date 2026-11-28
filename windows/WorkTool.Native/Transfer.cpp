#include "Transfer.h"
#include <array>
#include <format>

namespace worktool
{
    namespace
    {
        constexpr uint32_t limit = 100;
        constexpr std::array fields{L"bank", L"account", L"amount", L"plate", L"outMemo", L"inMemo", L"cms"};
        constexpr std::array titles{L"은행", L"계좌번호", L"금액 (원)", L"차량번호", L"출금통장표시", L"입금통장표시", L"CMS 코드"};
        constexpr std::array widths{112., 153., 113., 100., 117., 117., 97.};
        constexpr std::array filters{L"전체 상태", L"오류", L"검토", L"정상"};
        constexpr std::array filterKeys{L"", L"bad", L"warn", L"ok"};
        constexpr wchar_t sentLog[] = L"transfer-sent-today-v1";

        JsonArray Copy(JsonArray const& rows) { return JsonArray::Parse(rows.Stringify()); }

        std::wstring Won(JsonObject const& row)
        {
            auto amount = Text(row, L"amount");
            if (amount.find_first_not_of(L"0123456789") != std::wstring::npos) return amount;
            for (int i = static_cast<int>(amount.size()) - 3; i > 0; i -= 3) amount.insert(i, L",");
            return amount;
        }
    }

    Transfer::Transfer(Ui& ui) : ui_(ui)
    {
        banks_ = ui_.rules.Json("Transfer.banks.flatMap(g=>g[1])").GetArray();
        view_.Children().Append(Label(L"하나은행 이체파일", 30));
        view_.Children().Append(Label(L"Slack 요청을 추출하고 내역을 확인한 뒤 은행 업로드용 Excel을 저장하세요."));
        auto steps = Line();
        steps.Children().Append(ButtonFor(L"1. 이체 요청 입력", [this] { Guard([this] { Show(1); }); }));
        steps.Children().Append(ButtonFor(L"2. 추출 결과 확인", [this] { Guard([this] { Show(2); }); }));
        steps.Children().Append(ButtonFor(L"3. 파일 다운로드", [this] { Guard([this] { Show(3); }); }));
        steps.Children().Append(ButtonFor(L"전체 초기화", [this] { Reset(); }));
        view_.Children().Append(Scroll(steps, true));
        view_.Children().Append(status_);
        view_.Children().Append(inputStep_);
        view_.Children().Append(reviewStep_);
        view_.Children().Append(downloadStep_);

        inputStep_.Children().Append(threadList_);
        cms_.PlaceholderText(L"새로 추출하는 전체 내역에 적용");
        cms_.MaxLength(30);
        inputStep_.Children().Append(cms_);
        auto extract = Line();
        extract.Children().Append(ButtonFor(L"입력칸 추가", [this] { Guard([this] { AddThread(); }); }));
        extract.Children().Append(ButtonFor(L"이체정보 추출하기", [this] { Guard([this] { Extract(); }); }, true));
        inputStep_.Children().Append(extract);
        using namespace Microsoft::UI::Xaml::Input;
        KeyboardAccelerator shortcut;
        shortcut.Key(Windows::System::VirtualKey::Enter);
        shortcut.Modifiers(Windows::System::VirtualKeyModifiers::Control);
        shortcut.Invoked([this](auto const&, KeyboardAcceleratorInvokedEventArgs const& args)
        {
            if (inputStep_.Visibility() == Visibility::Visible)
            {
                Guard([this] { Extract(); });
                args.Handled(true);
            }
        });
        inputStep_.KeyboardAccelerators().Append(shortcut);

        reviewStep_.Children().Append(summary_);
        auto reviewActions = Line();
        for (auto filter : filters) filter_.Items().Append(box_value(hstring(filter)));
        filter_.SelectedIndex(0);
        Name(filter_, L"검토 상태");
        filter_.SelectionChanged([this](auto const&, auto const&) { Guard([this] { Review(); }); });
        search_.Header(nullptr);
        search_.PlaceholderText(L"차량·은행·계좌 검색");
        Name(search_, L"차량·은행·계좌 검색");
        search_.Width(185);
        search_.TextChanged([this](auto const&, auto const&) { Guard([this] { Review(); }); });
        reviewActions.Children().Append(filter_);
        reviewActions.Children().Append(search_);
        reviewActions.Children().Append(ButtonFor(L"내역 추가", [this] { Edit(0, true); }));
        reviewActions.Children().Append(ButtonFor(L"선택 삭제", [this] { DeleteSelected(); }));
        reviewActions.Children().Append(ButtonFor(L"메모·CMS 일괄 적용", [this] { Bulk(); }));
        reviewActions.Children().Append(ButtonFor(L"1억 초과 분할", [this] { Guard([this] { Split(); }); }));
        reviewStep_.Children().Append(Scroll(reviewActions, true));
        reviewStep_.Children().Append(Scroll(reviewTable_, true));
        reviewStep_.Children().Append(ButtonFor(L"파일 미리보기", [this] { Guard([this] { Show(3); }); }, true));

        downloadStep_.Children().Append(previewSummary_);
        downloadStep_.Children().Append(Scroll(previewTable_, true));
        comments_.IsReadOnly(true);
        comments_.Height(105);
        downloadStep_.Children().Append(comments_);
        downloadStep_.Children().Append(ButtonFor(L"송금 코멘트 복사", [this] { Guard([this]
        {
            if (comments_.Text().empty()) return;
            ClipboardText(comments_.Text());
            status_.Text(L"송금 코멘트를 복사했어요.");
        }); }));
        acknowledge_.Content(box_value(L"검토가 필요한 내역을 원문과 대조했어요."));
        acknowledge_.Click([this](auto const&, auto const&) { UpdateDownload(); });
        downloadStep_.Children().Append(acknowledge_);
        download_ = ButtonFor(L"하나은행 이체파일 저장", [this] { Download(); }, true);
        downloadStep_.Children().Append(download_);
        AddThread();
        Show(1);
    }

    void Transfer::Guard(Action action)
    {
        if (busy_) return;
        try { action(); }
        catch (...) { status_.Text(hstring(ErrorText())); }
    }

    void Transfer::Show(int step)
    {
        if (step == 3 && !rows_.Size())
        {
            status_.Text(L"먼저 이체정보를 추출하거나 내역을 추가해 주세요.");
            return;
        }
        inputStep_.Visibility(step == 1 ? Visibility::Visible : Visibility::Collapsed);
        reviewStep_.Visibility(step == 2 ? Visibility::Visible : Visibility::Collapsed);
        downloadStep_.Visibility(step == 3 ? Visibility::Visible : Visibility::Collapsed);
        if (step == 2) Review();
        if (step == 3) Preview();
    }

    void Transfer::AddThread()
    {
        auto id = Text(ui_.rules.Json("Transfer.newId()"));
        auto input = Input(L"Slack 스레드", {}, true);
        input.PlaceholderText(L"Slack 이체 요청의 원문을 붙여넣으세요.");
        threads_.push_back({id, {}, input});
        RenderThreads();
    }

    void Transfer::RenderThreads()
    {
        threadList_.Children().Clear();
        for (size_t i = 0; i < threads_.size(); ++i)
        {
            auto block = Column(5);
            auto& thread = threads_[i];
            if (auto previous = thread.input.Parent().try_as<StackPanel>()) previous.Children().Clear();
            thread.input.Header(box_value(hstring(L"Slack 스레드 " + std::to_wstring(i + 1))));
            block.Children().Append(thread.input);
            if (threads_.size() > 1)
                block.Children().Append(ButtonFor(L"이 입력칸 제거", [this, i]
                {
                    Guard([this, i] { threads_.erase(threads_.begin() + i); RenderThreads(); });
                }));
            threadList_.Children().Append(block);
        }
    }

    void Transfer::Extract()
    {
        auto next = Copy(rows_);
        std::vector<std::wstring> imported;
        std::wstring warnings;
        ui_.rules.Set("__cms", Trim(cms_.Text()));
        std::set<std::wstring> seen;
        for (auto const& thread : threads_)
            if (!thread.imported.empty() && thread.imported == Trim(NormalizeLines(thread.input.Text()))) seen.insert(thread.imported);
        for (size_t i = 0; i < threads_.size(); ++i)
        {
            auto const& thread = threads_[i];
            auto raw = Trim(NormalizeLines(thread.input.Text()));
            imported.push_back(thread.imported);
            if (raw.empty() || raw == thread.imported) continue;
            if (!seen.insert(raw).second)
            {
                warnings += L"스레드 " + std::to_wstring(i + 1) + L" · 같은 내용이라 건너뛰었어요.\n";
                imported.back() = raw;
                continue;
            }
            ui_.rules.Set("__rows", next);
            ui_.rules.Set("__raw", raw);
            ui_.rules.Set("__thread", thread.id);
            ui_.rules.Set("__index", JsonValue::CreateNumberValue(static_cast<double>(i)));
            auto result = ui_.rules.Json("(Transfer.setRows(__rows),Transfer.extractThread(__raw,__index,__thread,__cms))").GetObject();
            if (result.GetNamedBoolean(L"empty"))
            {
                warnings += L"스레드 " + std::to_wstring(i + 1) + L" · 이체정보를 찾지 못했어요.\n";
                continue;
            }
            auto added = result.GetNamedArray(L"added");
            ui_.rules.Set("__new", added);
            auto split = ui_.rules.Json("Transfer.splitAll(__new)").GetArray();
            for (auto const& row : split) next.Append(row);
            imported.back() = raw;
        }
        if (next.Size() > limit)
            throw hresult_error(E_INVALIDARG, L"나눈 금액을 포함해 한 파일에 100건까지 만들 수 있어요.");
        for (size_t i = 0; i < threads_.size(); ++i) threads_[i].imported = imported[i];
        auto count = next.Size() - rows_.Size();
        if (count)
        {
            rows_ = next;
            Recheck();
            Show(2);
        }
        auto message = count ? L"새 " + std::to_wstring(count) + L"건을 추가했어요. 원문과 맞는지 확인해 주세요." :
            L"새로 추출할 이체정보가 없어요.";
        status_.Text(hstring(message + (warnings.empty() ? L"" : L"\n" + warnings)));
    }

    void Transfer::Recheck()
    {
        ui_.rules.Set("__rows", rows_);
        checks_ = ui_.rules.Json("Transfer.check(__rows)").GetArray();
        errors_ = warnings_ = 0;
        for (auto const& item : checks_)
        {
            auto state = Text(item.GetObject(), L"state");
            if (state == L"bad") ++errors_;
            if (state == L"warn") ++warnings_;
        }
        acknowledge_.IsChecked(box_value(false).as<IReference<bool>>());
        auto summary = L"총 " + std::to_wstring(rows_.Size()) + L"건 · 오류 " + std::to_wstring(errors_) +
            L"건 · 검토 " + std::to_wstring(warnings_) + L"건 · 합계 ";
        summary += Text(ui_.rules.Json("Transfer.won(__rows.reduce((a,r)=>a+(/^\\d+$/.test(r.amount)&&Number.isSafeInteger(Number(r.amount))?Number(r.amount):0),0))")) + L"원";
        summary_.Text(hstring(summary));
        previewSummary_.Text(hstring(summary));
        UpdateDownload();
    }

    void Transfer::Review()
    {
        if (reviewStep_.Visibility() != Visibility::Visible) return;
        if (!rows_.Size()) summary_.Text(L"이체 내역이 없어요. Slack에서 추출하거나 내역을 추가하세요.");
        reviewTable_.Children().Clear();
        Grid header;
        Place(header, Label(L"선택"), 50);
        Place(header, Label(L"검토"), 220);
        for (size_t i = 0; i < titles.size(); ++i) Place(header, Label(titles[i]), widths[i]);
        Place(header, Label(L"원문"), 100);
        reviewTable_.Children().Append(header);
        auto filter = filterKeys.at(std::max(0, filter_.SelectedIndex()));
        auto query = Trim(search_.Text());
        for (uint32_t i = 0; i < rows_.Size(); ++i)
        {
            auto row = rows_.GetObjectAt(i);
            auto check = checks_.GetObjectAt(i);
            auto state = Text(check, L"state");
            if (*filter && state != filter) continue;
            if (!query.empty() && (Text(row, L"plate") + L" " + Text(row, L"bank") + L" " + Text(row, L"account")).find(query) == std::wstring::npos) continue;
            auto id = Text(row, L"id");
            Grid grid;
            CheckBox select;
            select.MinWidth(0);
            select.IsChecked(box_value(selected_.contains(id)).as<IReference<bool>>());
            Name(select, std::to_wstring(i + 1) + L"행 선택");
            select.Click([this, id, select = make_weak(select)](auto const&, auto const&)
            {
                if (auto control = select.get(); control && control.IsChecked().Value()) selected_.insert(id);
                else selected_.erase(id);
            });
            Place(grid, select, 50);
            auto messages = Join(check.GetNamedArray(L"errors"), L" · ");
            auto reviews = Join(check.GetNamedArray(L"reviews"), L" · ");
            if (!reviews.empty()) messages += (messages.empty() ? L"" : L" · ") + reviews;
            Place(grid, Label(messages.empty() ? L"정상" : messages, 13), 220);
            for (size_t k = 0; k < fields.size(); ++k)
            {
                auto value = k == 2 ? Won(row) : Text(row, fields[k]);
                auto button = ButtonFor(value.empty() ? L"입력" : value, [this, i] { Edit(i, false); });
                Name(button, std::wstring(titles[k]) + L" " + value + L" 편집");
                Place(grid, button, widths[k]);
            }
            Place(grid, ButtonFor(L"원문·편집", [this, i] { Edit(i, false); }), 100);
            reviewTable_.Children().Append(grid);
        }
    }

    fire_and_forget Transfer::Edit(uint32_t index, bool add)
    {
        if (busy_) co_return;
        try
        {
            if (add && rows_.Size() >= limit) throw hresult_error(E_INVALIDARG, L"이체파일은 최대 100건까지 만들 수 있어요.");
            JsonObject row = add ? JsonObject{} : rows_.GetObjectAt(index);
            if (add) Put(row, L"cms", Trim(cms_.Text()));
            auto panel = Column(8);
            ComboBox bank;
            bank.Header(box_value(L"입금은행"));
            bank.HorizontalAlignment(HorizontalAlignment::Stretch);
            for (auto const& value : banks_) bank.Items().Append(box_value(value.GetString()));
            auto current = Text(row, L"bank");
            for (uint32_t i = 0; i < banks_.Size(); ++i)
                if (banks_.GetStringAt(i) == current) bank.SelectedIndex(i);
            panel.Children().Append(bank);
            std::array<TextBox, fields.size() - 1> inputs;
            for (size_t i = 0; i < inputs.size(); ++i)
            {
                inputs[i] = Input(titles[i + 1], Text(row, fields[i + 1]));
                panel.Children().Append(inputs[i]);
            }
            auto source = Text(row, L"ctx");
            if (source.empty()) source = Text(row, L"source");
            if (!source.empty())
            {
                auto raw = Input(L"Slack 원문", source, true);
                raw.IsReadOnly(true);
                panel.Children().Append(raw);
            }
            auto content = Scroll(panel);
            content.MinWidth(310);
            content.MaxHeight(520);
            if (co_await ui_.Ask(add ? L"이체 내역 추가" : L"이체 내역 편집", content, L"반영", L"취소") != ContentDialogResult::Primary) co_return;
            if (add) Put(row, L"id", Text(ui_.rules.Json("Transfer.newId()")));
            Put(row, L"bank", bank.SelectedIndex() < 0 ? L"" : unbox_value<hstring>(bank.SelectedItem()).c_str());
            auto oldPlate = Text(row, L"plate");
            auto newPlate = Trim(inputs[2].Text());
            for (size_t i = 0; i < inputs.size(); ++i)
            {
                auto value = Trim(inputs[i].Text());
                if (i == 1)
                {
                    std::erase(value, L',');
                    std::erase(value, L'원');
                }
                if ((i == 3 || i == 4) && value == Text(row, fields[i + 1]) && (value.empty() || value == oldPlate))
                    value = newPlate;
                Put(row, fields[i + 1], Trim(value));
            }
            if (add) rows_.Append(row);
            Recheck();
            Review();
        }
        catch (...) { status_.Text(hstring(ErrorText())); }
    }

    fire_and_forget Transfer::Bulk()
    {
        if (busy_ || !rows_.Size()) co_return;
        try
        {
            auto panel = Column(8);
            ComboBox field;
            field.Header(box_value(L"적용할 항목"));
            for (auto title : {L"출금통장표시", L"입금통장표시", L"CMS 코드", L"계좌번호 하이픈·공백 정리"})
                field.Items().Append(box_value(title));
            field.SelectedIndex(0);
            auto input = Input(L"적용할 값");
            CheckBox empty;
            empty.Content(box_value(L"빈 칸에만 적용"));
            empty.IsChecked(box_value(true).as<IReference<bool>>());
            field.SelectionChanged([input, empty](auto const& sender, auto const&)
            {
                auto visibility = sender.as<ComboBox>().SelectedIndex() == 3 ? Visibility::Collapsed : Visibility::Visible;
                input.Visibility(visibility);
                empty.Visibility(visibility);
            });
            panel.Children().Append(field);
            panel.Children().Append(input);
            panel.Children().Append(empty);
            if (co_await ui_.Ask(L"이체 내역 일괄 적용", panel, L"적용", L"취소") != ContentDialogResult::Primary) co_return;
            auto selected = field.SelectedIndex();
            for (auto const& value : rows_)
            {
                auto row = value.GetObject();
                if (selected == 3)
                {
                    auto account = Text(row, L"account");
                    std::erase_if(account, [](wchar_t ch) { return ch == L'-' || iswspace(ch); });
                    Put(row, L"account", account);
                }
                else
                {
                    auto key = fields.at(selected + 4);
                    if (!empty.IsChecked().Value() || Text(row, key).empty()) Put(row, key, Trim(input.Text()));
                }
            }
            Recheck();
            Review();
        }
        catch (...) { status_.Text(hstring(ErrorText())); }
    }

    fire_and_forget Transfer::DeleteSelected()
    {
        if (busy_) co_return;
        if (selected_.empty())
        {
            status_.Text(L"삭제할 줄을 선택해 주세요.");
            co_return;
        }
        try
        {
            if (co_await ui_.Ask(L"선택한 이체 내역 삭제", Label(std::to_wstring(selected_.size()) + L"건을 삭제할까요?"),
                L"삭제", L"취소") != ContentDialogResult::Primary) co_return;
            for (uint32_t i = rows_.Size(); i > 0; --i)
                if (selected_.contains(Text(rows_.GetObjectAt(i - 1), L"id"))) rows_.RemoveAt(i - 1);
            selected_.clear();
            Recheck();
            Review();
        }
        catch (...) { status_.Text(hstring(ErrorText())); }
    }

    void Transfer::Split()
    {
        ui_.rules.Set("__rows", rows_);
        auto split = ui_.rules.Json("Transfer.splitAll(__rows)").GetArray();
        if (split.Size() > limit) throw hresult_error(E_INVALIDARG, L"금액을 나누면 이체파일의 최대 100건을 넘어요.");
        rows_ = split;
        Recheck();
        Review();
    }

    void Transfer::Preview()
    {
        ui_.rules.Set("__rows", rows_);
        auto preview = ui_.rules.Json("Transfer.fileRows(__rows)").GetArray();
        previewTable_.Children().Clear();
        constexpr std::array columns{125., 160., 125., 135., 135., 110.};
        uint32_t index = 0;
        for (auto const& value : preview)
        {
            auto cells = value.GetArray();
            Grid row;
            for (uint32_t i = 0; i < cells.Size(); ++i)
            {
                auto cell = i == 2 && index > 0 ? Won(rows_.GetObjectAt(index - 1)) : Text(cells.GetAt(i));
                Place(row, Label(cell), columns.at(i));
            }
            previewTable_.Children().Append(row);
            ++index;
        }
        comments_.Text(hstring(Join(ui_.rules.Json("Transfer.comments(__rows)").GetArray())));
        UpdateDownload();
    }

    void Transfer::UpdateDownload()
    {
        acknowledge_.Visibility(warnings_ ? Visibility::Visible : Visibility::Collapsed);
        download_.IsEnabled(!busy_ && rows_.Size() && !errors_ && (!warnings_ || acknowledge_.IsChecked().Value()) && Unsaved());
    }

    fire_and_forget Transfer::Download()
    {
        if (busy_ || !download_.IsEnabled()) co_return;
        try
        {
            ui_.rules.Set("__rows", rows_);
            auto serialized = rows_.Stringify();
            auto keys = ui_.rules.Json("Transfer.sentKeys(__rows)").GetArray();
            auto history = ui_.state.Today(sentLog);
            auto prior = history.GetNamedArray(L"items", JsonArray{});
            std::wstring duplicate;
            for (uint32_t i = 0; i < keys.Size(); ++i)
            {
                auto const key = keys.GetStringAt(i);
                for (auto const& item : prior)
                {
                    auto entry = item.GetObject();
                    if (Text(entry, L"key") != key) continue;
                    auto row = rows_.GetObjectAt(i);
                    duplicate += Text(row, L"bank") + L" " + Text(row, L"account") + L" · " +
                        Won(row) + L"원 · " + Text(entry, L"at") + L"\n";
                    break;
                }
            }
            if (!duplicate.empty())
            {
                auto message = Scroll(Label(L"같은 은행·계좌·금액이 오늘 저장된 파일에 들어 있어요.\n\n" + duplicate));
                message.MaxHeight(280);
                if (co_await ui_.Ask(L"오늘 이미 받은 이체 건이 있어요", message, L"그래도 저장", L"취소")
                    != ContentDialogResult::Primary) co_return;
            }
            ui_.rules.Set("__stamp", Stamp(L"%Y%m%d_%H%M"));
            auto path = ui_.PickSave(Text(ui_.rules.Json("Transfer.fileName(__rows,__stamp)")), L"xlsx");
            if (path.empty()) co_return;
            busy_ = true;
            UpdateDownload();
            auto bytes = ui_.rules.TransferExcel(rows_);
            WriteBytes(path, bytes);
            saved_ = serialized;
            history = ui_.state.Today(sentLog);
            auto items = history.GetNamedArray(L"items", JsonArray{});
            for (auto const& key : keys)
            {
                JsonObject entry;
                entry.SetNamedValue(L"key", key);
                Put(entry, L"at", Stamp(L"%H:%M"));
                items.Append(entry);
            }
            while (items.Size() > 2000) items.RemoveAt(0);
            history.SetNamedValue(L"items", items);
            try
            {
                ui_.state.SaveToday(sentLog, history);
                status_.Text(L"이체파일을 저장했어요. 은행 업로드 전에 받는 분과 금액을 다시 확인해 주세요.");
            }
            catch (...) { status_.Text(L"이체파일을 저장했어요. 오늘의 중복 확인 기록을 저장하지 못했어요."); }
        }
        catch (...) { status_.Text(hstring(ErrorText())); }
        busy_ = false;
        UpdateDownload();
    }

    fire_and_forget Transfer::Reset()
    {
        if (busy_) co_return;
        try
        {
            if (Unsaved() && co_await ui_.Ask(L"이체 내역 초기화", Label(L"저장하지 않은 이체 내역이 지워져요."),
                L"초기화", L"취소") != ContentDialogResult::Primary) co_return;
            rows_.Clear();
            threads_.clear();
            cms_.Text(L"");
            selected_.clear();
            saved_ = hstring{};
            filter_.SelectedIndex(0);
            search_.Text(L"");
            AddThread();
            Recheck();
            status_.Text(L"이체 요청과 검토 내역을 초기화했어요.");
            Show(1);
        }
        catch (...) { status_.Text(hstring(ErrorText())); }
    }
}
