namespace Rug.Constants;

/// <summary>Where Windows keeps the per-user startup entry and how Rug recognises a start at sign-in.</summary>
internal static class StartupConstants
{
    /// <summary>The per-user Run key that Explorer starts at sign-in and Task Manager lists under Startup apps.</summary>
    public const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";

    /// <summary>Where Task Manager records that the user turned a Run entry off; the Run value itself stays.</summary>
    public const string ApprovedKey = @"Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run";

    /// <summary>The value name of Rug's entry under both keys.</summary>
    public const string ValueName = "Rug";

    /// <summary>The argument the startup entry passes, so Rug starts in the tray without opening its settings.</summary>
    public const string BackgroundArgument = "--background";
}
