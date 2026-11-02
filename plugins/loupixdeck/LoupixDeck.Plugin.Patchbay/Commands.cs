using System.Text;
using LoupixDeck.PluginSdk;

namespace LoupixDeck.Plugin.Patchbay;

// A channel as a command's parameters: its kind ("input", "bus", "output") and name. LoupixDeck
// keeps them as Command(p1,p2) with no escaping, so a name with , ( ) or & in it is stored as
// "b64." and base64.
internal static class Param
{
    public static string Encode(string name) =>
        name.IndexOfAny([',', '(', ')', '&']) < 0 ? name : "b64." + Convert.ToBase64String(Encoding.UTF8.GetBytes(name));

    // A damaged "b64." value (hand-edited config, say) comes back as it is rather than throwing.
    public static string Decode(string value)
    {
        if (!value.StartsWith("b64.", StringComparison.Ordinal)) return value;
        var bytes = new byte[value.Length];
        return Convert.TryFromBase64String(value[4..], bytes, out var n) ? Encoding.UTF8.GetString(bytes, 0, n) : value;
    }

    public static string At(CommandContext ctx, int i) => i < ctx.Parameters.Length ? Decode(ctx.Parameters[i].Trim()) : "";

    public static string Kind(CommandContext ctx) => At(ctx, 0) is "bus" or "output" ? At(ctx, 0) : "input";
}

// How a key looks: dark, or lit in the command's colour; the name over what it does, and the
// channel's level as a bar along the bottom. Patchbay not answering: PATCHBAY OFF.
internal static class Key
{
    static readonly PluginColor Dark = PluginColor.FromRgb(30, 32, 36), Dim = PluginColor.FromRgb(120, 124, 130),
                                LevelColour = PluginColor.FromRgb(70, 200, 100);

    public static bool Draw(IRenderCanvas canvas, PatchbayClient client, string name, string what, bool lit, PluginColor colour,
                            PatchbayClient.Strip? strip = null)
    {
        var w = canvas.Width;
        var h = canvas.Height;
        if (client.State == null)
        {
            canvas.Clear(Dark);
            canvas.DrawText("PATCHBAY\nOFF", 0, 0, w, h, Dim, 13, TextHAlign.Center, TextVAlign.Middle, true, false, false, PluginColor.Black);
            return true;
        }
        canvas.Clear(lit ? colour : Dark);
        var ink = lit && colour.R + colour.G + colour.B > 520 ? PluginColor.FromRgb(20, 20, 20) : PluginColor.White;
        canvas.DrawText(name.ToUpperInvariant(), 4, 6, w - 8, h / 2, ink, 14, TextHAlign.Center, TextVAlign.Middle, true, false, false, PluginColor.Black);
        canvas.DrawText(what, 4, h / 2, w - 8, h / 2 - 14, lit ? ink : Dim, 12, TextHAlign.Center, TextVAlign.Middle, true, false, false, PluginColor.Black);
        if (strip != null)   // level: -60 dB to 0 dB across the bottom
        {
            var fill = (int) Math.Clamp((strip.Level + 60.0) / 60.0 * (w - 12), 0, w - 12);
            canvas.FillRectangle(6, h - 9, w - 12, 4, PluginColor.FromRgb(15, 15, 17));
            canvas.FillRectangle(6, h - 9, fill, 4, LevelColour);
        }
        return true;
    }

    public static string Db(double fader) => fader <= -60 ? "OFF" : $"{fader:0.0} dB";
}

internal abstract class ChannelCommand(PatchbayClient client) : IPluginCommand, IDisplayImageCommand
{
    protected readonly PatchbayClient Client = client;
    protected static readonly CommandParameter[] ChannelParameters =
        [new CommandParameter("Kind", typeof(string)) { DefaultValue = "input" }, new CommandParameter("Name", typeof(string))];

    public abstract CommandDescriptor Descriptor { get; }
    public virtual ButtonTargets SupportedTargets => ButtonTargets.TouchButton | ButtonTargets.SimpleButton;
    public abstract Task Execute(CommandContext ctx);
    public TimeSpan UpdateInterval => TimeSpan.FromMilliseconds(500);
    public abstract bool RenderImage(CommandContext ctx, IRenderCanvas canvas);

    protected string Route(CommandContext ctx, string what) => PatchbayClient.Path(Param.Kind(ctx), Param.At(ctx, 1), what);
    protected PatchbayClient.Strip? Strip(CommandContext ctx) => Client.Find(Param.Kind(ctx), Param.At(ctx, 1));
}

internal sealed class MuteCommand(PatchbayClient client) : ChannelCommand(client)
{
    public const string Name = "Patchbay.Mute";
    public override CommandDescriptor Descriptor { get; } = new()
    {
        CommandName = Name, DisplayName = "Mute", Group = "Patchbay", HiddenFromMenu = true,
        Description = "Mutes or unmutes a channel. Lit red while muted.",
        ParameterTemplate = "({Kind},{Name})", Parameters = ChannelParameters,
    };
    public override ButtonTargets SupportedTargets => ButtonTargets.All;
    public override Task Execute(CommandContext ctx) => Client.Send(Route(ctx, "mute"));
    public override bool RenderImage(CommandContext ctx, IRenderCanvas canvas)
    {
        var s = Strip(ctx);
        return Key.Draw(canvas, Client, Param.At(ctx, 1), "MUTE", s?.Mute == true, PluginColor.FromRgb(200, 52, 42), s);
    }
}

internal sealed class SoloCommand(PatchbayClient client) : ChannelCommand(client)
{
    public const string Name = "Patchbay.Solo";
    public override CommandDescriptor Descriptor { get; } = new()
    {
        CommandName = Name, DisplayName = "Solo", Group = "Patchbay", HiddenFromMenu = true,
        Description = "Solos an input. Lit yellow while soloed.",
        ParameterTemplate = "({Kind},{Name})", Parameters = ChannelParameters,
    };
    public override Task Execute(CommandContext ctx) => Client.Send(Route(ctx, "solo"));
    public override bool RenderImage(CommandContext ctx, IRenderCanvas canvas)
    {
        var s = Strip(ctx);
        return Key.Draw(canvas, Client, Param.At(ctx, 1), "SOLO", s?.Solo == true, PluginColor.FromRgb(230, 200, 50), s);
    }
}

// A fader on a dial: turning moves it a dB a click, the dial's press (or a key) puts it back to
// 0 dB. Clicks in quick succession build on the last value sent, not the last one read back.
internal sealed class FaderCommand(PatchbayClient client) : ChannelCommand(client), IAdjustmentCommand
{
    public const string Name = "Patchbay.Fader";
    public override CommandDescriptor Descriptor { get; } = new()
    {
        CommandName = Name, DisplayName = "Fader", Group = "Patchbay", HiddenFromMenu = true,
        Description = "Turn: moves a channel's fader a dB a click. Press: back to 0 dB.",
        ParameterTemplate = "({Kind},{Name})", Parameters = ChannelParameters,
    };
    public override ButtonTargets SupportedTargets => ButtonTargets.All;

    readonly Dictionary<string, (double Value, DateTime At)> sent = [];

    public Task Step(CommandContext ctx, int ticks)
    {
        var key = Route(ctx, "fader");
        var s = Strip(ctx);
        if (s == null)
            return Task.CompletedTask;
        lock (sent)
        {
            var from = sent.TryGetValue(key, out var last) && DateTime.UtcNow - last.At < TimeSpan.FromMilliseconds(700) ? last.Value : s.Fader;
            var to = Math.Clamp(Math.Max(from, -61) + ticks, -60, 12);
            sent[key] = (to, DateTime.UtcNow);
            return Client.Send(key, to);
        }
    }

    public Task ApplyAdjustment(CommandContext ctx, int ticks) => Step(ctx, ticks);
    public Task ApplyReset(CommandContext ctx) => Client.Send(Route(ctx, "fader"), 0);
    public override Task Execute(CommandContext ctx) => ApplyReset(ctx);

    public AdjustmentValue? GetValue(CommandContext ctx)
    {
        var s = Strip(ctx);
        return s == null ? null : new AdjustmentValue(Math.Clamp((s.Fader + 60) / 72, 0, 1), Key.Db(s.Fader)) { Detail = Param.At(ctx, 1).ToUpperInvariant() };
    }

    public override bool RenderImage(CommandContext ctx, IRenderCanvas canvas)
    {
        var s = Strip(ctx);
        return Key.Draw(canvas, Client, Param.At(ctx, 1), s == null ? "--" : Key.Db(s.Fader), false, PluginColor.Black, s);
    }
}

// One click of a fader, for a dial's turn left / right set up from the menu as a group.
internal sealed class FaderStepCommand(PatchbayClient client, FaderCommand fader, bool up) : IPluginCommand
{
    public const string UpName = "Patchbay.FaderUp", DownName = "Patchbay.FaderDown";
    public CommandDescriptor Descriptor { get; } = new()
    {
        CommandName = up ? UpName : DownName, DisplayName = up ? "Fader up" : "Fader down", Group = "Patchbay", HiddenFromMenu = true,
        Description = up ? "Raises a channel's fader 1 dB." : "Lowers a channel's fader 1 dB.",
        ParameterTemplate = "({Kind},{Name})",
        Parameters = [new CommandParameter("Kind", typeof(string)) { DefaultValue = "input" }, new CommandParameter("Name", typeof(string))],
    };
    public ButtonTargets SupportedTargets => ButtonTargets.All;
    public Task Execute(CommandContext ctx) => client.State == null ? Task.CompletedTask : fader.Step(ctx, up ? 1 : -1);
}

internal sealed class SceneCommand(PatchbayClient client) : IPluginCommand, IDisplayImageCommand
{
    public const string Name = "Patchbay.Scene";
    public CommandDescriptor Descriptor { get; } = new()
    {
        CommandName = Name, DisplayName = "Scene", Group = "Patchbay", HiddenFromMenu = true,
        Description = "Recalls a Patchbay scene. Lit green while it's the active scene.",
        ParameterTemplate = "({Scene})", Parameters = [new CommandParameter("Scene", typeof(string))],
    };
    public ButtonTargets SupportedTargets => ButtonTargets.TouchButton | ButtonTargets.SimpleButton;
    public TimeSpan UpdateInterval => TimeSpan.FromMilliseconds(500);
    public Task Execute(CommandContext ctx) => client.Send(PatchbayClient.Path("scene", Param.At(ctx, 0)));
    public bool RenderImage(CommandContext ctx, IRenderCanvas canvas) =>
        Key.Draw(canvas, client, Param.At(ctx, 0), "SCENE",
                 string.Equals(client.State?.ActiveScene, Param.At(ctx, 0), StringComparison.OrdinalIgnoreCase), PluginColor.FromRgb(60, 190, 90));
}

internal sealed class MacroCommand(PatchbayClient client) : IPluginCommand, IDisplayImageCommand
{
    public const string Name = "Patchbay.Macro";
    public CommandDescriptor Descriptor { get; } = new()
    {
        CommandName = Name, DisplayName = "Macro", Group = "Patchbay", HiddenFromMenu = true,
        Description = "Runs a Patchbay macro (pressed and released).",
        ParameterTemplate = "({Macro})", Parameters = [new CommandParameter("Macro", typeof(string))],
    };
    public ButtonTargets SupportedTargets => ButtonTargets.TouchButton | ButtonTargets.SimpleButton;
    public TimeSpan UpdateInterval => TimeSpan.FromSeconds(1);
    public Task Execute(CommandContext ctx) => client.Send(PatchbayClient.Path("macro", Param.At(ctx, 0)));
    public bool RenderImage(CommandContext ctx, IRenderCanvas canvas) =>
        Key.Draw(canvas, client, Param.At(ctx, 0), "MACRO", false, PluginColor.Black);
}
