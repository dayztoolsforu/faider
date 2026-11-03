using LoupixDeck.PluginSdk;

namespace LoupixDeck.Plugin.Faider;

// Faider for LoupixDeck: the desk's channels, scenes and macros on keys and dials, through
// Faider's local control API. Commands are picked from the menu (FAIDER > INPUTS > MIC FOCUS >
// MUTE...), which lists what's on the desk right now, so they bind to the right channel.
public sealed class FaiderPlugin : LoupixPlugin, IMenuContributor
{
    FaiderClient? client;
    IPluginHost? host;
    readonly List<IPluginCommand> commands = [];

    public override PluginMetadata Metadata { get; } = new()
    {
        Id = "faider",
        Name = "Faider",
        Version = new Version(1, 1, 0),   // 1.1: meters (METER, METER BRIDGE)
        SdkVersion = SdkInfo.Version,
        Author = "Faider",
        Description = "Mute, solo, faders, scenes and macros on the Faider desk (Faider running, CONTROL > API on).",
        Icon = ReadIcon(),
    };

    static byte[]? ReadIcon()
    {
        using var stream = typeof(FaiderPlugin).Assembly.GetManifestResourceStream("LoupixDeck.Plugin.Faider.icon.png");
        if (stream == null)
            return null;
        using var copy = new MemoryStream();
        stream.CopyTo(copy);
        return copy.ToArray();
    }

    public override void Initialize(IPluginHost pluginHost)
    {
        host = pluginHost;
        client = new FaiderClient();
        var fader = new FaderCommand(client);
        commands.AddRange([new MuteCommand(client), new SoloCommand(client), fader,
                           new FaderStepCommand(client, fader, true), new FaderStepCommand(client, fader, false),
                           new SceneCommand(client), new MacroCommand(client),
                           new MeterCommand(client), new MeterBridgeCommand(client)]);

        // Redraw what changed.
        client.Changed += what =>
        {
            string[] names = what switch
            {
                "mute" => [MuteCommand.Name],
                "solo" => [SoloCommand.Name],
                "fader" => [FaderCommand.Name],
                "level" => [MuteCommand.Name, SoloCommand.Name, FaderCommand.Name],
                "scene" => [SceneCommand.Name],
                _ => [MuteCommand.Name, SoloCommand.Name, FaderCommand.Name, SceneCommand.Name, MacroCommand.Name],
            };
            foreach (var n in names)
                host.RequestButtonRefresh(n);
        };
        host.Logger.Info("Faider: watching " + FaiderClient.Api);
    }

    public override void Shutdown()
    {
        client?.Dispose();
        client = null;
    }

    public override IEnumerable<IPluginCommand> GetCommands() => commands;

    public override IReadOnlyList<CommandGroupDescriptor> GetCommandGroups() =>
        [new CommandGroupDescriptor { Group = "Faider", Description = "The Faider desk: channels, scenes, macros", Section = CommandGroupSection.Plugins }];

    // The menu: what's on the desk now. Keys get METER / MUTE / SOLO / fader keys per channel and a
    // METER BRIDGE (a page of every channel's meter), dials
    // get the fader (turn) with mute (press) in one go, or the fader alone.
    public async Task<IReadOnlyList<MenuNode>> GetMenuNodes(ButtonTargets target)
    {
        try
        {
            var desk = client == null ? null : await client.Fetch();
            if (desk == null)
                return [new MenuNode { Name = "FAIDER (NOT RUNNING)", Children = [new MenuNode { Name = "START FAIDER, CONTROL > API ON" }] }];

            var dial = target.HasFlag(ButtonTargets.RotaryEncoder);
            List<MenuNode> top =
            [
                Channels("INPUTS", "input", desk.Inputs, dial, solo: true),
                Channels("BUSES", "bus", desk.Buses, dial, solo: false),
                Channels("OUTPUTS", "output", desk.Outputs, dial, solo: false),
            ];
            if (!dial)
            {
                top.Add(Named("SCENES", SceneCommand.Name, "Scene", desk.Scenes));
                top.Add(Named("MACROS", MacroCommand.Name, "Macro", desk.Macros));
                top.Insert(0, new MenuNode { Name = "METER BRIDGE (ALL CHANNELS)", CommandName = MeterBridgeCommand.Name });
            }
            return [new MenuNode { Name = "FAIDER", Children = top }];
        }
        catch (Exception e)
        {
            host?.Logger.Error("Faider: menu", e);
            return [];
        }
    }

    static MenuNode Channels(string title, string kind, IReadOnlyList<FaiderClient.Strip> strips, bool dial, bool solo)
    {
        return new MenuNode
        {
            Name = title,
            Children = strips.Select(s =>
            {
                var p = new Dictionary<string, string> { ["Kind"] = kind, ["Name"] = Param.Encode(s.Name) };
                List<MenuNode> items = dial
                    ? [
                        new MenuNode
                        {
                            Name = "FADER + MUTE (TURN, PRESS)",
                            RotaryGroup = new Dictionary<RotaryAction, MenuCommandRef>
                            {
                                [RotaryAction.CounterClockwise] = new() { CommandName = FaderStepCommand.DownName, Parameters = p },
                                [RotaryAction.Clockwise] = new() { CommandName = FaderStepCommand.UpName, Parameters = p },
                                [RotaryAction.Press] = new() { CommandName = MuteCommand.Name, Parameters = p },
                            },
                        },
                        new MenuNode { Name = "FADER", CommandName = FaderCommand.Name, Parameters = p },
                      ]
                    : [
                        new MenuNode { Name = "METER", CommandName = MeterCommand.Name, Parameters = p },
                        new MenuNode { Name = "MUTE", CommandName = MuteCommand.Name, Parameters = p },
                        .. solo ? new[] { new MenuNode { Name = "SOLO", CommandName = SoloCommand.Name, Parameters = p } } : [],
                        new MenuNode { Name = "FADER TO 0 DB", CommandName = FaderCommand.Name, Parameters = p },
                        new MenuNode { Name = "FADER UP 1 DB", CommandName = FaderStepCommand.UpName, Parameters = p },
                        new MenuNode { Name = "FADER DOWN 1 DB", CommandName = FaderStepCommand.DownName, Parameters = p },
                      ];
                return new MenuNode { Name = s.Name.ToUpperInvariant(), Children = items };
            }).ToList(),
        };
    }

    static MenuNode Named(string title, string command, string parameter, IReadOnlyList<string> names) => new()
    {
        Name = title,
        Children = names.Select(n => new MenuNode
        {
            Name = n.ToUpperInvariant(), CommandName = command,
            Parameters = new Dictionary<string, string> { [parameter] = Param.Encode(n) },
        }).ToList(),
    };
}
