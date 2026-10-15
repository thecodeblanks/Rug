using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace Rug;

internal sealed class SettingsForm : Form
{
    private readonly TrayContext _tray;
    private readonly FlowLayoutPanel _root;
    private readonly AppSettings _settings;
    private readonly FlowLayoutPanel _rugTiles = new() { AutoSize = true, WrapContents = true, Width = 600 };
    private readonly FlowLayoutPanel _floorTiles = new() { AutoSize = true, WrapContents = true, Width = 600 };
    private readonly TrackBar _size = new() { Minimum = 25, Maximum = 75, TickFrequency = 5, Width = 400 };
    private readonly Button _restoreFloor = new() { Text = "Restore previous wallpaper", AutoSize = true };
    private readonly Button _putBack = new() { Text = "Put them back", AutoSize = true };
    private readonly CheckBox _startAtSignIn = new() { Text = "Start Rug when I sign in to Windows", AutoSize = true };
    private bool _startupChanging;

    private readonly Dictionary<string, Image> _thumbs = new();

    public SettingsForm(TrayContext tray, AppSettings settings)
    {
        _tray = tray;
        _settings = settings;

        Text = "Rug";
        try { Icon = Assets.AppIcon(0); }
        catch (Exception) {  }
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        AutoScaleMode = AutoScaleMode.Dpi;
        ShowInTaskbar = true;

        var root = _root = new FlowLayoutPanel
        {
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            Padding = new Padding(18),
            Width = 640,
        };

        root.Controls.Add(Note("Drag the rug by any edge. Scroll over it to turn it, Alt + scroll to resize. Right-click the tray icon for the quick menu. Closing this window keeps the rug on the desktop."));

        root.Controls.Add(Header("Rug"));
        root.Controls.Add(_rugTiles);
        root.Controls.Add(MakeButton("Your image…", (_, _) => { _tray.OwnImage(); Rebuild(); }));

        root.Controls.Add(Header("Size"));
        _size.MouseUp += (_, _) => _tray.ResizeTo(_size.Value / 100f);
        _size.KeyUp += (_, _) => _tray.ResizeTo(_size.Value / 100f);
        root.Controls.Add(_size);

        root.Controls.Add(Header("Desktop floor"));
        root.Controls.Add(_floorTiles);
        _restoreFloor.Click += (_, _) => { _tray.RestoreFloor(); Rebuild(); };
        root.Controls.Add(_restoreFloor);

        root.Controls.Add(Header("Icons"));
        var iconRow = new FlowLayoutPanel { AutoSize = true, WrapContents = false };
        var sweep = MakeButton("Sweep under the rug", (_, _) => { Overlay.PileIcons(); });
        _putBack.Click += (_, _) => { Overlay.RestoreIcons(); Rebuild(); };
        iconRow.Controls.Add(sweep);
        iconRow.Controls.Add(_putBack);
        iconRow.Controls.Add(MakeButton("Cover icons with the rug", (_, _) => Overlay.CoverIcons()));
        root.Controls.Add(iconRow);
        root.Controls.Add(Note("If the icons snap back after a sweep, turn off Auto arrange: right-click an empty spot on the desktop, View, and clear \"Auto arrange icons\"."));

        root.Controls.Add(Header("Startup"));
        // Click, not CheckedChanged: Rebuild sets Checked from the registry, and only the user's click may write it.
        _startAtSignIn.Click += async (_, _) => await ChangeStartup(_startAtSignIn.Checked);
        root.Controls.Add(_startAtSignIn);
        root.Controls.Add(Note("Rug then starts in the tray without opening this window. Task Manager lists it under Startup apps."));

        var actions = new FlowLayoutPanel { AutoSize = true, WrapContents = false };
        actions.Controls.Add(MakeButton("Drop again", (_, _) => Overlay.Redrop()));
        actions.Controls.Add(MakeButton("Center", (_, _) => Overlay.Center()));
        actions.Controls.Add(MakeButton("Quit Rug", (_, _) => _tray.Quit()));
        root.Controls.Add(actions);

        var scroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true };
        scroll.Controls.Add(root);
        Controls.Add(scroll);
        FitHeight();

        FormClosing += (_, e) =>
        {
            if (e.CloseReason == CloseReason.UserClosing)
            {
                e.Cancel = true;
                Hide();
            }
        };
        VisibleChanged += (_, _) => { if (Visible) Rebuild(); };
    }

    public void Rebuild()
    {
        DisposeTiles(_rugTiles);
        DisposeTiles(_floorTiles);

        List<string> rugs = Assets.AllRugs();
        List<string> floors = Assets.Floors();
        PrepareThumbnails(rugs.Concat(floors));

        string? selected = _settings.RugPath ?? rugs.FirstOrDefault();
        foreach (string path in rugs)
        {
            string p = path;
            _rugTiles.Controls.Add(Tile(p, Assets.DisplayName(path), p == selected, () => { _tray.PickRug(p); Rebuild(); }));
        }

        foreach (string path in floors)
        {
            string p = path;
            _floorTiles.Controls.Add(Tile(p, Assets.DisplayName(path), false, () => { _tray.PickFloor(p); Rebuild(); }));
        }
        if (_floorTiles.Controls.Count == 0)
            _floorTiles.Controls.Add(Note("No floor pictures found."));

        _size.Value = Math.Clamp((int)Math.Round(Overlay.CurrentScale * 100), _size.Minimum, _size.Maximum);
        _restoreFloor.Enabled = Wallpaper.CanRestore(_settings);
        _putBack.Enabled = IconPile.HasHome;
        if (!_startupChanging)
        {
            _startAtSignIn.Checked = Autostart.IsOn();
            _startAtSignIn.Enabled = Autostart.Available;
        }
        FitHeight();
    }

    // The box shows what the registry holds after the write, not what was asked, so a change that Windows or
    // security software refused is visible.
    private async Task ChangeStartup(bool on)
    {
        _startupChanging = true;
        _startAtSignIn.Enabled = false;
        string? error = await Task.Run(() => Autostart.TrySet(on));
        _startupChanging = false;
        if (IsDisposed) return;
        _startAtSignIn.Checked = Autostart.IsOn();
        _startAtSignIn.Enabled = true;
        if (error != null)
            _tray.Warn("The startup setting could not be changed: " + error);
        else if (_startAtSignIn.Checked != on)
            _tray.Warn("Windows did not keep the startup setting. Security software may have blocked the change.");
    }

    private void FitHeight()
    {
        int screen = Screen.PrimaryScreen?.WorkingArea.Height ?? 900;
        ClientSize = new Size(_root.PreferredSize.Width + 40, Math.Min(_root.PreferredSize.Height + 20, screen - 80));
    }

    private Button Tile(string imagePath, string name, bool selected, Action onClick)
    {
        var tile = new Button
        {
            Text = name,
            Image = Thumbnail(imagePath),
            ImageAlign = ContentAlignment.TopCenter,
            TextAlign = ContentAlignment.BottomCenter,
            TextImageRelation = TextImageRelation.ImageAboveText,
            FlatStyle = FlatStyle.Flat,
            Size = new Size(118, 100),
            Margin = new Padding(4),
        };
        tile.FlatAppearance.BorderSize = selected ? 3 : 1;
        tile.FlatAppearance.BorderColor = selected ? Color.FromArgb(0, 122, 255) : Color.Silver;
        tile.Click += (_, _) => onClick();
        return tile;
    }

    // Clear() only detaches the old tiles; each one keeps its window handle until it is disposed. A tile's own
    // click rebuilds the panel, so the disposal waits until that click has finished. Thumbnails stay in _thumbs,
    // since a Button does not dispose its Image.
    private void DisposeTiles(Control panel)
    {
        Control[] old = panel.Controls.Cast<Control>().ToArray();
        panel.Controls.Clear();
        if (old.Length == 0) return;
        Action dispose = () => { foreach (Control control in old) control.Dispose(); };
        if (IsHandleCreated) BeginInvoke(dispose);
        else dispose();
    }

    // Decode one at a time: Assets.Thumbnail uses System.Drawing (GDI+), which is not safe to call from several
    // threads at once and throws a sporadic "generic error" under concurrency. The cache files are written one at
    // a time too.
    private void PrepareThumbnails(IEnumerable<string> paths)
    {
        string[] missing = paths.Distinct().Where(p => !_thumbs.ContainsKey(p)).ToArray();
        var made = new Bitmap[missing.Length];
        var fresh = new bool[missing.Length];
        for (int i = 0; i < missing.Length; i++) made[i] = Assets.Thumbnail(missing[i], out fresh[i]);
        for (int i = 0; i < missing.Length; i++)
        {
            if (fresh[i]) Assets.StoreThumbnail(missing[i], made[i]);
            _thumbs[missing[i]] = made[i];
        }
    }

    private Image Thumbnail(string path)
    {
        if (!_thumbs.TryGetValue(path, out Image? image))
        {
            var bitmap = Assets.Thumbnail(path, out bool fresh);
            if (fresh) Assets.StoreThumbnail(path, bitmap);
            image = bitmap;
            _thumbs[path] = image;
        }
        return image;
    }

    private static Label Header(string text) => new()
    {
        Text = text,
        Font = new Font(SystemFonts.DefaultFont, FontStyle.Bold),
        AutoSize = true,
        Margin = new Padding(0, 14, 0, 4),
    };

    private static Label Note(string text) => new()
    {
        Text = text,
        ForeColor = SystemColors.GrayText,
        AutoSize = true,
        MaximumSize = new Size(600, 0),
        Margin = new Padding(0, 4, 0, 4),
    };

    private static Button MakeButton(string text, EventHandler onClick)
    {
        var button = new Button { Text = text, AutoSize = true, Margin = new Padding(0, 4, 8, 4) };
        button.Click += onClick;
        return button;
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            foreach (var image in _thumbs.Values) image.Dispose();
            _thumbs.Clear();
        }
        base.Dispose(disposing);
    }
}
