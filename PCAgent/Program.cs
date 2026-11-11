using System;
using System.Diagnostics;
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

        // This is a lightweight monitor that still needs to report in reliably when
        // the machine it's watching is the one under heavy load — exactly when a
        // user most wants to see CPU/GPU numbers. At default (Normal) priority it
        // competes evenly with whatever is pegging all cores at 100%, and under
        // real contention its own timer ticks, sensor reads and HTTP send can all
        // get delayed past the send timeout, surfacing as "WiFi timeout — device
        // unreachable" even though the device and network are fine. AboveNormal
        // gives the scheduler a reason to favor it without going so far as
        // High/Realtime, which could itself destabilize the host.
        try { Process.GetCurrentProcess().PriorityClass = ProcessPriorityClass.AboveNormal; }
        catch { /* never let a priority-class failure stop the app from starting */ }

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
