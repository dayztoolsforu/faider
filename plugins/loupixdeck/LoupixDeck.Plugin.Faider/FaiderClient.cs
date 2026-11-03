using System.Net.Http.Headers;
using System.Text.Json;

namespace LoupixDeck.Plugin.Faider;

// Faider's local control API (src/engine/ControlServer.h in Faider): the desk's state polled
// four times a second, commands sent as POST with the X-Faider header. When something shown on
// a button changes, the buttons of that command are redrawn.
internal sealed class FaiderClient : IDisposable
{
    public const string Api = "http://127.0.0.1:9123/api";

    public sealed record Strip(string Name, double Fader, bool Mute, bool Solo, double Level, bool On);
    public sealed record Desk(IReadOnlyList<Strip> Inputs, IReadOnlyList<Strip> Buses, IReadOnlyList<Strip> Outputs,
                              IReadOnlyList<string> Scenes, string ActiveScene, IReadOnlyList<string> Macros);

    // The last state read, or null while Faider isn't answering.
    public Desk? State { get; private set; }

    // Called (on the polling thread) with what changed: "mute", "solo", "fader", "level", "scene" or "all".
    public event Action<string>? Changed;

    readonly HttpClient http = new() { Timeout = TimeSpan.FromSeconds(2) };
    readonly CancellationTokenSource stop = new();

    public FaiderClient()
    {
        http.DefaultRequestHeaders.Add("X-Faider", "1");
        _ = Task.Run(Poll);
        _ = Task.Run(PollMeters);
    }

    // Meters: L/R peaks in dB by "input:NAME" (any case), read 25 times a second while a meter is
    // on show (WantMeters, called as they draw), not at all otherwise.
    public IReadOnlyDictionary<string, (double L, double R)> Meters { get; private set; } =
        new Dictionary<string, (double L, double R)>(StringComparer.OrdinalIgnoreCase);
    long metersWanted;

    public void WantMeters() => Interlocked.Exchange(ref metersWanted, Environment.TickCount64);

    public (double L, double R)? Meter(string kind, string name) => Meters.TryGetValue($"{kind}:{name}", out var m) ? m : null;

    async Task PollMeters()
    {
        while (!stop.IsCancellationRequested)
        {
            var wanted = State != null && Environment.TickCount64 - Interlocked.Read(ref metersWanted) < 3000;
            if (wanted)
            {
                try
                {
                    using var doc = JsonDocument.Parse(await http.GetStringAsync($"{Api}/meters", stop.Token));
                    var next = new Dictionary<string, (double L, double R)>(StringComparer.OrdinalIgnoreCase);
                    foreach (var p in doc.RootElement.EnumerateObject())
                        next[p.Name] = (p.Value[0].GetDouble(), p.Value[1].GetDouble());
                    Meters = next;
                }
                catch (Exception) when (!stop.IsCancellationRequested) { }
            }
            try { await Task.Delay(wanted ? 40 : 200, stop.Token); } catch (TaskCanceledException) { }
        }
    }

    public void Dispose()
    {
        stop.Cancel();
        http.Dispose();
    }

    public async Task<Desk?> Fetch()
    {
        try
        {
            using var doc = JsonDocument.Parse(await http.GetStringAsync($"{Api}/state", stop.Token));
            var root = doc.RootElement;
            IReadOnlyList<Strip> Strips(string key) => root.GetProperty(key).EnumerateArray().Select(s => new Strip(
                s.GetProperty("name").GetString() ?? "", s.GetProperty("fader").GetDouble(), s.GetProperty("mute").GetBoolean(),
                s.TryGetProperty("solo", out var solo) && solo.GetBoolean(), s.GetProperty("level").GetDouble(),
                s.GetProperty("on").GetBoolean())).ToList();
            IReadOnlyList<string> Names(string key) => root.GetProperty(key).EnumerateArray().Select(n => n.GetString() ?? "").ToList();
            return new Desk(Strips("inputs"), Strips("buses"), Strips("outputs"), Names("scenes"),
                            root.GetProperty("activeScene").GetString() ?? "", Names("macros"));
        }
        catch (Exception) when (!stop.IsCancellationRequested)
        {
            return null;
        }
    }

    async Task Poll()
    {
        while (!stop.IsCancellationRequested)
        {
            var was = State;
            var now = await Fetch();
            State = now;
            if ((was == null) != (now == null))
                Notify("all");
            else if (was != null && now != null)
            {
                var before = was.Inputs.Concat(was.Buses).Concat(was.Outputs).ToList();
                var after = now.Inputs.Concat(now.Buses).Concat(now.Outputs).ToList();
                if (before.Count != after.Count || !before.Select(s => s.Name).SequenceEqual(after.Select(s => s.Name)))
                    Notify("all");
                else
                {
                    if (!before.Select(s => s.Mute).SequenceEqual(after.Select(s => s.Mute))) Notify("mute");
                    if (!before.Select(s => s.Solo).SequenceEqual(after.Select(s => s.Solo))) Notify("solo");
                    if (!before.Select(s => Math.Round(s.Fader, 1)).SequenceEqual(after.Select(s => Math.Round(s.Fader, 1)))) Notify("fader");
                    if (!before.Select(s => Math.Round(s.Level / 3)).SequenceEqual(after.Select(s => Math.Round(s.Level / 3)))) Notify("level");
                }
                if (was.ActiveScene != now.ActiveScene) Notify("scene");
            }
            try { await Task.Delay(250, stop.Token); } catch (TaskCanceledException) { }
        }
    }

    // A handler that throws mustn't end the polling loop (it runs as a fire-and-forget task, so
    // nothing would restart it): its redraw is skipped and the next change tries again.
    void Notify(string what)
    {
        try { Changed?.Invoke(what); }
        catch (Exception) { }
    }

    // kind "input", "bus" or "output".
    public Strip? Find(string kind, string name)
    {
        var list = kind switch { "bus" => State?.Buses, "output" => State?.Outputs, _ => State?.Inputs };
        return list?.FirstOrDefault(s => string.Equals(s.Name, name, StringComparison.OrdinalIgnoreCase));
    }

    // A command: route as the API takes it ("input/MIC FOCUS/mute", "scene/GAMING"), value optional.
    // A route from its parts, each escaped on its own (a name may hold '/').
    public static string Path(params string[] parts) => string.Join('/', parts.Select(Uri.EscapeDataString));

    public async Task Send(string route, double? value = null)
    {
        var path = route;   // already escaped part by part (Path)
        var query = value is double v ? "?value=" + v.ToString(System.Globalization.CultureInfo.InvariantCulture) : "";
        try
        {
            using var content = new ByteArrayContent([]);
            content.Headers.ContentType = new MediaTypeHeaderValue("text/plain");
            await http.PostAsync($"{Api}/{path}{query}", content, stop.Token);
        }
        catch (Exception) when (!stop.IsCancellationRequested) { /* Faider isn't running: the buttons say so */ }
    }
}
