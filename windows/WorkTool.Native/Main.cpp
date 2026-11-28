#include "Dispatch.h"
#include "Transfer.h"
#include "Capture.h"
#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <fstream>
#include <array>
#include <ranges>

using namespace worktool;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Markup;
using namespace Microsoft::UI::Xaml::XamlTypeInfo;

namespace
{
    class WorkToolApp : public ApplicationT<WorkToolApp, IXamlMetadataProvider>
    {
    public:
        explicit WorkToolApp(fs::path startupReport) : startupReport_(std::move(startupReport)) {}
        static inline int startupCode{};

        IXamlType GetXamlType(Windows::UI::Xaml::Interop::TypeName const& type) { return Provider().GetXamlType(type); }
        IXamlType GetXamlType(hstring const& name) { return Provider().GetXamlType(name); }
        com_array<XmlnsDefinition> GetXmlnsDefinitions() { return Provider().GetXmlnsDefinitions(); }

        void OnLaunched(LaunchActivatedEventArgs const&)
        {
            try { Open(); }
            catch (...)
            {
                startupCode = 1;
                auto message = phase_ + L"\n" + ErrorText();
                if (!startupReport_.empty()) std::ofstream(startupReport_) << to_string(hstring(message)) << '\n';
                else MessageBoxW(nullptr, message.c_str(), L"WorkTool", MB_OK | MB_ICONERROR);
                Exit();
            }
        }

    private:
        XamlControlsXamlMetaDataProvider Provider()
        {
            if (!provider_) provider_ = XamlControlsXamlMetaDataProvider{};
            return provider_;
        }

        void Open()
        {
            phase_ = L"WinUI 테마";
            XamlControlsXamlMetaDataProvider::Initialize();
            Resources().MergedDictionaries().Append(XamlControlsResources{});
            phase_ = L"WorkTool 창";
            ui_ = std::make_unique<Ui>();
            auto& ui = *ui_;
            ui.window = Window{};
            ui.window.Title(L"WorkTool");
            ui.window.SystemBackdrop(MicaBackdrop{});
            check_hresult(ui.window.as<::IWindowNative>()->get_WindowHandle(&ui.hwnd));
            ui.root = Grid{};
            ui.window.Content(ui.root);
            try { SetTheme(ui.state.Theme(), false); }
            catch (...) { SetTheme(L"system", false); }
            phase_ = L"탁송체크";
            dispatch_ = std::make_unique<Dispatch>(ui);
            phase_ = L"하나은행 이체파일";
            transfer_ = std::make_unique<Transfer>(ui);
            phase_ = L"WorkTool 탐색";
            BuildNavigation();
            auto appWindow = ui.window.AppWindow();
            appWindow.SetIcon(hstring((AppDirectory() / L"WorkTool.ico").wstring()));
            auto scale = GetDpiForWindow(ui.hwnd) / 96.0;
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromWindow(ui.hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
            {
                auto workWidth = monitor.rcWork.right - monitor.rcWork.left;
                auto workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
                auto width = std::min(static_cast<long>(1180 * scale), workWidth);
                auto height = std::min(static_cast<long>(790 * scale), workHeight);
                appWindow.MoveAndResize({monitor.rcWork.left + (workWidth - width) / 2,
                    monitor.rcWork.top + (workHeight - height) / 2, width, height});
            }
            appWindow.Closing([this](auto const&, AppWindowClosingEventArgs const& event)
            {
                if (allowClose_) return;
                if (dispatch_->Busy() || transfer_->Busy())
                {
                    event.Cancel(true);
                    return;
                }
                if (transfer_->Unsaved())
                {
                    event.Cancel(true);
                    ConfirmClose();
                }
            });
            if (!startupReport_.empty())
            {
                ui.root.Loaded([this](auto const&, auto const&)
                {
                    ui_->root.DispatcherQueue().TryEnqueue([this]
                    {
                        std::ofstream(startupReport_) << "PASS WinUI window loaded\n";
                        ui_->window.Close();
                    });
                });
            }
            ui.window.Activate();
        }

        static NavigationViewItem Nav(std::wstring_view label, std::wstring_view tag, Symbol symbol)
        {
            NavigationViewItem item;
            item.Content(box_value(hstring(label)));
            item.Tag(box_value(hstring(tag)));
            SymbolIcon icon;
            icon.Symbol(symbol);
            item.Icon(icon);
            return item;
        }

        StackPanel Home()
        {
            auto column = Column(18);
            column.Children().Append(Label(L"WorkTool", 36));
            column.Children().Append(Label(L"탁송 상태를 조회하거나 하나은행 이체파일을 만드세요.", 16));
            auto dispatch = Column(8);
            dispatch.Children().Append(Label(L"탁송체크", 23));
            dispatch.Children().Append(Label(L"차량목록과 한빛탁송·오픈마일·렌톡 엑셀을 대조해 상태와 비용을 확인하세요."));
            dispatch.Children().Append(ButtonFor(L"탁송체크 열기", [this] { navigation_.SelectedItem(dispatchItem_); }, true));
            auto transfer = Column(8);
            transfer.Children().Append(Label(L"하나은행 이체파일", 23));
            transfer.Children().Append(Label(L"Slack 요청에서 이체 정보를 추출하고 업로드용 Excel을 저장하세요."));
            transfer.Children().Append(ButtonFor(L"이체파일 열기", [this] { navigation_.SelectedItem(transferItem_); }));
            for (auto const& section : {dispatch, transfer})
            {
                Border card;
                card.CornerRadius({8, 8, 8, 8});
                card.Padding({20, 15, 20, 17});
                card.MaxWidth(660);
                card.HorizontalAlignment(HorizontalAlignment::Left);
                card.Background(Resources().Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Brush>());
                card.Child(section);
                column.Children().Append(card);
            }
            return column;
        }

        StackPanel Help()
        {
            auto page = Column(14);
            page.Children().Append(Label(L"사용 방법", 30));
            page.Children().Append(Label(L"탁송체크", 20));
            page.Children().Append(Label(L"차량목록과 업체 .xlsx를 넣고 차량 조회하기를 누르세요. "
                L"확인 필요 목록에서 탁송일정과 비용이 중첩된 건을 선택할 수 있어요. "
                L"슬랙봇 확인에 ‘차량번호 - 값’을 붙여넣으면 X는 0원, 확인 중은 그대로 반영해요."));
            page.Children().Append(Label(L"조회 표의 차량번호·일정·비용을 누르면 해당 값이 복사돼요. "
                L"체크한 차량은 캡처와 TXT·Excel·PDF 저장 범위에 우선 적용돼요."));
            page.Children().Append(Label(L"하나은행 이체파일", 20));
            page.Children().Append(Label(L"Slack 스레드를 붙여넣고 이체정보를 추출하세요. 편집 창에서 원문과 "
                L"은행·계좌·금액을 대조한 뒤 미리보기를 열어 Excel을 저장하세요. 받은 분과 금액은 은행 업로드 전에 확인하세요."));
            return page;
        }

        StackPanel Settings()
        {
            auto page = Column(14);
            page.Children().Append(Label(L"설정", 30));
            ComboBox theme;
            theme.Header(box_value(L"화면 모드"));
            for (auto name : {L"시스템 설정", L"라이트", L"다크"}) theme.Items().Append(box_value(name));
            auto current = ui_->root.RequestedTheme();
            theme.SelectedIndex(current == ElementTheme::Light ? 1 : current == ElementTheme::Dark ? 2 : 0);
            auto status = Label(L"");
            theme.SelectionChanged([this, status](auto const& sender, auto const&)
            {
                try
                {
                    auto index = sender.as<ComboBox>().SelectedIndex();
                    SetTheme(index == 1 ? L"light" : index == 2 ? L"dark" : L"system", true);
                }
                catch (...) { status.Text(hstring(ErrorText())); }
            });
            page.Children().Append(theme);
            page.Children().Append(status);
            return page;
        }

        void SetTheme(std::wstring_view mode, bool persist)
        {
            ui_->root.RequestedTheme(mode == L"light" ? ElementTheme::Light : mode == L"dark" ? ElementTheme::Dark : ElementTheme::Default);
            if (persist) ui_->state.Theme(mode);
        }

        void BuildNavigation()
        {
            navigation_ = NavigationView{};
            navigation_.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
            navigation_.IsSettingsVisible(false);
            navigation_.PaneDisplayMode(NavigationViewPaneDisplayMode::Auto);
            navigation_.OpenPaneLength(208);
            navigation_.CompactPaneLength(48);
            auto home = Nav(L"홈", L"home", Symbol::Home);
            dispatchItem_ = Nav(L"탁송체크", L"dispatch", Symbol::List);
            transferItem_ = Nav(L"하나은행 이체파일", L"transfer", Symbol::Document);
            auto help = Nav(L"사용 방법", L"help", Symbol::Help);
            auto changelog = Nav(L"변경 이력", L"changelog", Symbol::Clock);
            auto settings = Nav(L"설정", L"settings", Symbol::Setting);
            for (auto const& item : {home, dispatchItem_, transferItem_}) navigation_.MenuItems().Append(item);
            for (auto const& item : {help, changelog, settings}) navigation_.FooterMenuItems().Append(item);
            auto host = Grid{};
            constexpr std::array tags{L"home", L"dispatch", L"transfer", L"help", L"changelog", L"settings"};
            auto changes = Column();
            changes.Children().Append(Label(L"변경 이력", 30));
            auto data = ReadBytes(AppDirectory() / L"CHANGELOG.md");
            for (auto content = to_hstring(std::string_view(reinterpret_cast<char const*>(data.data()), data.size()));
                auto const& line : std::wstring_view(content) | std::views::split(L'\n'))
            {
                std::wstring text(line.begin(), line.end());
                if (text.starts_with(L"## ")) changes.Children().Append(Label(Trim(text.substr(3)), 20));
                else if (text.starts_with(L"### ")) changes.Children().Append(Label(Trim(text.substr(4)), 17));
                else if (text.starts_with(L"- ")) changes.Children().Append(Label(L"• " + Trim(text.substr(2))));
            }
            std::array<UIElement, tags.size()> views{Home(), dispatch_->View(), transfer_->View(), Help(), changes, Settings()};
            for (auto const& view : views)
            {
                view.as<FrameworkElement>().Margin({30, 23, 30, 34});
                auto page = Scroll(view);
                page.Visibility(Visibility::Collapsed);
                host.Children().Append(page);
            }
            host.Children().GetAt(0).Visibility(Visibility::Visible);
            navigation_.Content(host);
            navigation_.SelectionChanged([host, tags](auto const&, NavigationViewSelectionChangedEventArgs const& event)
            {
                auto item = event.SelectedItem().try_as<NavigationViewItem>();
                if (!item) return;
                auto key = unbox_value<hstring>(item.Tag());
                for (uint32_t i = 0; i < tags.size(); ++i)
                    host.Children().GetAt(i).Visibility(key == tags[i] ? Visibility::Visible : Visibility::Collapsed);
            });
            ui_->root.Children().Append(navigation_);
            navigation_.SelectedItem(home);
        }

        fire_and_forget ConfirmClose()
        {
            if (closePrompt_) co_return;
            closePrompt_ = true;
            try
            {
                if (co_await ui_->Ask(L"WorkTool을 닫을까요?", Label(L"저장하지 않은 이체 내역이 지워져요."),
                    L"닫기", L"계속 작업") == ContentDialogResult::Primary)
                {
                    allowClose_ = true;
                    ui_->window.Close();
                }
            }
            catch (hresult_error const&) {}
            closePrompt_ = false;
        }

        std::unique_ptr<Ui> ui_;
        std::unique_ptr<Dispatch> dispatch_;
        std::unique_ptr<Transfer> transfer_;
        NavigationView navigation_{nullptr};
        NavigationViewItem dispatchItem_{nullptr}, transferItem_{nullptr};
        XamlControlsXamlMetaDataProvider provider_{nullptr};
        fs::path startupReport_;
        std::wstring phase_;
        bool closePrompt_{}, allowClose_{};
    };

    int Smoke(fs::path const& output, fs::path const& repo)
    {
        fs::create_directories(output);
        std::ofstream log(output / L"smoke-result.txt");
        auto expect = [&](bool passed, char const* description)
        {
            log << (passed ? "PASS " : "FAIL ") << description << std::endl;
            if (!passed) throw hresult_error(E_FAIL, to_hstring(description));
        };
        try
        {
            Rules rules(true);
            auto fixtures = repo / L"scripts" / L"fixtures";
            JsonArray files;
            for (auto name : {L"한빛탁송_가상.xlsx", L"오픈마일_가상.xlsx", L"렌톡_가상.xlsx"})
            {
                auto file = rules.Vendor(ReadBytes(fixtures / name), name, L"");
                expect(file.GetNamedArray(L"records").Size() > 0, "vendor workbook records");
                files.Append(file);
            }
            auto encrypted = ReadBytes(repo / L"windows/WorkTool.Tests/Fixtures/encrypted.xlsx");
            expect(Encrypted(encrypted), "encrypted workbook signature");
            auto unlocked = rules.Vendor(encrypted, L"encrypted.xlsx", L"fixture-password");
            expect(unlocked.GetNamedArray(L"records").Size() == 1, "encrypted workbook records");
            bool wrongPasswordRejected{};
            try { rules.Vendor(encrypted, L"encrypted.xlsx", L"incorrect"); }
            catch (hresult_error const&) { wrongPasswordRejected = true; }
            expect(wrongPasswordRejected, "wrong workbook password rejected");

            auto input = ReadBytes(fixtures / L"차량목록.txt");
            rules.Set("__input", NormalizeLines(to_hstring(std::string_view(reinterpret_cast<char const*>(input.data()), input.size()))));
            rules.Set("__files", files);
            auto rows = rules.Json("DispatchCore.match(DispatchCore.parseInput(__input).vehicles,__files).map(r=>({...r,_label:DispatchCore.statusLabel(r)}))").GetArray();
            expect(rows.Size() && rows.Size() == rules.Json("DispatchCore.parseInput(__input).vehicles").GetArray().Size(), "dispatch query");
            rules.Set("__rows", rows);
            auto reply = rules.Json("(()=>{const rows=[{plate:'12가3456',cInquiry:true},{plate:'34나5678',cInquiry:true},{plate:'56다7890',cInquiry:true}];const r=DispatchCore.applySlackReplies('12가3456 - X\\n34나5678 - 12.350원\\n56다7890 - 확인 중',rows);return r.applied===3&&rows[0].amount===0&&rows[1].amount===12350&&rows[2].slackPending})()");
            expect(reply.GetBoolean(), "Slack replies preserve zero, separators and pending");
            auto txt = to_string(rules.Json("DispatchCore.textExport(__rows)").GetString());
            expect(txt.starts_with("\xef\xbb\xbf"), "dispatch TXT UTF-8 BOM");
            WriteBytes(output / L"dispatch.txt", {reinterpret_cast<uint8_t const*>(txt.data()), txt.size()});
            auto xlsx = rules.DispatchExcel(rows);
            expect(xlsx.size() > 4 && xlsx[0] == 'P' && xlsx[1] == 'K', "dispatch Excel ZIP signature");
            WriteBytes(output / L"dispatch.xlsx", xlsx);
            auto png = CapturePng(rows, L"가상 자료");
            expect(png.size() > 8 && png[0] == 0x89 && png[1] == 'P', "dispatch PNG signature");
            WriteBytes(output / L"dispatch.png", png);
            auto pdf = CapturePdf(rows, L"가상 자료");
            expect(pdf.size() > 8 && pdf[0] == '%' && pdf[1] == 'P', "dispatch PDF signature");
            WriteBytes(output / L"dispatch.pdf", pdf);

            rules.Set("__raw", L"12가3456\n하나\n12345678901234\n1억 2천만원");
            auto transfer = rules.Json("(Transfer.setRows([]),Transfer.splitAll(Transfer.extractThread(__raw,0,'test','').added))").GetArray();
            expect(transfer.Size() == 2 && Text(transfer.GetObjectAt(0), L"amount") == L"100000000" &&
                Text(transfer.GetObjectAt(1), L"amount") == L"20000000", "transfer extraction and split");
            rules.Set("__rows", transfer);
            auto checks = rules.Json("Transfer.check(__rows)").GetArray();
            expect(Text(checks.GetObjectAt(0), L"state") == L"warn", "transfer review required after split");
            auto bank = rules.TransferExcel(transfer);
            expect(bank.size() > 4 && bank[0] == 'P' && bank[1] == 'K', "Hana Excel ZIP signature");
            WriteBytes(output / L"hana.xlsx", bank);
            log << "PASS native smoke test\n";
            return 0;
        }
        catch (...)
        {
            log << "ERROR " << to_string(hstring(ErrorText())) << '\n';
            return 1;
        }
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        init_apartment(apartment_type::single_threaded);
        int count{};
        auto args = std::unique_ptr<LPWSTR[], decltype(&LocalFree)>(CommandLineToArgvW(GetCommandLineW(), &count), LocalFree);
        if (count == 4 && std::wstring_view(args[1]) == L"--smoke-test") return Smoke(args[2], args[3]);
        fs::path startupReport;
        if (count == 3 && std::wstring_view(args[1]) == L"--ui-smoke") startupReport = args[2];
        Application::Start([startupReport](auto const&) { make<WorkToolApp>(startupReport); });
        return WorkToolApp::startupCode;
    }
    catch (...)
    {
        auto message = ErrorText();
        MessageBoxW(nullptr, message.c_str(), L"WorkTool", MB_OK | MB_ICONERROR);
        return 1;
    }
}
