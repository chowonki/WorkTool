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

    private FrameworkElement CreateSlackDispatchStage()
    {
        slackReplies = Input("슬랙봇 확인 내용", "08구8701 - X\n77다1234 - 12,350원\n12가3456 - 확인 중");
        slackReplies.AcceptsReturn = true;
        slackReplies.TextWrapping = TextWrapping.Wrap;
        slackReplies.Height = 176;
        slackReplyReport = Text("");
        slackReplyReport.Visibility = Visibility.Collapsed;
        slackDispatchSummary = Text("");
        slackDispatchList = new ListView { SelectionMode = ListViewSelectionMode.None, IsItemClickEnabled = false };
        ScrollViewer.SetVerticalScrollBarVisibility(slackDispatchList, ScrollBarVisibility.Disabled);
        ScrollViewer.SetVerticalScrollMode(slackDispatchList, ScrollMode.Disabled);
        previousSlackPage = Button("이전", () => ChangeSlackPage(-1));
        nextSlackPage = Button("다음", () => ChangeSlackPage(1));
        slackPageLabel = Text("");
        showSlackResults = Button("반영된 차량 표에서 보기", ShowSlackResults);
        var entry = Card(Stack(Text("슬랙봇 회신 붙여넣기", 21),
            Text("한 줄에 차량번호 - 값 형식으로 입력해 주세요. 금액은 10원 단위로 반올림하고, X는 0원, 확인 중은 그대로 반영돼요."),
            slackReplies, WrapActions(Button("확인 내용 반영", ApplySlackReplies, true), showSlackResults,
                Button("다음: 복사·저장", () => ShowDispatchStage(DispatchStage.Save))),
            slackReplyReport));
        var list = Card(Stack(Text("확인 대상", 21), slackDispatchSummary,
            Button("미확인 차량번호 복사", CopyOutstandingSlackNumbers), slackDispatchList,
            WrapActions(previousSlackPage, slackPageLabel, nextSlackPage)));
        return VerticalScroll(SplitLayout(entry, list, 820));
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
        slackReplyReport.Foreground = result.Issues.Count > 0 ? attentionTone.Ink : successTone.Ink;
        slackReplyReport.Visibility = Visibility.Visible;
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
        filter.SelectedItem = filter.Items.OfType<ComboBoxItem>().First(item => Equals(item.Tag, "slack-replies"));
        RenderDispatch();
        ShowDispatchStage(DispatchStage.Review);
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
