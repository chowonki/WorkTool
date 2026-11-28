#pragma once
#include "Rules.h"
#include "Spreadsheets.h"
#include "Ui.h"

namespace wt
{
    /// 한 가지 색: 글자·아이콘(Solid), 알약 배경(Soft, 14%), 색 타일(위가 살짝 밝은 그라데이션)
    struct Swatch
    {
        Swatch(int light, int dark);
        void Apply(bool darkTheme, bool highContrast, winrt::Windows::UI::Color foreground, winrt::Windows::UI::Color background);
        muxm::SolidColorBrush Solid, Soft;
        muxm::LinearGradientBrush Tile;
    private:
        int light, dark;
    };

    // WinUI 3 창 하나로 된 앱. 화면은 모두 코드로 만들어요.
    //   MainWindow.cpp      창·탐색 창·공용 도구·홈·사용법·변경 이력·설정
    //   Colors.cpp          색과 화면 모드
    //   DispatchPage.cpp    탁송체크 화면과 표
    //   DispatchOptions.cpp 탁송체크 필터·열 너비·파일 목록·슬랙봇 회신·캡처
    //   TransferPage.cpp    하나은행 이체파일 화면
    //   TransferOptions.cpp 이체파일 입력칸·필터·편집·단축키
    //   SmokeTest.cpp       가상 자료로 하는 화면 검증 (--smoke-test)
    class MainWindow
    {
    public:
        MainWindow();
        void Activate() { window.Activate(); }

    private:
        using Action = std::function<void()>;
        using AsyncAction = std::function<Async()>;
        struct StatusKey { std::wstring status, label; bool slackPending{}; };
        struct StepMark { Border circle{ nullptr }; TextBlock number{ nullptr }; Swatch* color{}; };
        struct TransferRowView
        {
            FrameworkElement card{ nullptr }; CheckBox selection{ nullptr }; TextBlock status{ nullptr };
            Border pill{ nullptr }; TextBlock why{ nullptr }; JObj row{ nullptr };
        };
        struct Field { wchar_t const* key; wchar_t const* label; };

        // ── 창 ──
        mux::Window window{ nullptr };
        winrt::Microsoft::UI::Windowing::AppWindow appWindow{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{ nullptr };
        Grid root{ nullptr }, header{ nullptr }, pageHost{ nullptr };
        muxc::NavigationView navigation{ nullptr };
        muxc::InfoBar notice{ nullptr };
        Rules rules;
        std::map<std::wstring, FrameworkElement> pages;
        std::map<std::wstring, muxc::NavigationViewItem> navItems;
        std::map<std::wstring, TextBlock> navCounts;
        std::map<int, StepMark> stepMarks;
        std::wstring currentNav;
        bool syncingNav{}, allowClose{}, confirmingClose{};
        std::optional<std::filesystem::path> smokeDirectory;

        void BuildWindow();
        void UpdateCaptionColors();
        void BuildNavigation();
        muxc::NavigationViewItem NavItem(std::wstring const& key, std::wstring_view label, muxc::IconElement const& icon, bool count, UIElement const& lead);
        static Grid NavContent(std::wstring_view label, UIElement const& lead, TextBlock const& number);
        void Select(std::wstring const& key, bool activate = true);
        void ActivateNav(std::wstring const& key);
        void ShowPage(std::wstring const& key);
        void Go(std::wstring const& key);
        void UpdateDispatchNav();
        void UpdateTransferNav();
        winrt::fire_and_forget ConfirmClose();
        HWND Handle();

        // ── 공용 도구 ──
        TextBlock Muted(std::wstring_view value, double size = 12);
        Border Card(UIElement const& child);
        Button AsyncButton(std::wstring_view title, AsyncAction action, bool primary = false);
        AppBarButton Command(std::wstring_view label, std::wstring_view glyph, Action action, Swatch* color = nullptr);
        AppBarButton CommandAsync(std::wstring_view label, std::wstring_view glyph, AsyncAction action, Swatch* color = nullptr);
        void Notify(std::wstring_view message, bool error = false);
        void Guard(Action const& action);                 // 오류를 알림으로 보여 줘요
        winrt::fire_and_forget Run(AsyncAction action);   // 비동기 작업 + 오류 알림
        winrt::fire_and_forget RunButton(Button button, AsyncAction action);
        StackPanel PageTitle(std::wstring_view title, TextBlock const& subtitle);
        Border StatusLine(UIElement const& left, FrameworkElement const& right);
        Border SidePanel(UIElement const& content);
        ContentDialog Dialog(std::wstring_view title, UIElement const& content, std::wstring_view primary);
        winrt::Windows::Foundation::IAsyncOperation<hstring> SavePicker(std::wstring name, std::wstring extension);
        void Copy(std::wstring const& value, Action onCopy = nullptr);
        static std::wstring CopyPreview(std::wstring const& value);
        Grid TableHeader(std::vector<double> const& widths, std::vector<std::wstring> const& labels);
        Border TableRow(UIElement const& content, int index = 0);
        Async Delay(int milliseconds);
        FrameworkElement HomePage();
        FrameworkElement HelpPage();
        FrameworkElement ChangeLogPage();
        FrameworkElement SettingsPage();
        void LoadWorkToolTheme();

        // ── 색 ──
        winrt::Windows::UI::ViewManagement::AccessibilitySettings accessibilitySettings;
        winrt::Windows::UI::ViewManagement::UISettings colorSettings;
        muxm::SolidColorBrush divider, rowAlt, rowSelected, mutedInk, faintInk, dropHighlight, onTile, transparent;
        muxm::LinearGradientBrush headingGradient;
        Swatch green{ 0x1F7A4D, 0x6CCB98 }, accent{ 0x267358, 0x4FB38C }, teal{ 0x0F8B8D, 0x3FC7C9 }, purple{ 0x7A4CC2, 0xB28CFF },
            pink{ 0xC23F7A, 0xFF7FB4 }, indigo{ 0x4B56D2, 0x8F97FF }, brown{ 0x8A6233, 0xC9A06A }, blue{ 0x1D4FA8, 0x8AB4F8 },
            orange{ 0xA65A10, 0xE3A15B }, yellow{ 0x8A5A00, 0xE0B44C }, red{ 0xB42F2F, 0xF08A8A }, gray{ 0x6B6B6B, 0xA8A8A8 };
        void InitColors();
        void ObserveSystemColors();
        void UpdateWorkColors();
        Border Tile(Swatch& swatch, std::wstring_view glyph, double size = 20, double iconSize = 12, double radius = 5);
        StackPanel ColorHeading(std::wstring_view text, Swatch& swatch, std::wstring_view glyph, double size = 14, double tile = 18);
        Swatch& DispatchStatusSwatch(StatusKey const& row);
        static Border Pill(TextBlock const& text);
        static void TintPill(Border const& pill, Swatch& swatch);
        static StackPanel DotLabel(TextBlock const& text, muxm::Brush const& ink);
        static void TintAccentButton(Button const& button, Swatch& swatch);
        Border Divider();

        // ── 탁송체크 ──
        static inline std::vector<double> const DefaultDispatchColumns{ 134, 0, 102, 106, 96, 160, 90, 48, 64 };
        static inline std::vector<std::wstring> const DispatchHeadings{ L"차량번호", L"탁송일정", L"주유·충전비", L"탁송비", L"부가세", L"조회 상태", L"업체", L"완료", L"상세" };
        static inline std::vector<Field> const DispatchViews{
            { L"all", L"전체" }, { L"review", L"확인 필요" }, { L"confirmed", L"금액 확인" }, { L"progress", L"진행 중" }, { L"missing", L"미조회" },
            { L"jeju", L"제주 출발" }, { L"carrier", L"캐리어탁송" }, { L"slack", L"슬랙봇 문의" }, { L"slack-replies", L"슬랙 회신 반영" } };
        static inline std::vector<Field> const DispatchFields{
            { L"plate", L"차량번호" }, { L"schedule", L"탁송일정" }, { L"amount", L"주유·충전비" }, { L"dispatchFee", L"탁송비" },
            { L"vat", L"부가세" }, { L"status", L"조회 상태" }, { L"vendor", L"업체" }, { L"origin", L"출발지" }, { L"notes", L"메모" },
            { L"meaning", L"비용 구분" }, { L"carrierType", L"탁송 구분" } };
        static inline std::vector<int> const DispatchPageSizes{ 10, 30, 50, 100 };
        std::vector<double> dispatchColumns{ DefaultDispatchColumns };
        std::vector<JObj> files, results;
        std::set<std::wstring> selected, completed, copiedDispatch;
        std::map<std::wstring, int> dispatchCounts;
        TextBox vehicles{ nullptr }, search{ nullptr };
        TextBlock fileSummary{ nullptr }, dispatchSummary{ nullptr }, vehicleCount{ nullptr }, dispatchSubtitle{ nullptr }, dispatchCountText{ nullptr };
        StackPanel dispatchList{ nullptr }, dispatchEmpty{ nullptr }, dispatchFiles{ nullptr }, dispatchPageNumbers{ nullptr };
        TextBlock dispatchEmptyTitle{ nullptr }, dispatchEmptyText{ nullptr };
        ComboBox scope{ nullptr }, vendorBox{ nullptr }, stateBox{ nullptr }, dispatchPageSizeBox{ nullptr };
        Grid dispatchTable{ nullptr };
        Button previousResults{ nullptr }, nextResults{ nullptr };
        ScrollViewer dispatchScroll{ nullptr };
        Border dispatchPanel{ nullptr };
        FrameworkElement dispatchInputPanel{ nullptr };
        AppBarButton dispatchPanelToggle{ nullptr };
        std::wstring dispatchFilter{ L"all" };
        bool syncingDispatchBoxes{}, importingDispatch{};
        int dispatchPage{}, dispatchPageSize{ 30 };
        int visibleCount{}, visibleCars{};
        double visibleAmount{};
        std::wstring dispatchVendor, dispatchState, dispatchSort;
        std::map<std::wstring, std::wstring> dispatchSearches;
        winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::DateTime> dispatchFrom, dispatchTo;
        bool dispatchDescending{};
        std::wstring lastVehicleText;
        std::optional<std::wstring> pendingVehicleText;

        FrameworkElement DispatchPage();
        void ShowSlackPanel(bool slack);
        bool SlackPanelOpen() { return IsShown(slackDispatchStage); }
        void SetDispatchFilter(std::wstring const& key);
        int DispatchCount(std::wstring const& key);
        void RecountDispatch();
        void SyncDispatchBoxes();
        Async ImportFiles();
        Async AddDispatchFiles(std::vector<winrt::Windows::Storage::StorageFile> picked);
        JObj LoadVendorFile(std::filesystem::path const& path, std::wstring const& name);  // 검증용: 암호 없는 파일을 바로 읽어요
        void UpdateVehicleCount();
        void QueryDispatch();
        std::vector<JObj> VisibleResults();
        std::vector<JObj> ExportScope();
        void RenderDispatch();
        FrameworkElement CopyCell(std::wstring_view label, std::wstring const& value, std::wstring const& display, bool right = false, Action onCopy = nullptr);
        int DispatchPageCount() { return std::max(1, (visibleCount + dispatchPageSize - 1) / dispatchPageSize); }
        void ChangeDispatchPage(int page);
        void RenderDispatchPager();
        void SetDispatchPageSize(int size);
        void LoadDispatchPageSize();
        void UpdateDispatchSummary();
        static std::wstring Money(JObj const& row, std::wstring_view field);
        static double Sum(std::vector<JObj> const& rows, std::wstring_view field);
        Async ExportDispatch(std::wstring extension);
        AppBarButton PanelToggle(std::wstring_view label, std::function<FrameworkElement()> panel);
        static void SetPanelVisible(AppBarButton const& button, FrameworkElement const& panel, bool visible);
        static std::wstring RemoveBlankLines(std::wstring_view text);
        void OnVehiclesChanged();
        void SetVehiclesQuietly(std::wstring const& text, int caret);

        // 탁송체크: 필터·열 너비·파일 목록
        void LoadWorkMarks();
        void SaveWorkMarks();
        void LoadDispatchWidths();
        void UpdateDispatchHeader();
        Async EditDispatchWidths();
        void CleanVehicles();
        void RenderDispatchFiles();
        void InvalidateDispatch();
        Async ResetDispatch();
        std::vector<JObj> SelectedOrVisibleDispatch();
        void CopyDispatchNumbers();
        Async EditDispatchFilters();
        bool MatchesDispatchOptions(JObj const& row, Rules::Described const* described);
        Async CaptureDispatch(bool clipboard, bool pdf = false);

        // 탁송체크: 슬랙봇 회신
        FrameworkElement slackDispatchStage{ nullptr };
        TextBox slackReplies{ nullptr };
        TextBlock slackDispatchSummary{ nullptr }, slackReplyReport{ nullptr }, slackPageLabel{ nullptr };
        muxc::ListView slackDispatchList{ nullptr };
        Button showSlackResults{ nullptr }, previousSlackPage{ nullptr }, nextSlackPage{ nullptr };
        int slackPage{};
        FrameworkElement CreateSlackDispatchStage();
        void RefreshSlackDispatchList();
        void ApplySlackReplies();
        void ShowSlackResults();
        void CopyOutstandingSlackNumbers();
        void ClearSlackReplies();

        // ── 하나은행 이체파일 ──
        static inline std::vector<double> const TransferColumns{ 120, 178, 120, 0, 0, 95 };
        static inline std::vector<std::wstring> const TransferStepNames{ L"이체 요청 입력", L"확인·수정", L"다운로드" };
        static inline std::vector<Field> const TransferFields{
            { L"bank", L"은행" }, { L"account", L"계좌번호" }, { L"holder", L"예금주" }, { L"amount", L"금액" }, { L"plate", L"차량번호" },
            { L"outMemo", L"출금 표시" }, { L"inMemo", L"입금 표시" }, { L"cms", L"CMS코드" } };
        static inline std::vector<double> const DefaultTransferWidths{ 155, 190, 205, 146, 125, 180, 180, 140 };
        static constexpr int MaxTransferRows = 100;
        std::vector<double> transferWidths{ DefaultTransferWidths };
        std::vector<JObj> transfers;
        std::set<std::wstring> imported, selectedTransfers;
        std::vector<TransferRowView> transferViews;
        std::vector<TextBox> threadBoxes;
        std::map<std::pair<std::wstring, std::wstring>, std::function<void(std::wstring const&)>> groupedEditors;
        std::map<int, std::wstring> transferSearches;
        TextBlock transferSummary{ nullptr }, transferSubtitle{ nullptr }, transferSum{ nullptr }, transferRowCount{ nullptr }, summary3{ nullptr };
        StackPanel transferList{ nullptr }, previewRows{ nullptr }, commentList{ nullptr }, detailIssues{ nullptr }, threadInputs{ nullptr };
        TextBlock detailTitle{ nullptr }, detailSource{ nullptr };
        TextBox cmsOnExtract{ nullptr }, transferSearch{ nullptr };
        CheckBox acknowledge{ nullptr }, autoFixAccounts{ nullptr };
        Button download{ nullptr }, extractButton{ nullptr }, nextStepButton{ nullptr }, downloadCommand{ nullptr };
        Grid transferInput{ nullptr }, transferReview{ nullptr }, transferDownload{ nullptr }, transferHeaderHost{ nullptr };
        FrameworkElement transferPanel1{ nullptr }, transferPanel2{ nullptr }, transferPanel3{ nullptr }, ackSection{ nullptr };
        muxc::CommandBar transferCommands1{ nullptr }, transferCommands2{ nullptr }, transferCommands3{ nullptr };
        Border transferPanel{ nullptr };
        AppBarButton transferPanelToggle{ nullptr };
        ComboBox transferColumn{ nullptr };
        muxc::SelectorBar transferSeg{ nullptr };
        JObj detailRow{ nullptr };
        bool refreshing{}, saving{}, loadingSearch{};
        int step{ 1 }, lastBad{}, lastWarn{}, transferFilterIndex{};
        std::string savedSignature, savedThread, acknowledgedSignature;

        FrameworkElement TransferPage();
        StackPanel PanelSections(std::vector<StackPanel> const& sections);
        UIElement MemoControls();
        void ExtractTransfer();
        void AddTransfer();
        void SplitTransfers();
        std::string Signature() { return Dump(transfers); }
        std::string ThreadSignature();
        bool HasUnsavedTransfers();
        std::vector<double> TransferTableWidths();
        void BuildTransferHeader();
        void RenderTransfers();
        TextBox Edit(JObj const& row, std::wstring const& key, std::wstring_view label);
        void ValidateTransfers();
        void ShowTransferDetail(JObj const& row);
        void Step(int n);
        void UpdatePreview();
        void RenderComments();
        Async SaveTransfer();
        winrt::Windows::Foundation::IAsyncOperation<bool> ConfirmSentAgain();
        void RememberSent(std::wstring const& at);
        Async ResetTransfers();
        int TransferIndex(JObj const& row);

        void LoadTransferWidths();
        Async EditTransferWidths();
        TextBox AddThreadInput(bool first = false);
        muxc::Panel TransferFilters();
        void UpdateTransferSeg(int all, int ok, int warn, int bad);
        void ClearTransferFilters();
        bool ShowTransfer(JObj const& row, std::wstring const& state);
        Async DeleteSelectedTransfers();
        void SelectVisibleTransfers();
        std::wstring NormalizeTransferValue(std::wstring const& field, std::wstring value);
        void UpdateTransferField(JObj const& row, std::wstring const& field, std::wstring const& value);
        void CommitTransferEdit(JObj const& row, std::wstring const& field, TextBox const& box);
        void AddTransferShortcuts(Grid const& page);
        Async ShowTransferShortcuts();
        Async ShowTransferComments();

        // ── 화면 검증 ──
        Async SmokeTest(std::wstring output, std::wstring fixtures);
        Async Screenshot(std::wstring name);
        Async SetTextAndWait(TextBox box, std::wstring value);
    };
}
