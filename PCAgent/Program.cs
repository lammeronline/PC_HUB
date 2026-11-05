using System;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace PCHub;

static class Program
{
    private static readonly string LogPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PCHub", "crash.log");

    [STAThread]
    static void Main()
    {
        // Without these, an exception that escapes the UI thread (or a background
        // Task whose exception nobody awaits) kills the process with zero trace —
        // no dialog, no Windows Error Reporting entry, nothing. That's exactly what
        // was observed when undocking a laptop: the tray icon just vanished. These
        // handlers can't catch a genuine native/corrupted-state crash (e.g. inside
        // a GPU vendor driver call), but they do catch everything else and leave a
        // record instead of disappearing silently.
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
            LogCrash("AppDomain.UnhandledException", e.ExceptionObject as Exception);

        TaskScheduler.UnobservedTaskException += (_, e) =>
        {
            LogCrash("TaskScheduler.UnobservedTaskException", e.Exception);
            e.SetObserved();
        };

        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += (_, e) => LogCrash("Application.ThreadException", e.Exception);

        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm());
    }

    private static void LogCrash(string source, Exception? ex)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
            File.AppendAllText(LogPath,
                $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {source}: {ex}\n\n");
        }
        catch { /* best effort — never let logging itself crash the handler */ }
    }
}
