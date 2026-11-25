using System.Globalization;
using System.IO.Compression;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Xml;
using ExcelDataReader;

namespace WorkTool.Core;

public static class Spreadsheets
{
    public static JsonArray Read(string path, string? password = null)
    {
        if (new FileInfo(path).Length > 30_000_000) throw new InvalidDataException("파일은 30MB까지 불러올 수 있어요.");
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
        using var stream = File.OpenRead(path);
        using var reader = ExcelReaderFactory.CreateReader(stream, new ExcelReaderConfiguration { Password = password ?? "" });
        var sheets = new JsonArray(); long cells = 0;
        do
        {
            if (reader.RowCount > 100_030) throw new InvalidDataException("한 시트는 100,000행까지 지원해요.");
            var rows = new JsonArray();
            while (reader.Read())
            {
                var values = new JsonArray(); var count = Math.Max(45, reader.FieldCount);
                if ((cells += count) > 5_000_000) throw new InvalidDataException("엑셀 데이터가 너무 커요. 필요한 시트만 포함해 주세요.");
                for (var j = 0; j < count; j++)
                {
                    var value = j < reader.FieldCount ? reader.GetValue(j) : null;
                    values.Add(value switch
                    {
                        double n => JsonValue.Create(n), int n => JsonValue.Create(n),
                        DateTime date => JsonValue.Create(date.ToOADate()), TimeSpan time => JsonValue.Create(time.TotalDays),
                        bool b => JsonValue.Create(b ? "Y" : "N"), _ => JsonValue.Create(value?.ToString() ?? "")
                    });
                }
                rows.Add(values);
            }
            sheets.Add(new JsonObject { ["name"] = reader.Name, ["rows"] = rows });
        } while (reader.NextResult());
        return sheets;
    }
    public static void Write(string path, JsonArray values, string sheetName = "조회 결과")
    {
        using var stream = File.Create(path);
        using var zip = new ZipArchive(stream, ZipArchiveMode.Create);
        void Entry(string name, string xml)
        { using var writer = new StreamWriter(zip.CreateEntry(name).Open(), new UTF8Encoding(false)); writer.Write(xml); }
        Entry("[Content_Types].xml", """
        <?xml version="1.0" encoding="utf-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>
        """);
        Entry("_rels/.rels", """
        <Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>
        """);
        Entry("xl/_rels/workbook.xml.rels", """
        <Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>
        """);
        const string ns = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
        using (var xml = XmlWriter.Create(zip.CreateEntry("xl/workbook.xml").Open(), new XmlWriterSettings { Encoding = new UTF8Encoding(false), CloseOutput = true }))
        {
            xml.WriteStartElement("workbook", ns); xml.WriteStartElement("sheets", ns); xml.WriteStartElement("sheet", ns);
            xml.WriteAttributeString("name", sheetName); xml.WriteAttributeString("sheetId", "1");
            xml.WriteAttributeString("r", "id", "http://schemas.openxmlformats.org/officeDocument/2006/relationships", "rId1");
            xml.WriteEndElement(); xml.WriteEndElement(); xml.WriteEndElement();
        }
        using var sheet = XmlWriter.Create(zip.CreateEntry("xl/worksheets/sheet1.xml").Open(), new XmlWriterSettings { Encoding = new UTF8Encoding(false), CloseOutput = true });
        sheet.WriteStartElement("worksheet", ns); sheet.WriteStartElement("sheetData", ns);
        for (var i = 0; i < values.Count; i++)
        {
            sheet.WriteStartElement("row", ns); sheet.WriteAttributeString("r", (i + 1).ToString(CultureInfo.InvariantCulture));
            var items = values[i]!.AsArray();
            for (var j = 0; j < items.Count; j++)
            {
                sheet.WriteStartElement("c", ns); sheet.WriteAttributeString("r", Column(j) + (i + 1));
                if (i > 0 && items[j] is JsonValue v && v.GetValueKind() == JsonValueKind.Number)
                { sheet.WriteStartElement("v", ns); sheet.WriteString(v.ToJsonString()); sheet.WriteEndElement(); }
                else
                {
                    sheet.WriteAttributeString("t", "inlineStr"); sheet.WriteStartElement("is", ns); sheet.WriteStartElement("t", ns);
                    sheet.WriteAttributeString("xml", "space", "http://www.w3.org/XML/1998/namespace", "preserve");
                    sheet.WriteString(SafeText(items[j]?.ToString() ?? "")); sheet.WriteEndElement(); sheet.WriteEndElement();
                }
                sheet.WriteEndElement();
            }
            sheet.WriteEndElement();
        }
        sheet.WriteEndElement(); sheet.WriteEndElement();
    }
    private static string Column(int i) { var s = ""; for (i++; i > 0; i = (i - 1) / 26) s = (char)('A' + (i - 1) % 26) + s; return s; }
    private static string SafeText(string text)
    {
        var result = new StringBuilder();
        foreach (var rune in text.EnumerateRunes()) if (rune.Value is 9 or 10 or 13 || rune.Value >= 32 && rune.Value is not 0xFFFE and not 0xFFFF) result.Append(rune);
        return result.ToString();
    }
    public static JsonArray TransferRows(JsonArray rows)
    {
        var values = new JsonArray { new JsonArray("입금은행", "입금계좌번호", "입금액(원)", "출금통장표시", "입금통장표시", "CMS코드") };
        foreach (var r in rows)
            values.Add(new JsonArray(r.Text("bank"), r.Text("account"), long.Parse(r.Text("amount"), CultureInfo.InvariantCulture), r.Text("outMemo"), r.Text("inMemo"), r.Text("cms")));
        return values;
    }
}
