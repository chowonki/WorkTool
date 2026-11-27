using System.Text.Json.Nodes;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using WorkTool.Core;

namespace WorkTool.WinUI;

public sealed partial class MainWindow
{
    private FrameworkElement slackDispatchStage = null!;
    private TextBox slackReplies = null!;
    private TextBlock slackDispatchSummary = null!, slackReplyReport = null!;
    private ListView slackDispatchList = null!;
    private Button showSlackResults = null!, previousSlackPage = null!, nextSlackPage = null!;
    private TextBlock slackPageLabel = null!;
    private int slackPage;
    private const int SlackPageSize = 10;

    // 오른쪽 패널의 '슬랙봇 회신' 모드: 회신 붙여넣기 → 반영, 아래에 확인 대상 목록
    private FrameworkElement CreateSlackDispatchStage()
    {
        slackReplies = new TextBox
        {
            PlaceholderText = "08구8701 - X\n77다1234 - 12,350원\n12가3456 - 확인 중", AcceptsReturn = true, TextWrapping = TextWrapping.Wrap,
            Height = 170, FontFamily = (Microsoft.UI.Xaml.Media.FontFamily)Application.Current.Resources["MonoFont"], FontSize = 13
        };
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(slackReplies, "슬랙봇 확인 내용");
        slackReplyReport = Text("", 13);
        slackReplyReport.Visibility = Visibility.Collapsed;
        slackDispatchSummary = Muted("", 13);
        slackDispatchList = new ListView { SelectionMode = ListViewSelectionMode.None, IsItemClickEnabled = false };
        ScrollViewer.SetVerticalScrollBarVisibility(slackDispatchList, ScrollBarVisibility.Disabled);
        ScrollViewer.SetVerticalScrollMode(slackDispatchList, ScrollMode.Disabled);
        previousSlackPage = new Button { Content = new FontIcon { Glyph = "\uE76B", FontSize = 12 }, Padding = new Thickness(8, 5, 8, 5) };
        nextSlackPage = new Button { Content = new FontIcon { Glyph = "\uE76C", FontSize = 12 }, Padding = new Thickness(8, 5, 8, 5) };
        previousSlackPage.Click += (_, _) => ChangeSlackPage(-1);
        nextSlackPage.Click += (_, _) => ChangeSlackPage(1);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(previousSlackPage, "이전 페이지");
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(nextSlackPage, "다음 페이지");
        slackPageLabel = Muted("", 13);
        slackPageLabel.VerticalAlignment = VerticalAlignment.Center;
        showSlackResults = new Button { Content = "반영된 차량 표에서 보기" };
        showSlackResults.Click += (_, _) => ShowSlackResults();
        var back = new Button { Content = new FontIcon { Glyph = "\uE72B", FontSize = 14 }, Background = transparent, BorderThickness = new Thickness(0), Padding = new Thickness(6) };
        ToolTipService.SetToolTip(back, "입력으로 돌아가기");
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(back, "입력으로 돌아가기");
        back.Click += (_, _) => ShowSlackPanel(false);
        var title = ColorHeading("슬랙봇 회신", purple, "\uE8BD", 20, 22);
        var head = Bar(back, title); head.VerticalAlignment = VerticalAlignment.Center;
        var apply = Button("확인 내용 반영", ApplySlackReplies, true);
        apply.HorizontalAlignment = HorizontalAlignment.Stretch;
        var copyOutstanding = new HyperlinkButton { Content = "미확인 차량번호 복사", Padding = new Thickness(0, 4, 0, 4) };
        copyOutstanding.Click += (_, _) => CopyOutstandingSlackNumbers();
        var entry = Stack(ColorHeading("회신 붙여넣기", purple, "\uE77F"),
            Muted("한 줄에 ‘차량번호 - 값’ 형식이에요. 금액은 10원 단위로 반올림하고, X는 0원, 확인 중은 그대로 반영돼요.", 12),
            slackReplies, apply, showSlackResults, slackReplyReport);
        entry.Spacing = 8;
        var targets = Stack(ColorHeading("확인 대상", pink, "\uE8FD"), slackDispatchSummary, copyOutstanding, slackDispatchList, Bar(previousSlackPage, slackPageLabel, nextSlackPage));
        targets.Spacing = 6;
        var sep = Divider(); sep.Margin = new Thickness(-20, 6, -20, 6);
        var body = Stack(head, entry, sep, targets);
        body.Spacing = 12;
        body.Padding = new Thickness(20, 16, 20, 20);
        return VerticalScroll(body);
    }
    private void RefreshSlackDispatchList()
    {
        var eligible = results.OfType<JsonObject>().Where(row => row.Flag("cInquiry")).DistinctBy(row => row.Text("plate")).ToArray();
        var unanswered = eligible.Count(row => !row.Flag("slackReviewed"));
        var pending = eligible.Count(row => row.Flag("slackPending"));
        showSlackResults.IsEnabled = eligible.Length > unanswered;
        slackDispatchSummary.Text = eligible.Length == 0
            ? "이번 조회에는 슬랙봇 확인 대상이 없어요."
            : $"대상 {eligible.Length:N0}대 · 금액 확인 {eligible.Length - unanswered - pending:N0}대 · 확인 중 {pending:N0}대 · 미입력 {unanswered:N0}대";
        var pages = Math.Max(1, (eligible.Length + SlackPageSize - 1) / SlackPageSize);
        slackPage = Math.Clamp(slackPage, 0, pages - 1);
        previousSlackPage.IsEnabled = slackPage > 0;
        nextSlackPage.IsEnabled = slackPage + 1 < pages;
        slackPageLabel.Text = $"{slackPage + 1}/{pages}페이지";
        slackDispatchList.ItemsSource = eligible.Skip(slackPage * SlackPageSize).Take(SlackPageSize).Select(row =>
        {
            var value = !row.Flag("slackReviewed") ? "미입력" : row.Flag("slackPending")
                ? Rules.SlackPending : row.Text("amount") + "원";
            return $"{row.Text("plate")}   ·   {value}";
        }).ToArray();
    }

    private void ChangeSlackPage(int offset)
    {
        slackPage += offset;
        RefreshSlackDispatchList();
    }

    private void ApplySlackReplies()
    {
        if (string.IsNullOrWhiteSpace(slackReplies.Text))
        {
            Notify("슬랙봇 회신을 붙여넣어 주세요.", true);
            return;
        }

        var result = rules.ApplySlackReplies(slackReplies.Text, results);
        var report = new List<string> { $"{result.Applied:N0}대 반영 · 확인 필요 {result.Issues.Count:N0}건" };
        report.AddRange(result.Issues.Take(8));
        if (result.Issues.Count > 8) report.Add($"외 {result.Issues.Count - 8:N0}건");
        slackReplyReport.Text = string.Join(Environment.NewLine, report);
        slackReplyReport.Foreground = result.Issues.Count > 0 ? warningInk : successInk;
        slackReplyReport.Visibility = Visibility.Visible;
        RecountDispatch();
        RefreshSlackDispatchList();
        if (result.Applied > 0 && result.Issues.Count == 0)
        {
            ShowSlackResults();
            Notify($"슬랙봇 회신 {result.Applied:N0}대를 표에 반영했어요.");
        }
        else RenderDispatch();
    }

    private void ShowSlackResults()
    {
        // This view shows all reconciled rows, even when an earlier filter hid them.
        dispatchVendor = dispatchState = "";
        dispatchFrom = dispatchTo = null;
        dispatchSearches.Clear();
        search.Text = "";
        dispatchPage = 0;
        SyncDispatchBoxes();
        dispatchFilter = "slack-replies";
        RenderDispatch();
        Go("dispatch");
    }

    private void CopyOutstandingSlackNumbers()
    {
        var plates = results.Where(row => row?.Flag("cInquiry") == true &&
                (!row.Flag("slackReviewed") || row.Flag("slackPending")))
            .Select(row => row.Text("plate")).Distinct().ToArray();
        if (plates.Length == 0) { Notify("미확인 차량이 없어요."); return; }
        Copy(string.Join(Environment.NewLine, plates));
    }

    private void ClearSlackReplies()
    {
        slackReplies.Text = "";
        slackReplyReport.Visibility = Visibility.Collapsed;
    }
}
