using Microsoft.Win32;
using Rug.Constants;
using System.Security;

namespace Rug;

// Starting at sign-in through the per-user Run key, the entry Task Manager lists under Startup apps. Security software
// watches this key for new startup programs, so Rug writes it only when the user changes the setting, and a write that
// is already in place is skipped.
internal static class Autostart
{
    // Started as "dotnet Rug.dll" the process is the .NET host, which must not be what Windows starts at sign-in.
    public static bool Available =>
        Environment.ProcessPath is { } exe && !Path.GetFileName(exe).Equals("dotnet.exe", StringComparison.OrdinalIgnoreCase);

    public static bool IsOn()
    {
        if (!Available) return false;
        using RegistryKey? run = Registry.CurrentUser.OpenSubKey(StartupConstants.RunKey);
        if (run?.GetValue(StartupConstants.ValueName) as string != Command) return false;
        return !TurnedOffInTaskManager();
    }

    // Returns null on success, or why Windows refused the change. The call can wait while security software asks
    // the user about the change, so it runs off the UI thread.
    public static string? TrySet(bool on)
    {
        try
        {
            using RegistryKey run = Registry.CurrentUser.CreateSubKey(StartupConstants.RunKey);
            if (!on)
            {
                run.DeleteValue(StartupConstants.ValueName, throwOnMissingValue: false);
                return null;
            }
            if (run.GetValue(StartupConstants.ValueName) as string != Command)
                run.SetValue(StartupConstants.ValueName, Command);
            // Ticking the box is a newer choice than an earlier "Disable" in Task Manager, which Windows keeps apart
            // from the Run value; without the flag the entry counts as on.
            if (TurnedOffInTaskManager())
            {
                using RegistryKey? approved = Registry.CurrentUser.OpenSubKey(StartupConstants.ApprovedKey, writable: true);
                approved?.DeleteValue(StartupConstants.ValueName, throwOnMissingValue: false);
            }
            return null;
        }
        catch (Exception e) when (e is UnauthorizedAccessException or SecurityException or IOException)
        {
            return e.Message;
        }
    }

    private static string Command => $"\"{Environment.ProcessPath}\" {StartupConstants.BackgroundArgument}";

    // The StartupApproved format is not documented. Measured 2026-10-14 on Windows 11 26200: entries Task Manager
    // turned off start with 03 and do not run at sign-in, entries it left on start with 02 and run.
    private static bool TurnedOffInTaskManager()
    {
        using RegistryKey? approved = Registry.CurrentUser.OpenSubKey(StartupConstants.ApprovedKey);
        return approved?.GetValue(StartupConstants.ValueName) is byte[] { Length: > 0 } flags && (flags[0] & 1) != 0;
    }
}
