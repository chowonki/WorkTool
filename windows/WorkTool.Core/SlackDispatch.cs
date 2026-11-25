using System.Globalization;
using System.Text;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace WorkTool.Core;

public static partial class SlackDispatch
{
    public const string Pending = "확인 중";

    public sealed record ApplyResult(int Applied, IReadOnlyList<string> Issues);

    public static ApplyResult Apply(string text, JsonArray rows)
    {
        var eligible = rows.Where(row => row?.Flag("cInquiry") == true)
            .ToLookup(row => row.Text("plate"), StringComparer.Ordinal);
        var replies = new Dictionary<string, long?>(StringComparer.Ordinal);
        var conflicts = new HashSet<string>(StringComparer.Ordinal);
        var issues = new List<string>();
        var lines = text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');

        for (var i = 0; i < lines.Length; i++)
        {
            var line = lines[i].Trim().Normalize(NormalizationForm.FormKC);
            if (line.Length == 0) continue;
            var match = ReplyLine().Match(line);
            if (!match.Success || !TryReadValue(match.Groups["value"].Value, out var amount))
            {
                issues.Add($"{i + 1}줄: 차량번호와 값 형식을 확인해 주세요.");
                continue;
            }

            var plate = Spaces().Replace(match.Groups["plate"].Value, "");
            if (!eligible.Contains(plate))
            {
                issues.Add($"{i + 1}줄: {plate}은(는) 현재 슬랙봇 확인 대상이 아니에요.");
                continue;
            }

            if (!replies.TryAdd(plate, amount) && replies[plate] != amount && conflicts.Add(plate))
                issues.Add($"{plate}: 서로 다른 값이 있어 반영하지 않았어요.");
        }

        var applied = 0;
        foreach (var (plate, amount) in replies)
        {
            if (conflicts.Contains(plate)) continue;
            foreach (var row in eligible[plate])
            {
                row!["slackPending"] = amount is null;
                row["slackReviewed"] = true;
                row["amount"] = amount.HasValue ? JsonValue.Create(amount.Value) : null;
                row["status"] = amount.HasValue ? "confirmed" : "slack";
                row["meaning"] = "슬랙봇 확인";
            }
            applied++;
        }

        return new ApplyResult(applied, issues);
    }

    private static bool TryReadValue(string value, out long? amount)
    {
        amount = null;
        var token = value.Trim();
        if (Spaces().Replace(token, "") == "확인중") return true;
        if (token.Equals("X", StringComparison.OrdinalIgnoreCase))
        {
            amount = 0;
            return true;
        }
        var match = MoneyValue().Match(token);
        if (!match.Success) return false;
        var digits = match.Groups["number"].Value.Replace(",", "").Replace(".", "");
        if (!long.TryParse(digits, NumberStyles.None, CultureInfo.InvariantCulture, out var number) || number > 9_007_199_254_740_991L)
            return false;
        amount = number;
        return true;
    }

    [GeneratedRegex(@"^(?<plate>(?:[가-힣]{2}\s*)?[0-9]{2,3}\s*[가-힣]\s*[0-9]{4})\s*[-–—−]\s*(?<value>.+)$")]
    private static partial Regex ReplyLine();

    [GeneratedRegex(@"^(?:₩\s*)?(?<number>[0-9]+|[0-9]{1,3}(?<separator>[,.])[0-9]{3}(?:\k<separator>[0-9]{3})*)\s*원?$")]
    private static partial Regex MoneyValue();

    [GeneratedRegex(@"\s+")]
    private static partial Regex Spaces();
}
