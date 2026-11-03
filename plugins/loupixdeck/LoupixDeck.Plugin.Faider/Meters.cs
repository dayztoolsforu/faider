using LoupixDeck.PluginSdk;

namespace LoupixDeck.Plugin.Faider;

// A meter's movement: rises at once, falls 24 dB a second; the peak holds for 1.5 s.
internal sealed class Ballistics
{
    public double L = -100, R = -100, HoldL = -100, HoldR = -100;
    long last = Environment.TickCount64, holdAtL, holdAtR;

    public void Update(double inL, double inR)
    {
        var now = Environment.TickCount64;
        var fall = 24.0 * Math.Min(0.5, (now - last) / 1000.0);
        last = now;
        L = Math.Max(inL, L - fall);
        R = Math.Max(inR, R - fall);
        if (inL >= HoldL || now - holdAtL > 1500) { HoldL = inL; holdAtL = now; }
        if (inR >= HoldR || now - holdAtR > 1500) { HoldR = inR; holdAtR = now; }
    }
}

// Draws a channel's meter on a 90x90 key: its name, two segmented bars (L, R; -60 to 0 dB,
// green, amber from -18, red from -6) with their peak hold, and the held peak in dB. Muted: the
// bars grey and MUTED in red.
internal static class MeterKey
{
    public const int Segments = 15;
    const double Floor = -60;

    static readonly PluginColor Back = PluginColor.FromRgb(18, 19, 22), Off = PluginColor.FromRgb(40, 42, 46),
        Green = PluginColor.FromRgb(60, 200, 90), Amber = PluginColor.FromRgb(235, 175, 40), Red = PluginColor.FromRgb(230, 55, 45),
        Grey = PluginColor.FromRgb(110, 112, 118), MutedRed = PluginColor.FromRgb(220, 60, 50);

    public static int Lit(double db) => (int) Math.Clamp(Math.Ceiling((db - Floor) / -Floor * Segments), 0, Segments);

    // What the key shows, as a number: the same number, the same picture (so an unchanged frame isn't sent).
    public static long Signature(Ballistics b, bool muted) =>
        (long) Lit(b.L) | (long) Lit(b.R) << 5 | (long) Lit(b.HoldL) << 10 | (long) Lit(b.HoldR) << 15 | (muted ? 1L : 0) << 20
        | (long) Math.Clamp(Math.Round(Math.Max(b.HoldL, b.HoldR)) + 128, 0, 255) << 21;

    public static void Draw(IRenderCanvas canvas, string name, Ballistics? b, bool muted)
    {
        var w = canvas.Width;
        var h = canvas.Height;
        canvas.Clear(Back);
        canvas.DrawText(name.ToUpperInvariant(), 2, 2, w - 4, 16, PluginColor.White, 11, TextHAlign.Center, TextVAlign.Middle, true, false, false, PluginColor.Black);

        const int barW = 20, gap = 8;
        int top = 21, bottom = h - 17, x0 = (w - 2 * barW - gap) / 2;
        var segH = (double) (bottom - top) / Segments;
        for (var side = 0; side < 2; side++)
        {
            var x = x0 + side * (barW + gap);
            var lit = b == null ? 0 : Lit(side == 0 ? b.L : b.R);
            var hold = b == null ? 0 : Lit(side == 0 ? b.HoldL : b.HoldR);
            for (var i = 0; i < Segments; i++)
            {
                var db = Floor + (i + 1) * -Floor / Segments;
                var colour = muted ? Grey : db > -6 ? Red : db > -18 ? Amber : Green;
                var on = i < lit || i == hold - 1;
                var y = (int) Math.Round(bottom - (i + 1) * segH);
                canvas.FillRectangle(x, y + 1, barW, Math.Max(1, (int) segH - 1), on ? colour : Off);
            }
        }

        var peak = b == null ? -100 : Math.Max(b.HoldL, b.HoldR);
        var text = muted ? "MUTED" : peak <= -99 ? "-INF" : $"{peak:0.0}";
        canvas.DrawText(text, 2, h - 16, w - 4, 15, muted ? MutedRed : peak > -6 ? Red : Grey, 11, TextHAlign.Center, TextVAlign.Middle, true, false, false, PluginColor.Black);
    }
}

// A meter on a key, live (20 frames a second, only sent when it changes). Press: mute.
internal sealed class MeterCommand(FaiderClient client) : IPluginCommand, IAnimatedDisplayCommand
{
    public const string Name = "Faider.Meter";
    public CommandDescriptor Descriptor { get; } = new()
    {
        CommandName = Name, DisplayName = "Meter", Group = "Faider", HiddenFromMenu = true,
        Description = "A channel's live meter (L/R with peak hold). Press: mute.",
        ParameterTemplate = "({Kind},{Name})",
        Parameters = [new CommandParameter("Kind", typeof(string)) { DefaultValue = "input" }, new CommandParameter("Name", typeof(string))],
    };
    public ButtonTargets SupportedTargets => ButtonTargets.TouchButton;
    public int TargetFps => 20;

    readonly Dictionary<string, (Ballistics B, long Signature, long Frame)> keys = [];

    public Task Execute(CommandContext ctx) => client.Send(FaiderClient.Path(Param.Kind(ctx), Param.At(ctx, 1), "mute"));

    public AnimationFrameInfo RenderAnimatedFrame(CommandContext ctx, IRenderCanvas canvas, AnimationFrameContext frame)
    {
        client.WantMeters();
        var kind = Param.Kind(ctx);
        var name = Param.At(ctx, 1);
        var key = ctx.ButtonKey ?? $"{kind}:{name}";
        lock (keys)
        {
            if (!keys.TryGetValue(key, out var k))
                k = (new Ballistics(), -1, 0);
            if (client.State == null)
            {
                if (k.Signature == -2)
                    return AnimationFrameInfo.Skip();
                Key.Draw(canvas, client, name, "", false, PluginColor.Black);
                keys[key] = (k.B, -2, k.Frame + 1);
                return AnimationFrameInfo.Frame(k.Frame + 1);
            }
            var m = client.Meter(kind, name);
            k.B.Update(m?.L ?? -100, m?.R ?? -100);
            var muted = client.Find(kind, name)?.Mute == true;
            var signature = MeterKey.Signature(k.B, muted);
            if (signature == k.Signature)
            {
                keys[key] = k;
                return AnimationFrameInfo.Skip();
            }
            MeterKey.Draw(canvas, name, k.B, muted);
            keys[key] = (k.B, signature, k.Frame + 1);
            return AnimationFrameInfo.Frame(k.Frame + 1);
        }
    }
}

// The meter bridge: a page of live meters, every channel on the desk (inputs, buses, outputs, as
// many as there are keys), opened from a key. Press a meter: mute that channel.
internal sealed class MeterBridge(FaiderClient client, IPluginHost host) : FolderProviderBase
{
    readonly Dictionary<string, Ballistics> movement = new(StringComparer.OrdinalIgnoreCase);
    Timer? timer;

    public override string Title => "METERS";

    public override void OnEnter() => timer = new Timer(_ => RaiseEntriesChanged(), null, 0, 66);   // about 15 frames a second

    public override void OnExit()
    {
        timer?.Dispose();
        timer = null;
    }

    public override IReadOnlyList<FolderEntry> BuildEntries()
    {
        client.WantMeters();
        var desk = client.State;
        if (desk == null)
            return [new FolderEntry { SlotIndex = host.FolderGrid.SlotForIndex(0), Text = "FAIDER\nOFF", TextSize = 13, Bold = true }];

        var channels = desk.Inputs.Select(s => ("input", s)).Concat(desk.Buses.Select(s => ("bus", s))).Concat(desk.Outputs.Select(s => ("output", s)));
        var entries = new List<FolderEntry>();
        var i = 0;
        foreach (var (kind, strip) in channels)
        {
            var slot = host.FolderGrid.SlotForIndex(i++);
            if (slot < 0)
                break;
            var key = $"{kind}:{strip.Name}";
            Ballistics b;
            lock (movement)
            {
                if (!movement.TryGetValue(key, out b!))
                    movement[key] = b = new Ballistics();
                var m = client.Meter(kind, strip.Name);
                b.Update(m?.L ?? -100, m?.R ?? -100);
            }
            var muted = strip.Mute;
            entries.Add(new FolderEntry
            {
                SlotIndex = slot,
                Render = canvas => MeterKey.Draw(canvas, strip.Name, b, muted),
                OnPress = () => client.Send(FaiderClient.Path(kind, strip.Name, "mute")),
            });
        }
        return entries;
    }
}

internal sealed class MeterBridgeCommand(FaiderClient client) : IPluginCommand, IDisplayImageCommand
{
    public const string Name = "Faider.MeterBridge";
    public CommandDescriptor Descriptor { get; } = new()
    {
        CommandName = Name, DisplayName = "Meter bridge", Group = "Faider",
        Description = "Opens a page of live meters for every channel on the Faider desk. Press a meter: mute.",
    };
    public ButtonTargets SupportedTargets => ButtonTargets.TouchButton | ButtonTargets.SimpleButton;
    public TimeSpan UpdateInterval => TimeSpan.FromSeconds(1);

    public Task Execute(CommandContext ctx)
    {
        ctx.Host.OpenFolder(new MeterBridge(client, ctx.Host));
        return Task.CompletedTask;
    }

    public bool RenderImage(CommandContext ctx, IRenderCanvas canvas) =>
        Key.Draw(canvas, client, "METERS", "BRIDGE", false, PluginColor.Black);
}
