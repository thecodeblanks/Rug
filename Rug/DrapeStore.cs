using Rug.Constants;
using System;
using System.IO;

namespace Rug;

/// <summary>
/// Saves and restores the rug's exact draped shape: the screen size and scale it was built at, and every cloth
/// vertex position. A restart can then put the rug back with its folds intact instead of dropping a fresh one.
/// </summary>
internal static class DrapeStore
{
    private static string FilePath => Path.Combine(AppSettings.DataDir, DrapeConstants.FileName);

    /// <summary>Writes the drape: the screen size and scale it was built at, then the first <paramref name="count"/> vertices.</summary>
    /// <param name="scale">The rug scale the drape was built at, so a restart rebuilds the cloth to the same size.</param>
    /// <param name="width">Screen width the vertex positions are in; a restart on another size drops fresh instead.</param>
    /// <param name="height">Screen height the vertex positions are in.</param>
    /// <param name="points">Vertex positions, three floats (x, y, z) per vertex; may be longer than needed.</param>
    /// <param name="count">The vertex count to write.</param>
    public static void Save(float scale, int width, int height, float[] points, int count)
    {
        if (count <= 0 || points.Length < count * 3) return;
        // Never persist a blown-up cloth: SameBits treats NaN as unchanged, so a diverged cloth can "settle",
        // and a NaN restored later spreads through the cloth and never clears.
        if (!float.IsFinite(scale)) return;
        for (int i = 0; i < count * 3; i++)
            if (!float.IsFinite(points[i])) return;
        try
        {
            Directory.CreateDirectory(AppSettings.DataDir);
            using var w = new BinaryWriter(File.Create(FilePath));
            w.Write(DrapeConstants.Magic);
            w.Write(DrapeConstants.Version);
            w.Write(count);
            w.Write(scale);
            w.Write(width);
            w.Write(height);
            for (int i = 0; i < count * 3; i++) w.Write(points[i]);
        }
        catch (Exception)
        {
        }
    }

    /// <summary>Reads a saved drape. Returns false, with empty output, when there is none or it is unreadable.</summary>
    /// <param name="scale">The scale the drape was saved at.</param>
    /// <param name="width">Screen width the saved positions are in.</param>
    /// <param name="height">Screen height the saved positions are in.</param>
    /// <param name="points">Vertex positions, three floats per vertex.</param>
    /// <returns>True when a valid, finite drape was read.</returns>
    public static bool TryLoad(out float scale, out int width, out int height, out float[] points)
    {
        scale = 0;
        width = 0;
        height = 0;
        points = [];
        try
        {
            if (!File.Exists(FilePath)) return false;
            using var r = new BinaryReader(File.OpenRead(FilePath));
            if (r.ReadInt32() != DrapeConstants.Magic || r.ReadInt32() != DrapeConstants.Version) return false;
            int count = r.ReadInt32();
            scale = r.ReadSingle();
            width = r.ReadInt32();
            height = r.ReadInt32();
            if (count <= 0 || count > 1_000_000) return false;
            if (!float.IsFinite(scale)) return false;
            var p = new float[count * 3];
            for (int i = 0; i < count * 3; i++)
            {
                float v = r.ReadSingle();
                // Reject a corrupt value here: the native step guard reads speed through std::max, which drops a
                // NaN, so a non-finite vertex would restore and never be caught.
                if (!float.IsFinite(v)) return false;
                p[i] = v;
            }
            points = p;
            return true;
        }
        catch (Exception)
        {
            scale = 0;
            width = 0;
            height = 0;
            points = [];
            return false;
        }
    }
}
