using System.Text.Json;
using System.Text.Json.Nodes;
using Jint;

namespace WorkTool.Core;

// No browser, DOM, CLR access or network. Only embedded, repository-owned rules run.
public sealed class Rules
{
    private readonly Engine dispatch = Create("dispatch");
    private readonly Engine transfer = Create("transfer");
    private static Engine Create(string name)
    {
        var engine = new Engine(o => o.LimitMemory(128_000_000).TimeoutInterval(TimeSpan.FromSeconds(30)));
        using var source = typeof(Rules).Assembly.GetManifestResourceStream($"WorkTool.Core.Rules.{name}.js")!;
        engine.Execute(new StreamReader(source).ReadToEnd());
        return engine;
    }
    private static JsonNode Eval(Engine e, string expression) => JsonNode.Parse(e.Evaluate($"JSON.stringify({expression})").AsString())!;
    private static string Json(object? value) => JsonSerializer.Serialize(value);
    public string[] Banks => Eval(transfer, "BANK_OPTIONS").AsArray().Select(n => n!.GetValue<string>()).ToArray();
    public JsonObject ParseTransfer(string text) => Eval(transfer, $"parseOne({Json(text.Replace("\r\n", "\n").Replace('\r', '\n'))})").AsObject();
    public JsonArray Split(JsonArray rows) => Eval(transfer, $"splitAll({rows.ToJsonString()})").AsArray();
    public JsonArray Check(JsonArray rows)
    {
        transfer.Execute($"rows={rows.ToJsonString()};");
        return Eval(transfer, "rows.map(check)").AsArray();
    }
    public string Comments(JsonArray rows)
    {
        transfer.Execute($"rows={rows.ToJsonString()};");
        return string.Join(Environment.NewLine, Eval(transfer, "buildComments()").AsArray().Select(x => x!.GetValue<string>()));
    }
    public JsonObject ParseVehicles(string text) => Eval(dispatch, $"DispatchCore.parseInput({Json(text)})").AsObject();
    public JsonObject ReadSheets(JsonArray sheets, string name) => Eval(dispatch, $"DispatchCore.readSheets({sheets.ToJsonString()},{Json(name)})").AsObject();
    public JsonArray Match(JsonArray vehicles, JsonArray files) => Eval(dispatch, $"DispatchCore.match({vehicles.ToJsonString()},{files.ToJsonString()})").AsArray();
    public JsonArray Filter(JsonArray results, string filter) => Eval(dispatch, $"{results.ToJsonString()}.filter(r=>DispatchCore.includes(r,{Json(filter)}))").AsArray();
    public string Status(JsonNode row) => Eval(dispatch, $"DispatchCore.statusLabel({row.ToJsonString()})").GetValue<string>();
    public string Notes(JsonNode row) => Eval(dispatch, $"DispatchCore.notes({row.ToJsonString()})").GetValue<string>();
    public JsonObject Choose(JsonNode row, string field, string id)
    {
        dispatch.Execute($"globalThis.chosen={row.ToJsonString()}; DispatchCore.useCandidate(chosen,{Json(field)},{Json(id)});");
        return Eval(dispatch, "chosen").AsObject();
    }
    public JsonArray ExportRows(JsonArray results)
        => Eval(dispatch, $"[DispatchCore.headers,...DispatchCore.unique({results.ToJsonString()}).map(DispatchCore.values)]").AsArray();
    public string ExportText(JsonArray results)
        => Eval(dispatch, $"DispatchCore.textExport({results.ToJsonString()})").GetValue<string>();
}

public static class NodeText
{
    public static string Text(this JsonNode? n, string key) => n?[key]?.ToString() ?? "";
    public static bool Flag(this JsonNode n, string key) => n[key]?.GetValue<bool>() == true;
}
