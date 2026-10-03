// PRISM ENGINE — minimal offline project manager/editor CLI starter.
// NOT a graphical scene editor. The Android runtime is C++20; this tool is a
// desktop .NET 8 prototype that invokes the one APK exporter (no other output).
using System.Diagnostics;
using System.Text.Json;

namespace PrismEditor;

public sealed record PrismProject(
    string Name,
    string PackageId,
    string EntryScene = "scenes/main.scene.json",
    string Target = "android-apk",
    int TargetFps = 60,
    string Quality = "auto");

internal static class Program
{
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true, PropertyNameCaseInsensitive = true };

    private static int Main(string[] args)
    {
        if (args.Length == 0) { Usage(); return 0; }
        try
        {
            return args[0] switch
            {
                "new" when args.Length >= 3 => Create(args[1], args[2]),
                "inspect" when args.Length >= 2 => Inspect(args[1]),
                "build-apk" when args.Length >= 2 => Build(args[1], args.Length > 2 && args[2] == "--release"),
                "help" => Help(),
                _ => Invalid()
            };
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"PRISM ERROR: {e.Message}");
            return 1;
        }
    }

    private static void Usage()
    {
        Console.WriteLine("PRISM ENGINE  •  Every Angle. Every World. One File.");
        Console.WriteLine("Commands: new <name> <directory> | inspect <directory> | build-apk <directory> [--release]");
        Console.WriteLine("No account. No phone-home. Android APK only. See docs/.");
    }
    private static int Help() { Usage(); return 0; }
    private static int Invalid() { Usage(); return 2; }

    private static int Create(string name, string directory)
    {
        var root = Path.GetFullPath(directory);
        if (Directory.Exists(root) && Directory.EnumerateFileSystemEntries(root).Any())
            throw new IOException($"Directory is not empty: {root}");
        Directory.CreateDirectory(Path.Combine(root, "scenes"));
        Directory.CreateDirectory(Path.Combine(root, "scripts"));
        Directory.CreateDirectory(Path.Combine(root, "assets"));
        var slug = new string(name.ToLowerInvariant().Where(c => char.IsAsciiLetterOrDigit(c)).ToArray());
        if (slug.Length == 0) throw new ArgumentException("Project name must contain ASCII letters/digits");
        var project = new PrismProject(name, "dev.prismengine." + slug);
        File.WriteAllText(Path.Combine(root, "project.prism.json"), JsonSerializer.Serialize(project, Json));
        File.WriteAllText(Path.Combine(root, "scenes", "main.scene.json"),
            "{\"version\":1,\"name\":\"Main\",\"entities\":[]}");
        File.WriteAllText(Path.Combine(root, "scripts", "Game.prism"),
            "class Game { func start() { print(\"Hello, Prism!\"); } }\nvar game = new Game();\ngame.start();\n");
        Console.WriteLine($"Created {name} at {root} — Android APK only.");
        return 0;
    }

    private static PrismProject Load(string root)
    {
        var path = Path.Combine(Path.GetFullPath(root), "project.prism.json");
        var project = JsonSerializer.Deserialize<PrismProject>(File.ReadAllText(path), Json)
            ?? throw new InvalidDataException("Invalid Prism project");
        if (project.Target != "android-apk")
            throw new InvalidDataException("Only android-apk is supported");
        if (project.TargetFps is < 15 or > 240)
            throw new InvalidDataException("TargetFps must be between 15 and 240");
        return project;
    }

    private static int Inspect(string root)
    {
        var p = Load(root);
        Console.WriteLine($"{p.Name}  [{p.PackageId}]  {p.Target}  {p.TargetFps} FPS  quality={p.Quality}");
        Console.WriteLine($"Entry scene: {p.EntryScene}");
        return 0;
    }

    private static int Build(string root, bool release)
    {
        var p = Load(root);
        var dir = Path.GetFullPath(root);
        var repo = FindRepo(AppContext.BaseDirectory)
            ?? throw new DirectoryNotFoundException("Cannot find PRISM engine repository (android/ missing)");
        var script = Path.Combine(dir, "scripts", "Game.prism");
        if (!File.Exists(script)) throw new FileNotFoundException("Missing entry script", script);
        // The current APK template is a fixed sample: project scene/assets are not yet
        // imported. Refuse to claim a custom project was built when it wasn't.
        throw new NotSupportedException(
            $"Custom project APK export is not implemented yet. Run tools/build-apk.sh " +
            $"{(release ? "release" : "debug")} to build the bundled demo APK instead. " +
            $"Project '{p.Name}' at {dir} was not exported.");
    }

    private static string? FindRepo(string start)
    {
        var d = new DirectoryInfo(start);
        while (d != null)
        {
            if (Directory.Exists(Path.Combine(d.FullName, "android"))) return d.FullName;
            d = d.Parent;
        }
        return null;
    }
}
