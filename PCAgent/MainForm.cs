using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO.Ports;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;

namespace PCHub;

public class MainForm : Form
{
    // ── Palette ────────────────────────────────────────────────────────────────
    static readonly Color ColBg     = ColorTranslator.FromHtml("#080d17");
    static readonly Color ColTop    = ColorTranslator.FromHtml("#101827");
    static readonly Color ColPanel  = ColorTranslator.FromHtml("#111a2a");
    static readonly Color ColCard   = ColorTranslator.FromHtml("#0d1524");
    static readonly Color ColLine   = ColorTranslator.FromHtml("#20324f");
    static readonly Color ColText   = ColorTranslator.FromHtml("#edf5ff");
    static readonly Color ColSub    = ColorTranslator.FromHtml("#91acd2");
    static readonly Color ColMuted  = ColorTranslator.FromHtml("#7890b3");
    static readonly Color ColCyan   = ColorTranslator.FromHtml("#38c7ff");
    static readonly Color ColGreen  = ColorTranslator.FromHtml("#43e884");
    static readonly Color ColOrange = ColorTranslator.FromHtml("#ff922e");
    static readonly Color ColRed    = ColorTranslator.FromHtml("#ff4f5f");
    static readonly Color ColAmber  = ColorTranslator.FromHtml("#ffbd2e");
    static readonly Color ColViolet = ColorTranslator.FromHtml("#a884ff");
    static readonly Color ColBarBg  = ColorTranslator.FromHtml("#263551");

    // ── State ─────────────────────────────────────────────────────────────────
    AppSettings  _settings;
    HardwareMonitor _monitor;
    PcHubClient  _client;
    System.Windows.Forms.Timer _timer;
    bool _suppressClose;
    bool _paused;

    // ── Header ────────────────────────────────────────────────────────────────
    Panel  _headerPanel = null!;
    Panel  _statusDot   = null!;
    Label  _statusLabel = null!;
    Panel  _statusBadge = null!;
    Button _pauseBtn    = null!;
    ToolStripMenuItem _pauseItem = null!;

    // ── Tab system ────────────────────────────────────────────────────────────
    Panel    _tabBar      = null!;
    Button[] _tabBtns     = null!;
    Panel    _monitorPage = null!;
    Panel    _settingsPage = null!;
    int      _selectedTab  = 0;

    // ── Monitor widgets ───────────────────────────────────────────────────────
    Label    _cpuNameLabel  = null!;
    RoundBar _cpuTempBar    = null!;
    Label    _cpuTempLabel  = null!;
    RoundBar _cpuLoadBar    = null!;
    Label    _cpuLoadLabel  = null!;
    Label    _cpuPowerLabel = null!;

    Label    _gpuNameLabel  = null!;
    RoundBar _gpuTempBar    = null!;
    Label    _gpuTempLabel  = null!;
    RoundBar _gpuLoadBar    = null!;
    Label    _gpuLoadLabel  = null!;
    Label    _gpuVramLabel  = null!;

    RoundBar _ramLoadBar     = null!;
    Label    _ramLoadLabel   = null!;
    Label    _ramDetailLabel = null!;
    Label    _lastUpdateLabel = null!;

    // ── Settings widgets ──────────────────────────────────────────────────────
    RadioButton _radioWifi        = null!;
    RadioButton _radioSerial      = null!;
    Panel       _wifiGroup        = null!;
    TextBox     _hostBox          = null!;
    DarkNumeric _portNum          = null!;
    Button      _wifiTestBtn      = null!;
    Panel       _serialGroup      = null!;
    ComboBox    _portCombo        = null!;
    Button      _refreshPortsBtn  = null!;
    Button      _serialTestBtn    = null!;
    DarkNumeric _intervalNum      = null!;
    CheckBox    _trayCheck           = null!;
    CheckBox    _startMinimizedCheck = null!;
    CheckBox    _startupCheck        = null!;
    CheckBox    _batteryCheck        = null!;
    Button      _saveBtn             = null!;

    // ── Tray ──────────────────────────────────────────────────────────────────
    NotifyIcon       _trayIcon = null!;
    ContextMenuStrip _trayMenu = null!;
    Color            _lastTrayColor;

    // ══════════════════════════════════════════════════════════════════════════
    public MainForm()
    {
        _settings = AppSettings.Load();
        _monitor  = new HardwareMonitor();
        _client   = new PcHubClient(_settings);
        _timer    = new System.Windows.Forms.Timer();

        BuildForm();
        BuildTray();
        WireTimer();
        LoadAppIcon();

        HandleCreated += (_, _) =>
        {
            ApplySettingsToUi();
            _monitor.ReadBattery = _settings.ShowBattery;
            _timer.Start();
        };
    }

    Icon? _appIcon;

    void LoadAppIcon()
    {
        var p = Path.Combine(AppContext.BaseDirectory, "pchub.ico");
        if (File.Exists(p)) { _appIcon = new Icon(p); Icon = _appIcon; }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Form construction
    // ══════════════════════════════════════════════════════════════════════════

    void BuildForm()
    {
        SuspendLayout();
        Text            = "PCHUB Agent";
        FormBorderStyle = FormBorderStyle.FixedSingle;
        ClientSize      = new Size(480, 560);
        MaximizeBox     = false;
        MinimizeBox     = true;
        StartPosition   = FormStartPosition.CenterScreen;
        BackColor       = ColBg;
        ForeColor       = ColText;
        BuildHeader();
        BuildTabBar();
        BuildMonitorPage();
        BuildSettingsPage();
        ResumeLayout(true);
    }

    // ── Header ────────────────────────────────────────────────────────────────

    void BuildHeader()
    {
        _headerPanel = new Panel { Dock = DockStyle.Top, Height = 52, BackColor = ColTop };
        _headerPanel.Paint += (_, e) =>
        {
            using var pen = new Pen(ColLine, 1f);
            e.Graphics.DrawLine(pen, 0, _headerPanel.Height - 1, _headerPanel.Width, _headerPanel.Height - 1);
        };

        _statusDot = new Panel { Size = new Size(10, 10), Location = new Point(14, 21), BackColor = ColMuted };
        MakeCircle(_statusDot);

        _statusLabel = new Label
        {
            AutoSize  = false, Location  = new Point(30, 15), Size = new Size(248, 22),
            BackColor = Color.Transparent, ForeColor = ColMuted,
            Font = new Font("Segoe UI", 8.5f), Text = "Disconnected"
        };

        _statusBadge = new Panel { Location = new Point(282, 15), Size = new Size(72, 22), BackColor = Color.Transparent };
        _statusBadge.Paint += DrawBadge;

        _pauseBtn = new Button
        {
            Text = "⏸  Pause", Location = new Point(400, 13), Size = new Size(70, 26),
            BackColor = ColorTranslator.FromHtml("#0d1a2e"), ForeColor = ColSub,
            FlatStyle = FlatStyle.Flat, Font = new Font("Segoe UI", 8f), Cursor = Cursors.Hand,
        };
        _pauseBtn.FlatAppearance.BorderColor = ColLine;
        _pauseBtn.FlatAppearance.BorderSize  = 1;
        _pauseBtn.Click += (_, _) => TogglePause();

        _headerPanel.Controls.Add(_statusDot);
        _headerPanel.Controls.Add(_statusLabel);
        _headerPanel.Controls.Add(_statusBadge);
        _headerPanel.Controls.Add(_pauseBtn);
        Controls.Add(_headerPanel);
    }

    void DrawBadge(object? sender, PaintEventArgs e)
    {
        var p = (Panel)sender!;
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        bool conn = _client?.IsConnected ?? false;
        Color bg, border, fg; string text;
        if (_paused)
        { bg = ColorTranslator.FromHtml("#1f1200"); border = ColorTranslator.FromHtml("#7a4a00"); fg = ColAmber; text = "Paused"; }
        else if (conn)
        { bg = ColorTranslator.FromHtml("#041e0e"); border = ColorTranslator.FromHtml("#155c2e"); fg = ColGreen; text = "● Live"; }
        else
        { bg = ColorTranslator.FromHtml("#1e0508"); border = ColorTranslator.FromHtml("#5e1520"); fg = ColRed; text = "● Offline"; }

        using (var path = PillPath(0.5f, 0.5f, p.Width - 1f, p.Height - 1f))
        {
            using var br = new SolidBrush(bg); g.FillPath(br, path);
            using var pen = new Pen(border, 1f); g.DrawPath(pen, path);
        }
        using var font  = new Font("Segoe UI", 7.5f, FontStyle.Bold);
        using var sf    = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
        using var brush = new SolidBrush(fg);
        g.DrawString(text, font, brush, new RectangleF(0, 0, p.Width, p.Height), sf);
    }

    // ── Custom tab bar (no TabControl) ────────────────────────────────────────

    void BuildTabBar()
    {
        const int barH = 34;
        const int tabW = 110;

        _tabBar = new Panel
        {
            Location  = new Point(0, 52),
            Size      = new Size(ClientSize.Width, barH),
            BackColor = ColTop,
        };
        _tabBar.Paint += (_, e) =>
        {
            using var sep = new Pen(ColLine, 1f);
            e.Graphics.DrawLine(sep, 0, barH - 1, _tabBar.Width, barH - 1);
            using var ap = new Pen(ColCyan, 2f);
            int sx = _selectedTab * tabW;
            e.Graphics.DrawLine(ap, sx + 2, barH - 2, sx + tabW - 2, barH - 2);
        };

        string[] labels = { "MONITOR", "SETTINGS" };
        _tabBtns = new Button[2];
        for (int i = 0; i < 2; i++)
        {
            int idx = i;
            var btn = new Button
            {
                Text      = labels[i],
                Location  = new Point(i * tabW, 0),
                Size      = new Size(tabW, barH),
                FlatStyle = FlatStyle.Flat,
                BackColor = ColTop,
                ForeColor = i == 0 ? ColText : ColMuted,
                Font      = new Font("Segoe UI", 8f, i == 0 ? FontStyle.Bold : FontStyle.Regular),
                Cursor    = Cursors.Hand,
            };
            btn.FlatAppearance.BorderSize         = 0;
            btn.FlatAppearance.MouseOverBackColor = ColorTranslator.FromHtml("#151f30");
            btn.FlatAppearance.MouseDownBackColor = ColCard;
            btn.Click += (_, _) => SelectTab(idx);
            _tabBtns[i] = btn;
            _tabBar.Controls.Add(btn);
        }
        Controls.Add(_tabBar);

        int pageY = 52 + barH;
        int pageH = ClientSize.Height - pageY;

        _monitorPage = new Panel
        {
            Location   = new Point(0, pageY),
            Size       = new Size(ClientSize.Width, pageH),
            BackColor  = ColBg,
            AutoScroll = true,
        };
        _settingsPage = new Panel
        {
            Location   = new Point(0, pageY),
            Size       = new Size(ClientSize.Width, pageH),
            BackColor  = ColBg,
            AutoScroll = true,
            Visible    = false,
        };
        Controls.Add(_monitorPage);
        Controls.Add(_settingsPage);
    }

    void SelectTab(int idx)
    {
        _selectedTab          = idx;
        _monitorPage.Visible  = idx == 0;
        _settingsPage.Visible = idx == 1;
        for (int i = 0; i < _tabBtns.Length; i++)
        {
            _tabBtns[i].ForeColor = i == idx ? ColText : ColMuted;
            _tabBtns[i].Font      = new Font("Segoe UI", 8f, i == idx ? FontStyle.Bold : FontStyle.Regular);
        }
        _tabBar.Invalidate();
        if (idx == 1) RefreshSerialPorts();
    }

    // ── Monitor page ──────────────────────────────────────────────────────────

    void BuildMonitorPage()
    {
        const int cx = 12, cw = 454;
        int y = 10;

        // CPU card
        var cpu = WebCard(cx, y, cw, 112, ColCyan);
        _monitorPage.Controls.Add(cpu);
        cpu.Controls.Add(SectionLabel("CPU", 12, 12));
        _cpuNameLabel = InfoLabel("—", 56, 10, cw - 68); cpu.Controls.Add(_cpuNameLabel);
        cpu.Controls.Add(RowLabel("TEMP", 12, 38));
        _cpuTempBar   = Bar(58, 36, cw - 118, ColCyan, cpu);
        _cpuTempLabel = ValLabel(cx, 38, cw); cpu.Controls.Add(_cpuTempLabel);
        cpu.Controls.Add(RowLabel("LOAD", 12, 57));
        _cpuLoadBar   = Bar(58, 55, cw - 118, ColCyan, cpu);
        _cpuLoadLabel = ValLabel(cx, 57, cw); cpu.Controls.Add(_cpuLoadLabel);
        _cpuPowerLabel = FootLabel("POWER  — W", 12, 79, cw - 24); cpu.Controls.Add(_cpuPowerLabel);
        y += 120;

        // GPU card
        var gpu = WebCard(cx, y, cw, 112, ColViolet);
        _monitorPage.Controls.Add(gpu);
        gpu.Controls.Add(SectionLabel("GPU", 12, 12));
        _gpuNameLabel = InfoLabel("—", 56, 10, cw - 68); gpu.Controls.Add(_gpuNameLabel);
        gpu.Controls.Add(RowLabel("TEMP", 12, 38));
        _gpuTempBar   = Bar(58, 36, cw - 118, ColViolet, gpu);
        _gpuTempLabel = ValLabel(cx, 38, cw); gpu.Controls.Add(_gpuTempLabel);
        gpu.Controls.Add(RowLabel("LOAD", 12, 57));
        _gpuLoadBar   = Bar(58, 55, cw - 118, ColViolet, gpu);
        _gpuLoadLabel = ValLabel(cx, 57, cw); gpu.Controls.Add(_gpuLoadLabel);
        _gpuVramLabel = FootLabel("VRAM  — / — MB", 12, 79, cw - 24); gpu.Controls.Add(_gpuVramLabel);
        y += 120;

        // RAM card
        var ram = WebCard(cx, y, cw, 64, ColGreen);
        _monitorPage.Controls.Add(ram);
        ram.Controls.Add(SectionLabel("RAM", 12, 12));
        _ramLoadBar   = Bar(58, 10, cw - 118, ColGreen, ram);
        _ramLoadLabel = ValLabel(cx, 10, cw); ram.Controls.Add(_ramLoadLabel);
        _ramDetailLabel = FootLabel("— GB / — GB", 12, 32, cw - 24); ram.Controls.Add(_ramDetailLabel);
        y += 72;

        _lastUpdateLabel = new Label
        {
            Text = "Last update: —", Location = new Point(cx, y), Size = new Size(cw, 14),
            Font = new Font("Consolas", 7.5f), ForeColor = ColMuted, BackColor = Color.Transparent,
        };
        _monitorPage.Controls.Add(_lastUpdateLabel);
    }

    // ── Settings page ─────────────────────────────────────────────────────────

    void BuildSettingsPage()
    {
        int y = 14;
        const int iW = 448;

        var connHdr = MakeLabel("CONNECTION", 12, y, 200, 13, ColMuted, 7.5f);
        connHdr.Font = new Font("Consolas", 7.5f, FontStyle.Bold);
        _settingsPage.Controls.Add(connHdr);
        y += 20;

        _radioWifi = new RadioButton
        {
            Text = "WiFi", Location = new Point(12, y), Size = new Size(74, 22),
            ForeColor = ColText, BackColor = ColBg, Checked = true,
            Font = new Font("Segoe UI", 9f)
        };
        _radioWifi.HandleCreated += (s, _) => SetWindowTheme(((Control)s!).Handle, "DarkMode_Explorer", null);
        _radioSerial = new RadioButton
        {
            Text = "USB Serial", Location = new Point(90, y), Size = new Size(96, 22),
            ForeColor = ColText, BackColor = ColBg,
            Font = new Font("Segoe UI", 9f)
        };
        _radioSerial.HandleCreated += (s, _) => SetWindowTheme(((Control)s!).Handle, "DarkMode_Explorer", null);
        _settingsPage.Controls.Add(_radioWifi);
        _settingsPage.Controls.Add(_radioSerial);
        _radioWifi.CheckedChanged   += (_, _) => UpdateConnectionGroupVisibility();
        _radioSerial.CheckedChanged += (_, _) => UpdateConnectionGroupVisibility();
        y += 28;

        int cardY = y;

        // WiFi card
        _wifiGroup = WebCard(10, cardY, iW, 76, ColCyan);
        _settingsPage.Controls.Add(_wifiGroup);
        _wifiGroup.Controls.Add(MakeLabel("Host", 10, 13, 40, 14, ColMuted, 8f));
        var (hw, hb) = MakeInputBox(54, 7, 252);
        hb.Text = _settings.WifiHost; _hostBox = hb;
        _wifiGroup.Controls.Add(hw);
        _wifiGroup.Controls.Add(MakeLabel("Port", 322, 13, 40, 14, ColMuted, 8f));
        _portNum = MakeNumericBox(364, 7, _settings.WifiPort, 1, 65535, 68);
        _wifiGroup.Controls.Add(_portNum);
        _wifiTestBtn = MakeButton("Test", 10, 42, 70, 24, ColCyan, Color.Black);
        _wifiTestBtn.Click += async (_, _) => await TestConnectionAsync();
        _wifiGroup.Controls.Add(_wifiTestBtn);

        // Serial card
        _serialGroup = WebCard(10, cardY, iW, 76, ColCyan);
        _serialGroup.Visible = false;
        _settingsPage.Controls.Add(_serialGroup);
        _serialGroup.Controls.Add(MakeLabel("Port", 10, 13, 40, 14, ColMuted, 8f));
        _portCombo = new ComboBox
        {
            Location = new Point(54, 7), Size = new Size(200, 26),
            BackColor = ColPanel, ForeColor = ColText,
            FlatStyle = FlatStyle.Flat, DropDownStyle = ComboBoxStyle.DropDownList,
            Font = new Font("Segoe UI", 9f),
        };
        _portCombo.HandleCreated += (s, _) => SetWindowTheme(((Control)s!).Handle, "", "");
        _serialGroup.Controls.Add(_portCombo);
        _refreshPortsBtn = MakeButton("Refresh", 262, 7, 68, 26, ColPanel, ColMuted);
        _refreshPortsBtn.Click += (_, _) => RefreshSerialPorts();
        _serialGroup.Controls.Add(_refreshPortsBtn);
        _serialTestBtn = MakeButton("Test", 10, 42, 70, 24, ColCyan, Color.Black);
        _serialTestBtn.Click += async (_, _) => await TestConnectionAsync();
        _serialGroup.Controls.Add(_serialTestBtn);

        y = cardY + 84;

        _settingsPage.Controls.Add(MakeSep(10, y, iW)); y += 14;
        _settingsPage.Controls.Add(MakeLabel("Update interval (seconds):", 12, y + 4, 210, 16, ColMuted, 8.5f));
        _intervalNum = MakeNumericBox(228, y, Math.Clamp(_settings.UpdateIntervalSec, 1, 60), 1, 60, 66);
        _settingsPage.Controls.Add(_intervalNum);
        y += 38;

        _settingsPage.Controls.Add(MakeSep(10, y, iW)); y += 12;
        _trayCheck           = MakeCheck("Minimize to system tray on close", y, _settings.MinimizeToTray); _settingsPage.Controls.Add(_trayCheck); y += 26;
        _startMinimizedCheck = MakeCheck("Start minimized to tray",          y, _settings.StartMinimized); _settingsPage.Controls.Add(_startMinimizedCheck); y += 26;
        _startupCheck        = MakeCheck("Run on Windows startup",            y, _settings.RunOnStartup);  _settingsPage.Controls.Add(_startupCheck); y += 26;
        _batteryCheck        = MakeCheck("Send battery status to display",    y, _settings.ShowBattery);   _settingsPage.Controls.Add(_batteryCheck); y += 34;

        _saveBtn = MakeButton("Save Settings", 12, y, 120, 32, ColCyan, Color.Black);
        _saveBtn.Font  = new Font("Segoe UI", 9.5f, FontStyle.Bold);
        _saveBtn.Click += OnSaveSettings;
        _settingsPage.Controls.Add(_saveBtn);
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Tray
    // ══════════════════════════════════════════════════════════════════════════

    void BuildTray()
    {
        _trayMenu = new ContextMenuStrip { BackColor = ColPanel, ForeColor = ColText, Renderer = new DarkMenuRenderer() };

        var showItem = new ToolStripMenuItem("Show / Hide") { Font = new Font("Segoe UI", 9f), ForeColor = ColText };
        showItem.Click += (_, _) => ToggleVisibility();

        _pauseItem = new ToolStripMenuItem("⏸  Pause") { Font = new Font("Segoe UI", 9f), ForeColor = ColAmber };
        _pauseItem.Click += (_, _) => TogglePause();

        var exitItem = new ToolStripMenuItem("Exit") { Font = new Font("Segoe UI", 9f), ForeColor = ColRed };
        exitItem.Click += (_, _) => ExitApp();

        _trayMenu.Items.Add(showItem);
        _trayMenu.Items.Add(_pauseItem);
        _trayMenu.Items.Add(new ToolStripSeparator());
        _trayMenu.Items.Add(exitItem);

        _trayIcon = new NotifyIcon
        {
            Text = "PCHUB Agent", Icon = CreateTrayIcon(ColMuted),
            ContextMenuStrip = _trayMenu, Visible = true
        };
        _lastTrayColor = ColMuted;
        _trayIcon.DoubleClick += (_, _) => ToggleVisibility();
    }

    void SwapTrayIcon(Color color)
    {
        var old = _trayIcon.Icon;
        _trayIcon.Icon = CreateTrayIcon(color);
        old?.Dispose();
    }

    static Icon CreateTrayIcon(Color statusColor)
    {
        using var bmp = new Bitmap(16, 16);
        using var g   = Graphics.FromImage(bmp);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Color.Transparent);
        using (var pen = new Pen(ColCyan, 1.2f)) g.DrawRectangle(pen, 1f, 2f, 14f, 9f);
        using (var b = new SolidBrush(ColorTranslator.FromHtml("#111a2a"))) g.FillRectangle(b, 3f, 4f, 10f, 5f);
        using (var pen = new Pen(ColCyan, 1f)) g.DrawLine(pen, 8f, 11f, 8f, 13f);
        using (var pen = new Pen(ColCyan, 1.5f)) g.DrawLine(pen, 5f, 13f, 11f, 13f);
        using (var b = new SolidBrush(statusColor)) g.FillEllipse(b, 0f, 11f, 5f, 5f);
        using (var pen = new Pen(Color.FromArgb(160, 0, 0, 0), 0.8f)) g.DrawEllipse(pen, 0f, 11f, 5f, 5f);
        return Icon.FromHandle(bmp.GetHicon());
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Timer / data loop
    // ══════════════════════════════════════════════════════════════════════════

    void WireTimer()
    {
        _timer.Interval = _settings.UpdateIntervalSec * 1000;
        _timer.Tick    += OnTimerTick;
    }

    async void OnTimerTick(object? sender, EventArgs e)
    {
        _timer.Stop();
        try
        {
            if (_paused) return;
            var metrics = await Task.Run(() => _monitor.Read());
            string bat  = metrics.HasBattery ? $"  |  BAT {metrics.BatteryPercent:F0}%" : "";
            Text = $"PCHUB Agent  |  {metrics.CpuName}  |  {metrics.GpuName}{bat}";
            await _client.SendAsync(metrics);
            UpdateMonitorUi(metrics);
            UpdateHeaderUi();
        }
        catch (Exception ex)
        {
            _statusLabel.Text    = $"Error: {ex.Message}";
            _statusDot.BackColor = ColRed;
        }
        finally
        {
            _timer.Interval = _settings.UpdateIntervalSec * 1000;
            _timer.Start();
        }
    }

    void TogglePause()
    {
        _paused = !_paused;
        if (_paused)
        {
            _pauseBtn.Text = "▶  Resume"; _pauseBtn.ForeColor = ColGreen;
            _pauseItem.Text = "▶  Resume";
            _statusLabel.Text = "Paused"; _statusLabel.ForeColor = ColMuted;
            _statusDot.BackColor = ColAmber;
            _trayIcon.Text = "PCHUB Agent (Paused)";
            _statusBadge.Invalidate(); SwapTrayIcon(ColAmber); _lastTrayColor = ColAmber;
        }
        else
        {
            _pauseBtn.Text = "⏸  Pause"; _pauseBtn.ForeColor = ColSub;
            _pauseItem.Text = "⏸  Pause";
            _trayIcon.Text = "PCHUB Agent";
            _statusBadge.Invalidate(); _lastTrayColor = Color.Empty; UpdateHeaderUi();
        }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // UI update
    // ══════════════════════════════════════════════════════════════════════════

    void UpdateMonitorUi(PcMetrics m)
    {
        if (!string.IsNullOrWhiteSpace(m.CpuName)) _cpuNameLabel.Text = m.CpuName;

        _cpuTempBar.Value = m.CpuTemp / 100f; _cpuTempBar.Fill = BarColor(m.CpuTemp / 100f);
        _cpuTempLabel.Text = $"{m.CpuTemp:F0}°C"; _cpuTempLabel.ForeColor = _cpuTempBar.Fill;
        _cpuLoadBar.Value = m.CpuLoad / 100f; _cpuLoadBar.Fill = BarColor(m.CpuLoad / 100f);
        _cpuLoadLabel.Text = $"{m.CpuLoad:F0}%";
        _cpuPowerLabel.Text = $"POWER  {m.CpuPower:F1} W";

        if (!string.IsNullOrWhiteSpace(m.GpuName)) _gpuNameLabel.Text = m.GpuName;

        _gpuTempBar.Value = m.GpuTemp / 100f; _gpuTempBar.Fill = BarColor(m.GpuTemp / 100f);
        _gpuTempLabel.Text = $"{m.GpuTemp:F0}°C"; _gpuTempLabel.ForeColor = _gpuTempBar.Fill;
        _gpuLoadBar.Value = m.GpuLoad / 100f; _gpuLoadBar.Fill = BarColor(m.GpuLoad / 100f);
        _gpuLoadLabel.Text = $"{m.GpuLoad:F0}%";
        _gpuVramLabel.Text = m.GpuVramTotalMb > 0
            ? $"VRAM  {m.GpuVramUsedMb:N0} / {m.GpuVramTotalMb:N0} MB" : "VRAM  n/a";

        float rp = m.RamTotalMb > 0 ? (float)m.RamUsedMb / m.RamTotalMb : 0f;
        _ramLoadBar.Value = rp; _ramLoadBar.Fill = BarColor(rp);
        _ramLoadLabel.Text = $"{rp * 100f:F0}%";
        _ramDetailLabel.Text = $"{m.RamUsedMb / 1024f:F1} GB / {m.RamTotalMb / 1024f:F1} GB";
        _lastUpdateLabel.Text = $"Last update: {DateTime.Now:HH:mm:ss}";
    }

    void UpdateHeaderUi()
    {
        if (_paused) return;
        var c = _client.IsConnected ? ColGreen : _client.IsPaused ? ColAmber : ColRed;
        _statusDot.BackColor = c;
        _statusLabel.Text = _client.StatusMessage;
        _statusLabel.ForeColor = _client.IsConnected ? ColorTranslator.FromHtml("#a9c8ee") : ColMuted;
        _statusBadge.Invalidate();
        if (_lastTrayColor != c) { SwapTrayIcon(c); _lastTrayColor = c; }
    }

    static Color BarColor(float f) =>
        f >= 0.8f ? ColRed : f >= 0.6f ? ColOrange : ColCyan;

    // ══════════════════════════════════════════════════════════════════════════
    // Settings helpers
    // ══════════════════════════════════════════════════════════════════════════

    void ApplySettingsToUi()
    {
        _radioWifi.Checked           = _settings.ConnectionType.Equals("wifi",   StringComparison.OrdinalIgnoreCase);
        _radioSerial.Checked         = _settings.ConnectionType.Equals("serial", StringComparison.OrdinalIgnoreCase);
        _hostBox.Text                = _settings.WifiHost;
        _portNum.Value               = Math.Clamp(_settings.WifiPort, 1, 65535);
        _intervalNum.Value           = Math.Clamp(_settings.UpdateIntervalSec, 1, 60);
        _trayCheck.Checked           = _settings.MinimizeToTray;
        _startMinimizedCheck.Checked = _settings.StartMinimized;
        _startupCheck.Checked        = _settings.RunOnStartup;
        _batteryCheck.Checked        = _settings.ShowBattery;
        UpdateConnectionGroupVisibility();
    }

    void UpdateConnectionGroupVisibility()
    {
        _wifiGroup.Visible   = _radioWifi.Checked;
        _serialGroup.Visible = _radioSerial.Checked;
    }

    void RefreshSerialPorts()
    {
        string cur = _portCombo.Text;
        _portCombo.Items.Clear();
        foreach (var p in SerialPort.GetPortNames()) _portCombo.Items.Add(p);
        if (!string.IsNullOrWhiteSpace(cur) && _portCombo.Items.Contains(cur))
            _portCombo.SelectedItem = cur;
        else if (!string.IsNullOrWhiteSpace(_settings.SerialPort) && _portCombo.Items.Contains(_settings.SerialPort))
            _portCombo.SelectedItem = _settings.SerialPort;
        else if (_portCombo.Items.Count > 0)
            _portCombo.SelectedIndex = 0;
    }

    void OnSaveSettings(object? sender, EventArgs e)
    {
        _settings.ConnectionType    = _radioWifi.Checked ? "wifi" : "serial";
        _settings.WifiHost          = _hostBox.Text.Trim();
        _settings.WifiPort          = (int)_portNum.Value;
        _settings.SerialPort        = _portCombo.Text;
        _settings.UpdateIntervalSec = (int)_intervalNum.Value;
        _settings.MinimizeToTray    = _trayCheck.Checked;
        _settings.StartMinimized    = _startMinimizedCheck.Checked;
        _settings.RunOnStartup      = _startupCheck.Checked;
        _settings.ShowBattery       = _batteryCheck.Checked;
        _settings.Save();
        SetStartupEnabled(_settings.RunOnStartup);
        _monitor.ReadBattery = _settings.ShowBattery;
        _client.Dispose(); _client = new PcHubClient(_settings);
        _timer.Stop(); _timer.Interval = _settings.UpdateIntervalSec * 1000; _timer.Start();

        _saveBtn.Text = "Saved!"; _saveBtn.BackColor = ColGreen;
        var t = new System.Windows.Forms.Timer { Interval = 1500 };
        t.Tick += (_, _) => { _saveBtn.Text = "Save Settings"; _saveBtn.BackColor = ColCyan; t.Dispose(); };
        t.Start();
    }

    async Task TestConnectionAsync()
    {
        var ts = new AppSettings
        {
            ConnectionType = _radioWifi.Checked ? "wifi" : "serial",
            WifiHost = _hostBox.Text.Trim(), WifiPort = (int)_portNum.Value,
            SerialPort = _portCombo.Text, UpdateIntervalSec = (int)_intervalNum.Value,
            MinimizeToTray = _trayCheck.Checked
        };
        using var tc = new PcHubClient(ts);
        string res = await tc.TestConnectionAsync();
        MessageBox.Show(res, "Connection Test", MessageBoxButtons.OK,
            tc.IsConnected ? MessageBoxIcon.Information : MessageBoxIcon.Warning);
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Window / tray behaviour
    // ══════════════════════════════════════════════════════════════════════════

    protected override void SetVisibleCore(bool value)
    {
        if (!IsHandleCreated && _settings.StartMinimized) { CreateHandle(); value = false; }
        base.SetVisibleCore(value);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (e.CloseReason == CloseReason.UserClosing && _settings.MinimizeToTray && !_suppressClose)
        {
            e.Cancel = true; Hide();
            _trayIcon.ShowBalloonTip(2000, "PCHUB Agent", "Running in background.", ToolTipIcon.Info);
            return;
        }
        base.OnFormClosing(e);
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _timer.Stop(); _timer.Dispose(); _monitor.Dispose(); _client.Dispose();
        _trayIcon.Visible = false; _trayIcon.Dispose(); _appIcon?.Dispose();
        base.OnFormClosed(e);
    }

    void ToggleVisibility()
    {
        if (Visible) Hide();
        else { Show(); WindowState = FormWindowState.Normal; Activate(); }
    }

    void ExitApp() { _suppressClose = true; Close(); }

    static void SetStartupEnabled(bool enabled)
    {
        const string runKey = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Run";
        using var key = Registry.CurrentUser.OpenSubKey(runKey, writable: true);
        if (key == null) return;
        if (enabled) key.SetValue("PCHUB Agent", $"\"{Application.ExecutablePath}\"");
        else         key.DeleteValue("PCHUB Agent", throwOnMissingValue: false);
    }

    // ══════════════════════════════════════════════════════════════════════════
    // UI factory helpers
    // ══════════════════════════════════════════════════════════════════════════

    static Panel WebCard(int x, int y, int w, int h, Color accent)
    {
        var p = new Panel { Location = new Point(x, y), Size = new Size(w, h), BackColor = ColCard };
        using var rg = CardPath(0, 0, w, h, 8f);
        p.Region = new Region(rg);
        p.Paint += (_, e) =>
        {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            using var bp  = CardPath(0.5f, 0.5f, w - 1f, h - 1f, 7.5f);
            using var pen = new Pen(ColLine, 1f); e.Graphics.DrawPath(pen, bp);
            using var ap  = new Pen(accent, 2f);  e.Graphics.DrawLine(ap, 9f, 1f, w - 10f, 1f);
        };
        return p;
    }

    static RoundBar Bar(int x, int y, int w, Color fill, Panel parent)
    {
        var b = new RoundBar { Location = new Point(x, y), Size = new Size(w, 8), Fill = fill };
        parent.Controls.Add(b);
        return b;
    }

    static Label SectionLabel(string text, int x, int y) => new Label
    {
        Text = text, Location = new Point(x, y), Size = new Size(40, 12),
        Font = new Font("Consolas", 8f, FontStyle.Bold),
        ForeColor = ColSub, BackColor = Color.Transparent, AutoSize = false,
    };

    static Label RowLabel(string text, int x, int y) => new Label
    {
        Text = text, Location = new Point(x, y + 1), Size = new Size(44, 12),
        Font = new Font("Consolas", 7.5f), ForeColor = ColMuted,
        BackColor = Color.Transparent, AutoSize = false,
    };

    static Label ValLabel(int cx, int y, int cw) => new Label
    {
        Text = "—", Location = new Point(cw - 56, y - 1), Size = new Size(54, 16),
        Font = new Font("Consolas", 9f, FontStyle.Bold),
        ForeColor = ColText, BackColor = Color.Transparent,
        TextAlign = ContentAlignment.MiddleRight, AutoSize = false,
    };

    static Label InfoLabel(string text, int x, int y, int w) => new Label
    {
        Text = text, Location = new Point(x, y), Size = new Size(w, 16),
        Font = new Font("Consolas", 8f), ForeColor = ColMuted,
        BackColor = Color.Transparent, AutoSize = false,
    };

    static Label FootLabel(string text, int x, int y, int w) => new Label
    {
        Text = text, Location = new Point(x, y), Size = new Size(w, 14),
        Font = new Font("Consolas", 7.5f), ForeColor = ColorTranslator.FromHtml("#5a7ea8"),
        BackColor = Color.Transparent, AutoSize = false,
    };

    static GraphicsPath CardPath(float x, float y, float w, float h, float r)
    {
        float d = r * 2f;
        var p = new GraphicsPath();
        p.AddArc(x,         y,         d, d, 180f, 90f);
        p.AddArc(x + w - d, y,         d, d, 270f, 90f);
        p.AddArc(x + w - d, y + h - d, d, d,   0f, 90f);
        p.AddArc(x,         y + h - d, d, d,  90f, 90f);
        p.CloseFigure();
        return p;
    }

    static GraphicsPath PillPath(float x, float y, float w, float h)
    {
        var p = new GraphicsPath();
        p.AddArc(x, y, h, h, 90f, 180f);
        p.AddArc(x + w - h, y, h, h, 270f, 180f);
        p.CloseFigure();
        return p;
    }

    static Label MakeLabel(string text, int x, int y, int w, int h,
        Color? fore = null, float fs = 8.5f,
        ContentAlignment align = ContentAlignment.MiddleLeft) => new Label
    {
        Text = text, Location = new Point(x, y), Size = new Size(w, h),
        ForeColor = fore ?? ColText, BackColor = Color.Transparent,
        Font = new Font("Segoe UI", fs), TextAlign = align, AutoSize = false,
    };

    static (Panel wrap, TextBox box) MakeInputBox(int x, int y, int w)
    {
        var wrap = new Panel { Location = new Point(x, y), Size = new Size(w, 26), BackColor = ColPanel };
        wrap.Paint += (_, e) =>
        {
            using var pen = new Pen(ColLine, 1f);
            e.Graphics.DrawRectangle(pen, 0, 0, wrap.Width - 1, wrap.Height - 1);
        };
        var box = new TextBox
        {
            Location = new Point(2, 4), Size = new Size(w - 4, 18),
            BackColor = ColPanel, ForeColor = ColText, BorderStyle = BorderStyle.None,
            Font = new Font("Segoe UI", 9f),
        };
        wrap.Controls.Add(box);
        return (wrap, box);
    }

    static DarkNumeric MakeNumericBox(int x, int y, decimal value, decimal min, decimal max, int w = 66)
    {
        var num = new DarkNumeric { Location = new Point(x, y), Size = new Size(w, 26), Minimum = min, Maximum = max };
        num.Value = value;
        return num;
    }

    static Panel MakeSep(int x, int y, int w)
        => new Panel { Location = new Point(x, y), Size = new Size(w, 1), BackColor = ColLine };

    static CheckBox MakeCheck(string text, int y, bool @checked)
    {
        var cb = new CheckBox
        {
            Text = text, Location = new Point(12, y), Size = new Size(430, 22),
            ForeColor = ColText, BackColor = ColBg, Checked = @checked,
            Font = new Font("Segoe UI", 9f),
        };
        cb.HandleCreated += (s, _) => SetWindowTheme(((Control)s!).Handle, "DarkMode_Explorer", null);
        return cb;
    }

    static Button MakeButton(string text, int x, int y, int w, int h, Color back, Color fore)
        => new Button
        {
            Text = text, Location = new Point(x, y), Size = new Size(w, h),
            BackColor = back, ForeColor = fore, FlatStyle = FlatStyle.Flat,
            Font = new Font("Segoe UI", 8.5f), Cursor = Cursors.Hand,
            FlatAppearance = { BorderSize = 0 }
        };

    static void MakeCircle(Panel p)
    {
        p.Paint += (_, e) =>
        {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            using var b = new SolidBrush(p.BackColor);
            e.Graphics.Clear(p.Parent?.BackColor ?? ColTop);
            e.Graphics.FillEllipse(b, 0, 0, p.Width - 1, p.Height - 1);
        };
    }

    // ══════════════════════════════════════════════════════════════════════════
    // RoundBar
    // ══════════════════════════════════════════════════════════════════════════

    sealed class RoundBar : Control
    {
        float _value;
        Color _fill = ColCyan;

        [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public float Value { get => _value; set { _value = Math.Clamp(value, 0, 1); Invalidate(); } }
        [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public Color Fill  { get => _fill;  set { _fill = value; Invalidate(); } }

        public RoundBar()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.ResizeRedraw | ControlStyles.UserPaint |
                     ControlStyles.SupportsTransparentBackColor, true);
            BackColor = Color.Transparent;
        }

        protected override void OnPaintBackground(PaintEventArgs e) { }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            int w = Width, h = Height;
            using (var tp = Pill(0, 0, w, h))
            using (var tb = new SolidBrush(ColBarBg))
                g.FillPath(tb, tp);
            float fw = w * _value;
            if (fw >= h)
            {
                using var fp = Pill(0, 0, fw, h);
                using var fb = new SolidBrush(_fill);
                g.FillPath(fb, fp);
            }
        }

        static GraphicsPath Pill(float x, float y, float w, float h)
        {
            var p = new GraphicsPath();
            if (w <= h) { p.AddEllipse(x, y, w, h); return p; }
            p.AddArc(x, y, h, h, 90f, 180f);
            p.AddArc(x + w - h, y, h, h, 270f, 180f);
            p.CloseFigure();
            return p;
        }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // DarkNumeric — fully custom dark numeric input (replaces NumericUpDown)
    // ══════════════════════════════════════════════════════════════════════════

    sealed class DarkNumeric : Panel
    {
        decimal _value;
        [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public decimal Minimum { get; set; } = 0;
        [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public decimal Maximum { get; set; } = 100;
        [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public decimal Value
        {
            get => _value;
            set { _value = Math.Clamp(value, Minimum, Maximum); _txt.Text = ((int)_value).ToString(); }
        }

        readonly TextBox _txt;
        const int ArrowW = 20;

        public DarkNumeric()
        {
            BackColor = ColPanel;
            SetStyle(ControlStyles.ResizeRedraw, true);

            _txt = new TextBox
            {
                BackColor = ColPanel, ForeColor = ColText,
                BorderStyle = BorderStyle.None, Font = new Font("Segoe UI", 9f), Text = "0",
            };
            _txt.TextChanged += (_, _) =>
            {
                if (decimal.TryParse(_txt.Text, out var v))
                    _value = Math.Clamp(v, Minimum, Maximum);
            };
            _txt.KeyDown += (_, e) =>
            {
                if (e.KeyCode == Keys.Up)   { Value++; e.Handled = true; }
                if (e.KeyCode == Keys.Down) { Value--; e.Handled = true; }
            };
            _txt.KeyPress += (_, e) =>
            {
                if (!char.IsDigit(e.KeyChar) && e.KeyChar != '\b') e.Handled = true;
            };
            Controls.Add(_txt);
            Paint       += OnPaint;
            MouseDown   += OnMouseDown;
            SizeChanged += (_, _) => PositionText();
            PositionText();
        }

        void PositionText()
        {
            _txt.Location = new Point(4, 4);
            _txt.Size     = new Size(Width - ArrowW - 6, 18);
        }

        void OnPaint(object? sender, PaintEventArgs e)
        {
            var g = e.Graphics;
            using (var pen = new Pen(ColLine, 1f))
                g.DrawRectangle(pen, 0, 0, Width - 1, Height - 1);
            int bx = Width - ArrowW;
            using (var pen = new Pen(ColLine, 1f))
                g.DrawLine(pen, bx, 1, bx, Height - 2);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            int ax = bx + ArrowW / 2, hy = Height / 2;
            using var br = new SolidBrush(ColSub);
            g.FillPolygon(br, new PointF[] { new(ax-3.5f, hy-2f), new(ax+3.5f, hy-2f), new(ax, hy-6f) });
            g.FillPolygon(br, new PointF[] { new(ax-3.5f, hy+2f), new(ax+3.5f, hy+2f), new(ax, hy+6f) });
        }

        void OnMouseDown(object? sender, MouseEventArgs e)
        {
            if (e.X < Width - ArrowW) { _txt.Focus(); return; }
            Value += e.Y < Height / 2 ? 1 : -1;
        }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Dark context-menu renderer
    // ══════════════════════════════════════════════════════════════════════════

    class DarkMenuRenderer : ToolStripProfessionalRenderer
    {
        public DarkMenuRenderer() : base(new DarkColorTable()) { }
        protected override void OnRenderMenuItemBackground(ToolStripItemRenderEventArgs e)
        {
            using var b = new SolidBrush(e.Item.Selected
                ? ColorTranslator.FromHtml("#1a2840")
                : ColorTranslator.FromHtml("#111a2a"));
            e.Graphics.FillRectangle(b, new Rectangle(Point.Empty, e.Item.Size));
        }
    }

    class DarkColorTable : ProfessionalColorTable
    {
        public override Color MenuBorder                  => ColorTranslator.FromHtml("#20324f");
        public override Color ToolStripDropDownBackground => ColorTranslator.FromHtml("#111a2a");
        public override Color ImageMarginGradientBegin    => ColorTranslator.FromHtml("#111a2a");
        public override Color ImageMarginGradientMiddle   => ColorTranslator.FromHtml("#111a2a");
        public override Color ImageMarginGradientEnd      => ColorTranslator.FromHtml("#111a2a");
        public override Color SeparatorDark               => ColorTranslator.FromHtml("#20324f");
        public override Color SeparatorLight              => ColorTranslator.FromHtml("#20324f");
    }

    [System.Runtime.InteropServices.DllImport("uxtheme.dll", ExactSpelling = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    static extern int SetWindowTheme(IntPtr hWnd, string? appName, string? idList);
}
