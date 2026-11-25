using System.Text.Json.Nodes;
using ExcelDataReader;
using WorkTool.Core;

var rules = new Rules();
System.Text.Encoding.RegisterProvider(System.Text.CodePagesEncodingProvider.Instance);
var passed = 0;
void Assert(bool condition, string message) { if (!condition) throw new Exception(message); passed++; Console.WriteLine("PASS " + message); }
var parsed = rules.ParseTransfer("123가4567\n국민은행\n123-456-789012\n3만5천원");
var row = parsed["items"]![0]!;
Assert(row.Text("bank") == "국민" && row.Text("account") == "123456789012" && row.Text("amount") == "35000" && row.Text("plate") == "123가4567", "Jint parses Korean request exactly");
Assert(rules.ParseTransfer("123가4567\r국민은행\r001234567890\r3만5천원")["items"]![0].Text("amount") == "35000", "WinUI carriage returns normalize to line feeds");
foreach (var bad in new[] { "3.5억", "-35000원", "35,00원", "1/2만원", "1e5원", "35000원 / 40000원" })
    Assert(rules.ParseTransfer("국민은행\n123-456-789012\n" + bad)["items"]![0].Text("amount") == "", "reject ambiguous amount " + bad);
var records = parsed["items"]!.DeepClone().AsArray();
records[0]!["id"] = "one"; records[0]!["outMemo"] = "123가4567"; records[0]!["inMemo"] = "123가4567"; records[0]!["cms"] = "";
Assert(rules.Check(records)[0].Text("state") == "ok", "valid transfer accepted");
records.Add(records[0]!.DeepClone()); records[1]!["id"] = "two";
Assert(rules.Check(records).All(c => c.Text("state") == "warn"), "duplicate transfer requires review");
records.RemoveAt(1); records[0]!["amount"] = "120000000";
records = rules.Split(records);
Assert(records.Count == 2 && records[0].Text("amount") == "100000000" && records[1].Text("amount") == "20000000", "1억 limit splits exactly");
Assert(rules.Check(records).All(c => c.Text("state") == "warn"), "split transfers require review");
Assert(rules.Comments(records).Contains("송금완료"), "comment generation executes without DOM");
var vehicles = rules.ParseVehicles("2026-11-25 10:00 123가4567\n45나6789\n2026-02-30 10:00 12가3456");
Assert(vehicles["vehicles"]!.AsArray().Count == 2 && vehicles["invalid"]!.AsArray().Count == 1, "invalid calendar date rejected");
var head = new JsonArray(); var body = new JsonArray();
for (var i = 0; i < 45; i++) { head.Add(""); body.Add(""); }
head[0] = "차량번호"; head[1] = "상태"; head[2] = "주유/부대비용 총액"; head[3] = "출발지 주소";
body[0] = "123가4567"; body[1] = "탁송완료"; body[2] = "35004"; body[10] = "2026-11-25"; body[11] = "10:00"; body[41] = "50000";
var sheets = new JsonArray { new JsonObject { ["name"] = "한빛", ["rows"] = new JsonArray(head, body) } };
var file = rules.ReadSheets(sheets, "fixture.xlsx");
var results = rules.Match(vehicles["vehicles"]!.AsArray(), new JsonArray(file));
Assert(results[0].Text("amount") == "35000" && results[0].Text("vat") == "5000" && results[1].Text("status") == "missing", "dispatch rounding, VAT and missing vehicle");
Assert(rules.Filter(results, "confirmed").Count == 1, "dispatch filter uses shared rules");
var second = file.DeepClone(); second["records"]![0]!["dispatchFee"] = 60000;
var conflict = rules.Match(vehicles["vehicles"]!.AsArray(), new JsonArray(file.DeepClone(), second));
Assert(conflict[0]!.Flag("feeConflict"), "conflicting fee requires choice");
var chosen = rules.Choose(conflict[0]!, "fee", conflict[0]!["feeCandidates"]![0].Text("id"));
Assert(!chosen.Flag("feeConflict") && chosen.Text("vat") == "6000", "candidate updates VAT");
var dir = Path.Combine(Path.GetTempPath(), "WorkTool-tests-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(dir);
try
{
    var path = Path.Combine(dir, "transfer.xlsx");
    records[0]!["account"] = "001234567890";
    Spreadsheets.Write(path, Spreadsheets.TransferRows(records), "다계좌이체");
    using (var stream = File.OpenRead(path))
    using (var book = ExcelReaderFactory.CreateReader(stream))
    {
        book.Read(); book.Read();
        Assert(book.GetString(1) == "001234567890", "Excel preserves account leading zeroes");
        Assert(book.GetValue(2) is double, "bank amount is numeric cell");
    }
    var company = Path.Combine(dir, "company.xlsx");
    Spreadsheets.Write(company, sheets[0]!["rows"]!.AsArray());
    Assert(rules.ReadSheets(Spreadsheets.Read(company), "company.xlsx")["records"]!.AsArray().Count == 1, "native Excel import recognizes vendor");
    var secret = Path.Combine(AppContext.BaseDirectory, "Fixtures", "encrypted.xlsx");
    Assert(rules.ReadSheets(Spreadsheets.Read(secret, "fixture-password"), "encrypted.xlsx")["records"]!.AsArray().Count == 1, "encrypted Excel import");
    Assert(rules.ExportText(results).Contains("123가4567"), "dispatch text export");
}
finally { Directory.Delete(dir, true); }
Console.WriteLine($"All {passed} checks passed.");
