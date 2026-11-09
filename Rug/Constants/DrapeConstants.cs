namespace Rug.Constants;

/// <summary>How the rug's saved draped shape is stored on disk.</summary>
internal static class DrapeConstants
{
    /// <summary>First four bytes of the drape file, so an unrelated file is never read as a drape.</summary>
    public const int Magic = 0x44525547; // "GURD"

    /// <summary>Layout version of the drape file; a mismatch makes the reader fall back to a fresh drop.</summary>
    public const int Version = 2;

    /// <summary>The drape file's name inside the Rug data folder.</summary>
    public const string FileName = "drape.bin";
}
