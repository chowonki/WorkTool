using System.Text.Json;
using System.Text.Json.Nodes;

namespace WorkTool.WinUI;

// 이 기기에 남기는 작은 상태 파일. 포터블 실행(portable.mode)이면 ZIP 옆 Data 폴더에, 아니면 LocalAppData에 둬요.
internal static class LocalState
{
    public static string DataFile(string name)
    {
        var appDirectory = AppContext.BaseDirectory;
        if (File.Exists(Path.Combine(appDirectory, "portable.mode")))
            return Path.GetFullPath(Path.Combine(appDirectory, "..", "Data", name));
        return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WorkTool", name);
    }

    private static string Today => DateTime.Now.ToString("yyyy-MM-dd");

    // 오늘 날짜로만 유지하는 목록. 날짜가 바뀌면 비어 있는 것으로 봐요.
    public static JsonObject ReadToday(string name)
    {
        try
        {
            var path = DataFile(name);
            if (File.Exists(path) && JsonNode.Parse(File.ReadAllText(path)) is JsonObject saved && saved["date"]?.ToString() == Today)
                return saved;
            if (File.Exists(path)) File.Delete(path);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException) { }
        return new JsonObject { ["date"] = Today };
    }

    public static void WriteToday(string name, JsonObject value)
    {
        try
        {
            var path = DataFile(name);
            value["date"] = Today;
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            var temp = path + ".tmp";
            File.WriteAllText(temp, value.ToJsonString());
            File.Move(temp, path, true);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
    }

    public static void Delete(string name)
    {
        try { File.Delete(DataFile(name)); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
    }

    public static IEnumerable<string> Strings(JsonNode? node) =>
        node is JsonArray array ? array.Select(x => x?.GetValueKind() == JsonValueKind.String ? x.GetValue<string>() : null).OfType<string>() : [];
}
