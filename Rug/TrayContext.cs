using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace Rug;

internal sealed class TrayContext : ApplicationContext
{
    private readonly NotifyIcon _tray;
    private readonly AppSettings _settings;
    private readonly Control _host = new();
    private SettingsForm? _form;

    public TrayContext(AppSettings settings)
    {
        _settings = settings;
        _ = _host.Handle;
        _tray = new NotifyIcon
        {
            Icon = LoadAppIcon(),
            Text = "Rug",
            Visible = true,
            ContextMenuStrip = new ContextMenuStrip(),
        };
        _tray.ContextMenuStrip.Opening += (_, _) => BuildMenu();
        _tray.DoubleClick += (_, _) => ShowSettings();

        Task.Run(() =>
        {
            string? error = Overlay.Start(settings);
            if (error != null)
                _host.BeginInvoke(new Action(() => MessageBox.Show(error, "Rug", MessageBoxButtons.OK, MessageBoxIcon.Error)));
        });
        Overlay.Notifier = message => _host.BeginInvoke(new Action(() => _tray.ShowBalloonTip(4000, "Rug", message, ToolTipIcon.Warning)));
        BuildMenu();
    }

    public void ShowSettingsFromAnyThread() => _host.BeginInvoke(new Action(ShowSettings));

    public void ShowSettings()
    {
        _form ??= new SettingsForm(this, _settings);
        if (!_form.Visible) _form.Show();
        _form.WindowState = FormWindowState.Normal;
        _form.Activate();
    }

    public void PickRug(string path)
    {
        _settings.RugPath = path;
        _settings.Save();
        Overlay.SetRug(path);
    }

    public void ResizeTo(float scale)
    {
        _settings.Scale = scale;
        _settings.Save();
        Overlay.SetScale(scale);
    }

    public void PickFloor(string floorPath)
    {
        try { Wallpaper.Apply(_settings, floorPath); }
        catch (Exception e) { _tray.ShowBalloonTip(4000, "Rug", "The floor could not be set: " + e.Message, ToolTipIcon.Warning); }
    }

    public void RestoreFloor() => Wallpaper.Restore(_settings);

    public void Warn(string message) => _tray.ShowBalloonTip(4000, "Rug", message, ToolTipIcon.Warning);

    public void OwnImage()
    {
        using var dialog = new OpenFileDialog
        {
            Title = "Your rug picture",
            Filter = "Pictures|*.jpg;*.jpeg;*.png;*.bmp",
        };
        if (dialog.ShowDialog() != DialogResult.OK) return;
        Directory.CreateDirectory(Assets.UserRugDir);
        string copy = Path.Combine(Assets.UserRugDir, Path.GetFileName(dialog.FileName));
        try
        {
            File.Copy(dialog.FileName, copy, true);
        }
        catch (Exception e)
        {
            _tray.ShowBalloonTip(4000, "Rug", "The picture could not be copied: " + e.Message, ToolTipIcon.Warning);
            return;
        }
        PickRug(copy);
        _form?.Rebuild();
    }

    public void Quit()
    {
        Overlay.Stop();
        _settings.Scale = Overlay.CurrentScale;
        _settings.Save();
        _tray.Visible = false;
        _tray.Dispose();
        _form?.Close();
        ExitThread();
    }

    private void BuildMenu()
    {
        var menu = _tray.ContextMenuStrip!;
        menu.Items.Clear();

        Add(menu, "Settings…", (_, _) => ShowSettings());
        menu.Items.Add(new ToolStripSeparator());
        Add(menu, "Sweep icons under the rug", (_, _) => Overlay.PileIcons());
        if (IconPile.HasHome) Add(menu, "Put icons back", (_, _) => Overlay.RestoreIcons());
        Add(menu, "Cover icons with the rug", (_, _) => Overlay.CoverIcons());
        Add(menu, "Drop again", (_, _) => Overlay.Redrop());
        Add(menu, "Center", (_, _) => Overlay.Center());

        var size = new ToolStripMenuItem("Size");
        float current = Overlay.CurrentScale;
        foreach (var (label, value) in new[] { ("Small", 0.32f), ("Medium", 0.46f), ("Large", 0.62f) })
        {
            float v = value;
            var item = new ToolStripMenuItem(label) { Checked = MathF.Abs(current - v) < 0.01f };
            item.Click += (_, _) => ResizeTo(v);
            size.DropDownItems.Add(item);
        }
        menu.Items.Add(size);

        var rugs = new ToolStripMenuItem("Rug");
        string? selected = CurrentRugPath();
        foreach (string path in Assets.AllRugs())
        {
            string p = path;
            var item = new ToolStripMenuItem(Assets.DisplayName(path)) { Checked = p == selected };
            item.Click += (_, _) => PickRug(p);
            rugs.DropDownItems.Add(item);
        }
        rugs.DropDownItems.Add(new ToolStripSeparator());
        Add(rugs, "Rug from your image…", (_, _) => OwnImage());
        menu.Items.Add(rugs);

        var floors = new ToolStripMenuItem("Desktop floor");
        foreach (string path in Assets.Floors())
        {
            string p = path;
            var item = new ToolStripMenuItem(Assets.DisplayName(path));
            item.Click += (_, _) => { PickFloor(p); _form?.Rebuild(); };
            floors.DropDownItems.Add(item);
        }
        if (Wallpaper.CanRestore(_settings))
        {
            floors.DropDownItems.Add(new ToolStripSeparator());
            Add(floors, "Restore previous wallpaper", (_, _) => { RestoreFloor(); _form?.Rebuild(); });
        }
        menu.Items.Add(floors);

        menu.Items.Add(new ToolStripSeparator());
        Add(menu, "Quit Rug", (_, _) => Quit());
    }

    private string? CurrentRugPath() => _settings.RugPath ?? Assets.AllRugs().FirstOrDefault();

    private static void Add(ToolStripItemCollection items, string text, EventHandler onClick)
    {
        var item = new ToolStripMenuItem(text);
        item.Click += onClick;
        items.Add(item);
    }

    private static void Add(ToolStrip menu, string text, EventHandler onClick) => Add(menu.Items, text, onClick);

    private static void Add(ToolStripDropDownItem parent, string text, EventHandler onClick) => Add(parent.DropDownItems, text, onClick);

    private static Icon LoadAppIcon()
    {
        try { return Assets.AppIcon(32); }
        catch (Exception) { return CreateIcon(); }
    }

    private static Icon CreateIcon()
    {
        using var bmp = new Bitmap(32, 32);
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.Transparent);
            using var brush = new SolidBrush(Color.FromArgb(0, 122, 255));
            g.FillRectangle(brush, 3, 7, 26, 18);
        }
        return Icon.FromHandle(bmp.GetHicon());
    }
}
