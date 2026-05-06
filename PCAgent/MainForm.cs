using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO.Ports;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace PCHub;

public class MainForm : Form
{
    // ── Palette ────────────────────────────────────────────────────────────────
    private static readonly Color ColBg      = ColorTranslator.FromHtml("#0D0D1A");
    private static readonly Color ColPanel   = ColorTranslator.FromHtml("#18192F");
    private static readonly Color ColCard    = ColorTranslator.FromHtml("#1C2040");
    private static readonly Color ColText    = Color.White;
    private static readonly Color ColMuted   = ColorTranslator.FromHtml("#A0A0B8");
    private static readonly Color ColCyan    = ColorTranslator.FromHtml("#00B4FF");
    private static readonly Color ColAmber   = ColorTranslator.FromHtml("#FDA000");
    private static readonly Color ColGreen   = ColorTranslator.FromHtml("#00E400");
    private static readonly Color ColRed     = ColorTranslator.FromHtml("#FF5050");

    // ── State ──────────────────────────────────────────────────────────────────
    private AppSettings _settings;
    private HardwareMonitor _monitor;
    private PcHubClient _client;
    private System.Windows.Forms.Timer _timer;
    private bool _suppressClose;
    private bool _paused;

    // ── Header ─────────────────────────────────────────────────────────────────
    private Panel  _headerPanel = null!;
    private Panel  _statusDot   = null!;
    private Label  _statusLabel = null!;
    private Button _pauseBtn    = null!;
    private ToolStripMenuItem _pauseItem = null!;

    // ── Tab control ────────────────────────────────────────────────────────────
    private TabControl _tabs = null!;
    private TabPage _monitorTab = null!;
    private TabPage _settingsTab = null!;

    // ── Monitor tab widgets ────────────────────────────────────────────────────
    private Label _cpuNameLabel   = null!;
    private Panel _cpuTempTrack   = null!;
    private Panel _cpuTempFill    = null!;
    private Label _cpuTempLabel   = null!;
    private Panel _cpuLoadTrack   = null!;
    private Panel _cpuLoadFill    = null!;
    private Label _cpuLoadLabel   = null!;
    private Label _cpuPowerLabel  = null!;

    private Label _gpuNameLabel   = null!;
    private Panel _gpuTempTrack   = null!;
    private Panel _gpuTempFill    = null!;
    private Label _gpuTempLabel   = null!;
    private Panel _gpuLoadTrack   = null!;
    private Panel _gpuLoadFill    = null!;
    private Label _gpuLoadLabel   = null!;
    private Label _gpuVramLabel   = null!;

    private Panel _ramLoadTrack   = null!;
    private Panel _ramLoadFill    = null!;
    private Label _ramLoadLabel   = null!;
    private Label _ramDetailLabel = null!;

    private Label _lastUpdateLabel = null!;

    // ── Settings tab widgets ───────────────────────────────────────────────────
    private RadioButton _radioWifi   = null!;
    private RadioButton _radioSerial = null!;

    private GroupBox _wifiGroup    = null!;
    private TextBox  _hostBox      = null!;
    private NumericUpDown _portNum = null!;
    private Button   _wifiTestBtn  = null!;

    private GroupBox  _serialGroup   = null!;
    private ComboBox  _portCombo     = null!;
    private Button    _refreshPortsBtn = null!;
    private Button    _serialTestBtn   = null!;

    private NumericUpDown _intervalNum  = null!;
    private CheckBox      _trayCheck    = null!;
    private Button        _saveBtn      = null!;

    // ── Tray ───────────────────────────────────────────────────────────────────
    private NotifyIcon   _trayIcon = null!;
    private ContextMenuStrip _trayMenu = null!;

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

        // Start timer after form is shown
        Load += (_, _) =>
        {
            ApplySettingsToUi();
            _timer.Start();
        };
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Form construction
    // ══════════════════════════════════════════════════════════════════════════

    private void BuildForm()
    {
        SuspendLayout();

        Text             = "PCHUB Agent";
        Size             = new Size(440, 530);
        MinimumSize      = new Size(440, 530);
        MaximumSize      = new Size(440, 530);
        FormBorderStyle  = FormBorderStyle.FixedSingle;
        MaximizeBox      = false;
        MinimizeBox      = true;
        StartPosition    = FormStartPosition.CenterScreen;
        BackColor        = ColBg;
        ForeColor        = ColText;

        BuildHeader();
        BuildTabControl();
        BuildMonitorTab();
        BuildSettingsTab();

        ResumeLayout(true);
    }

    // ─── Header ───────────────────────────────────────────────────────────────

    private void BuildHeader()
    {
        _headerPanel = new Panel
        {
            Dock      = DockStyle.Top,
            Height    = 36,
            BackColor = ColPanel,
            Padding   = new Padding(10, 0, 10, 0)
        };

        _statusDot = new Panel
        {
            Size      = new Size(10, 10),
            Location  = new Point(12, 13),
            BackColor = ColMuted
        };
        MakeCircle(_statusDot);

        _statusLabel = new Label
        {
            AutoSize  = false,
            Location  = new Point(30, 9),
            Size      = new Size(290, 18),
            BackColor = Color.Transparent,
            ForeColor = ColMuted,
            Font      = new Font("Segoe UI", 9f),
            Text      = "Disconnected"
        };

        _pauseBtn = new Button
        {
            Text        = "⏸  Pause",
            Location    = new Point(334, 6),
            Size        = new Size(90, 24),
            BackColor   = ColCard,
            ForeColor   = ColMuted,
            FlatStyle   = FlatStyle.Flat,
            Font        = new Font("Segoe UI", 8.5f),
            Cursor      = Cursors.Hand,
        };
        _pauseBtn.FlatAppearance.BorderColor = ColPanel;
        _pauseBtn.Click += (_, _) => TogglePause();

        _headerPanel.Controls.Add(_statusDot);
        _headerPanel.Controls.Add(_statusLabel);
        _headerPanel.Controls.Add(_pauseBtn);
        Controls.Add(_headerPanel);
    }

    // ─── Tab control ──────────────────────────────────────────────────────────

    private void BuildTabControl()
    {
        _tabs = new TabControl
        {
            Location  = new Point(0, 36),
            Size      = new Size(440, 494),
            Dock      = DockStyle.None,
            DrawMode  = TabDrawMode.OwnerDrawFixed,
            ItemSize  = new Size(100, 28),
            BackColor = ColBg,
            Padding   = new Point(16, 4)
        };

        _tabs.DrawItem += DrawTab;

        _monitorTab  = new TabPage("MONITOR")  { BackColor = ColBg, ForeColor = ColText, UseVisualStyleBackColor = false };
        _settingsTab = new TabPage("SETTINGS") { BackColor = ColBg, ForeColor = ColText, UseVisualStyleBackColor = false };

        _tabs.TabPages.Add(_monitorTab);
        _tabs.TabPages.Add(_settingsTab);

        Controls.Add(_tabs);
    }

    private void DrawTab(object? sender, DrawItemEventArgs e)
    {
        var tab   = _tabs.TabPages[e.Index];
        bool sel  = e.Index == _tabs.SelectedIndex;
        var rect  = e.Bounds;

        using var bg = new SolidBrush(sel ? ColCard : ColPanel);
        e.Graphics.FillRectangle(bg, rect);

        var textColor = sel ? ColCyan : ColMuted;
        using var tf = new SolidBrush(textColor);
        using var font = new Font("Segoe UI", 8.5f, sel ? FontStyle.Bold : FontStyle.Regular);

        var sf = new StringFormat
        {
            Alignment     = StringAlignment.Center,
            LineAlignment = StringAlignment.Center
        };
        e.Graphics.DrawString(tab.Text, font, tf, rect, sf);

        if (sel)
        {
            using var accentPen = new Pen(ColCyan, 2);
            e.Graphics.DrawLine(accentPen, rect.Left, rect.Bottom - 1, rect.Right - 1, rect.Bottom - 1);
        }
    }

    // ─── Monitor tab ──────────────────────────────────────────────────────────

    private void BuildMonitorTab()
    {
        int y = 10;
        const int cardW = 400;

        // CPU Card
        var cpuCard = MakeCard(10, y, cardW, 110);
        _monitorTab.Controls.Add(cpuCard);

        _cpuNameLabel = MakeLabel("CPU", 10, 8, cardW - 20, 16, ColMuted, 8f);
        cpuCard.Controls.Add(_cpuNameLabel);

        // Temp row
        cpuCard.Controls.Add(MakeLabel("Temp", 10, 30, 50, 14, ColMuted, 8f));
        (_cpuTempTrack, _cpuTempFill) = MakeBar(65, 30, cardW - 110, 12, cpuCard);
        _cpuTempLabel = MakeLabel("--°C", cardW - 42, 28, 40, 14, ColText, 8.5f, ContentAlignment.MiddleRight);
        cpuCard.Controls.Add(_cpuTempLabel);

        // Load row
        cpuCard.Controls.Add(MakeLabel("Load", 10, 52, 50, 14, ColMuted, 8f));
        (_cpuLoadTrack, _cpuLoadFill) = MakeBar(65, 52, cardW - 110, 12, cpuCard);
        _cpuLoadLabel = MakeLabel("--%", cardW - 42, 50, 40, 14, ColText, 8.5f, ContentAlignment.MiddleRight);
        cpuCard.Controls.Add(_cpuLoadLabel);

        // Power row
        _cpuPowerLabel = MakeLabel("Power: --W", 10, 76, cardW - 20, 16, ColMuted, 8f);
        cpuCard.Controls.Add(_cpuPowerLabel);

        y += 120;

        // GPU Card
        var gpuCard = MakeCard(10, y, cardW, 110);
        _monitorTab.Controls.Add(gpuCard);

        _gpuNameLabel = MakeLabel("GPU", 10, 8, cardW - 20, 16, ColMuted, 8f);
        gpuCard.Controls.Add(_gpuNameLabel);

        gpuCard.Controls.Add(MakeLabel("Temp", 10, 30, 50, 14, ColMuted, 8f));
        (_gpuTempTrack, _gpuTempFill) = MakeBar(65, 30, cardW - 110, 12, gpuCard);
        _gpuTempLabel = MakeLabel("--°C", cardW - 42, 28, 40, 14, ColText, 8.5f, ContentAlignment.MiddleRight);
        gpuCard.Controls.Add(_gpuTempLabel);

        gpuCard.Controls.Add(MakeLabel("Load", 10, 52, 50, 14, ColMuted, 8f));
        (_gpuLoadTrack, _gpuLoadFill) = MakeBar(65, 52, cardW - 110, 12, gpuCard);
        _gpuLoadLabel = MakeLabel("--%", cardW - 42, 50, 40, 14, ColText, 8.5f, ContentAlignment.MiddleRight);
        gpuCard.Controls.Add(_gpuLoadLabel);

        _gpuVramLabel = MakeLabel("VRAM: -- / -- MB", 10, 76, cardW - 20, 16, ColMuted, 8f);
        gpuCard.Controls.Add(_gpuVramLabel);

        y += 120;

        // RAM Card
        var ramCard = MakeCard(10, y, cardW, 70);
        _monitorTab.Controls.Add(ramCard);

        ramCard.Controls.Add(MakeLabel("RAM", 10, 8, 50, 14, ColMuted, 8f));
        (_ramLoadTrack, _ramLoadFill) = MakeBar(65, 8, cardW - 110, 12, ramCard);
        _ramLoadLabel = MakeLabel("--%", cardW - 42, 6, 40, 14, ColText, 8.5f, ContentAlignment.MiddleRight);
        ramCard.Controls.Add(_ramLoadLabel);

        _ramDetailLabel = MakeLabel("-- GB / -- GB", 10, 30, cardW - 20, 16, ColMuted, 8f);
        ramCard.Controls.Add(_ramDetailLabel);

        y += 80;

        // Last-update label
        _lastUpdateLabel = MakeLabel("Last update: --", 10, y, cardW, 14, ColMuted, 7.5f);
        _monitorTab.Controls.Add(_lastUpdateLabel);
    }

    // ─── Settings tab ─────────────────────────────────────────────────────────

    private void BuildSettingsTab()
    {
        int y = 12;

        // Connection type radios
        var connLabel = MakeLabel("CONNECTION", 12, y, 200, 16, ColMuted, 8f);
        connLabel.Font = new Font("Segoe UI", 8f, FontStyle.Bold);
        _settingsTab.Controls.Add(connLabel);
        y += 22;

        _radioWifi = new RadioButton
        {
            Text      = "WiFi",
            Location  = new Point(12, y),
            Size      = new Size(80, 20),
            ForeColor = ColText,
            BackColor = Color.Transparent,
            Checked   = true,
            Font      = new Font("Segoe UI", 9f)
        };
        _radioSerial = new RadioButton
        {
            Text      = "USB Serial",
            Location  = new Point(100, y),
            Size      = new Size(100, 20),
            ForeColor = ColText,
            BackColor = Color.Transparent,
            Font      = new Font("Segoe UI", 9f)
        };
        _settingsTab.Controls.Add(_radioWifi);
        _settingsTab.Controls.Add(_radioSerial);
        _radioWifi.CheckedChanged   += (_, _) => UpdateConnectionGroupVisibility();
        _radioSerial.CheckedChanged += (_, _) => UpdateConnectionGroupVisibility();
        y += 30;

        // WiFi Group
        _wifiGroup = new GroupBox
        {
            Text      = "WiFi Settings",
            Location  = new Point(10, y),
            Size      = new Size(408, 90),
            ForeColor = ColMuted,
            BackColor = ColCard,
            Font      = new Font("Segoe UI", 8.5f)
        };
        _settingsTab.Controls.Add(_wifiGroup);

        _wifiGroup.Controls.Add(MakeLabel("Host:", 10, 22, 40, 16, ColMuted, 8.5f));
        _hostBox = new TextBox
        {
            Location  = new Point(55, 20),
            Size      = new Size(200, 22),
            BackColor = ColPanel,
            ForeColor = ColText,
            BorderStyle = BorderStyle.FixedSingle,
            Font      = new Font("Segoe UI", 9f),
            Text      = _settings.WifiHost
        };
        _wifiGroup.Controls.Add(_hostBox);

        _wifiGroup.Controls.Add(MakeLabel("Port:", 265, 22, 36, 16, ColMuted, 8.5f));
        _portNum = new NumericUpDown
        {
            Location  = new Point(304, 19),
            Size      = new Size(60, 22),
            Minimum   = 1,
            Maximum   = 65535,
            Value     = _settings.WifiPort,
            BackColor = ColPanel,
            ForeColor = ColText,
            BorderStyle = BorderStyle.FixedSingle,
            Font      = new Font("Segoe UI", 9f)
        };
        _wifiGroup.Controls.Add(_portNum);

        _wifiTestBtn = MakeButton("Test", 10, 54, 80, 26, ColCyan, Color.Black);
        _wifiTestBtn.Click += async (_, _) => await TestConnectionAsync();
        _wifiGroup.Controls.Add(_wifiTestBtn);

        y += 100;

        // Serial Group
        _serialGroup = new GroupBox
        {
            Text      = "Serial Settings",
            Location  = new Point(10, y),
            Size      = new Size(408, 90),
            ForeColor = ColMuted,
            BackColor = ColCard,
            Font      = new Font("Segoe UI", 8.5f),
            Visible   = false
        };
        _settingsTab.Controls.Add(_serialGroup);

        _serialGroup.Controls.Add(MakeLabel("Port:", 10, 22, 36, 16, ColMuted, 8.5f));
        _portCombo = new ComboBox
        {
            Location         = new Point(52, 19),
            Size             = new Size(160, 22),
            BackColor        = ColPanel,
            ForeColor        = ColText,
            FlatStyle        = FlatStyle.Flat,
            DropDownStyle    = ComboBoxStyle.DropDownList,
            Font             = new Font("Segoe UI", 9f)
        };
        _serialGroup.Controls.Add(_portCombo);

        _refreshPortsBtn = MakeButton("Refresh", 220, 19, 72, 24, ColPanel, ColMuted);
        _refreshPortsBtn.Click += (_, _) => RefreshSerialPorts();
        _serialGroup.Controls.Add(_refreshPortsBtn);

        _serialTestBtn = MakeButton("Test", 10, 54, 80, 26, ColCyan, Color.Black);
        _serialTestBtn.Click += async (_, _) => await TestConnectionAsync();
        _serialGroup.Controls.Add(_serialTestBtn);

        y += 100;

        // Update interval
        _settingsTab.Controls.Add(MakeLabel("Update interval (seconds):", 12, y + 3, 190, 16, ColMuted, 8.5f));
        _intervalNum = new NumericUpDown
        {
            Location    = new Point(210, y),
            Size        = new Size(60, 24),
            Minimum     = 1,
            Maximum     = 60,
            Value       = Math.Clamp(_settings.UpdateIntervalSec, 1, 60),
            BackColor   = ColPanel,
            ForeColor   = ColText,
            BorderStyle = BorderStyle.FixedSingle,
            Font        = new Font("Segoe UI", 9f)
        };
        _settingsTab.Controls.Add(_intervalNum);
        y += 36;

        // Minimize to tray
        _trayCheck = new CheckBox
        {
            Text      = "Minimize to system tray on close",
            Location  = new Point(12, y),
            Size      = new Size(250, 22),
            ForeColor = ColText,
            BackColor = Color.Transparent,
            Checked   = _settings.MinimizeToTray,
            Font      = new Font("Segoe UI", 9f)
        };
        _settingsTab.Controls.Add(_trayCheck);
        y += 36;

        // Save button
        _saveBtn = MakeButton("Save Settings", 12, y, 120, 32, ColCyan, Color.Black);
        _saveBtn.Font  = new Font("Segoe UI", 9.5f, FontStyle.Bold);
        _saveBtn.Click += OnSaveSettings;
        _settingsTab.Controls.Add(_saveBtn);

        // Populate serial ports when settings tab is selected
        _tabs.Selected += (_, e) =>
        {
            if (e.TabPage == _settingsTab)
                RefreshSerialPorts();
        };
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Tray
    // ══════════════════════════════════════════════════════════════════════════

    private void BuildTray()
    {
        _trayMenu = new ContextMenuStrip();
        _trayMenu.BackColor = ColPanel;
        _trayMenu.ForeColor = ColText;
        _trayMenu.Renderer  = new DarkMenuRenderer();

        var showItem = new ToolStripMenuItem("Show / Hide");
        showItem.Font      = new Font("Segoe UI", 9f);
        showItem.ForeColor = ColText;
        showItem.Click    += (_, _) => ToggleVisibility();

        _pauseItem = new ToolStripMenuItem("⏸  Pause");
        _pauseItem.Font      = new Font("Segoe UI", 9f);
        _pauseItem.ForeColor = ColAmber;
        _pauseItem.Click    += (_, _) => TogglePause();

        var exitItem = new ToolStripMenuItem("Exit");
        exitItem.Font      = new Font("Segoe UI", 9f);
        exitItem.ForeColor = ColRed;
        exitItem.Click    += (_, _) => ExitApp();

        _trayMenu.Items.Add(showItem);
        _trayMenu.Items.Add(_pauseItem);
        _trayMenu.Items.Add(new ToolStripSeparator());
        _trayMenu.Items.Add(exitItem);

        _trayIcon = new NotifyIcon
        {
            Text             = "PCHUB Agent",
            Icon             = CreateTrayIcon(ColCyan),
            ContextMenuStrip = _trayMenu,
            Visible          = true
        };

        _trayIcon.DoubleClick += (_, _) => ToggleVisibility();
    }

    private void SwapTrayIcon(Color color)
    {
        var old = _trayIcon.Icon;
        _trayIcon.Icon = CreateTrayIcon(color);
        old?.Dispose();
    }

    private static Icon CreateTrayIcon(Color color)
    {
        using var bmp = new Bitmap(16, 16);
        using var g   = Graphics.FromImage(bmp);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Color.Transparent);
        using var brush = new SolidBrush(color);
        g.FillEllipse(brush, 1, 1, 13, 13);
        return Icon.FromHandle(bmp.GetHicon());
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Timer / data loop
    // ══════════════════════════════════════════════════════════════════════════

    private void WireTimer()
    {
        _timer.Interval = _settings.UpdateIntervalSec * 1000;
        _timer.Tick    += OnTimerTick;
    }

    private async void OnTimerTick(object? sender, EventArgs e)
    {
        _timer.Stop(); // Prevent re-entry

        try
        {
            if (_paused)
                return;

            var metrics = _monitor.Read();
            Text = $"PCHUB Agent  |  CPU: {metrics.CpuName}  |  GPU: {metrics.GpuName}";
            await _client.SendAsync(metrics);
            UpdateMonitorUi(metrics);
            UpdateHeaderUi();
        }
        catch (Exception ex)
        {
            _statusLabel.Text      = $"Error: {ex.Message}";
            _statusDot.BackColor   = ColRed;
        }
        finally
        {
            _timer.Interval = _settings.UpdateIntervalSec * 1000;
            _timer.Start();
        }
    }

    private void TogglePause()
    {
        _paused = !_paused;

        if (_paused)
        {
            _pauseBtn.Text           = "▶  Resume";
            _pauseBtn.ForeColor      = ColGreen;
            _pauseItem.Text          = "▶  Resume";
            _statusLabel.Text        = "Paused";
            _statusLabel.ForeColor   = ColMuted;
            _statusDot.BackColor     = ColAmber;
            _trayIcon.Text           = "PCHUB Agent (Paused)";
            SwapTrayIcon(ColAmber);
        }
        else
        {
            _pauseBtn.Text           = "⏸  Pause";
            _pauseBtn.ForeColor      = ColMuted;
            _pauseItem.Text          = "⏸  Pause";
            _trayIcon.Text           = "PCHUB Agent";
            SwapTrayIcon(ColCyan);
            UpdateHeaderUi();
        }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // UI update helpers
    // ══════════════════════════════════════════════════════════════════════════

    private void UpdateMonitorUi(PcMetrics m)
    {
        // CPU
        if (!string.IsNullOrWhiteSpace(m.CpuName))
            _cpuNameLabel.Text = m.CpuName;

        SetBar(_cpuTempFill, _cpuTempTrack, m.CpuTemp / 100f);
        _cpuTempLabel.Text  = $"{m.CpuTemp:F0}°C";
        _cpuTempFill.BackColor = BarColor(m.CpuTemp / 100f);

        SetBar(_cpuLoadFill, _cpuLoadTrack, m.CpuLoad / 100f);
        _cpuLoadLabel.Text  = $"{m.CpuLoad:F0}%";
        _cpuLoadFill.BackColor = BarColor(m.CpuLoad / 100f);

        _cpuPowerLabel.Text = $"Power: {m.CpuPower:F1} W";

        // GPU
        if (!string.IsNullOrWhiteSpace(m.GpuName))
            _gpuNameLabel.Text = m.GpuName;

        SetBar(_gpuTempFill, _gpuTempTrack, m.GpuTemp / 100f);
        _gpuTempLabel.Text  = $"{m.GpuTemp:F0}°C";
        _gpuTempFill.BackColor = BarColor(m.GpuTemp / 100f);

        SetBar(_gpuLoadFill, _gpuLoadTrack, m.GpuLoad / 100f);
        _gpuLoadLabel.Text  = $"{m.GpuLoad:F0}%";
        _gpuLoadFill.BackColor = BarColor(m.GpuLoad / 100f);

        if (m.GpuVramTotalMb > 0)
            _gpuVramLabel.Text = $"VRAM: {m.GpuVramUsedMb:N0} / {m.GpuVramTotalMb:N0} MB";
        else
            _gpuVramLabel.Text = "VRAM: n/a";

        // RAM
        float ramPct = m.RamTotalMb > 0 ? (float)m.RamUsedMb / m.RamTotalMb : 0f;
        SetBar(_ramLoadFill, _ramLoadTrack, ramPct);
        _ramLoadLabel.Text  = $"{ramPct * 100f:F0}%";
        _ramLoadFill.BackColor = BarColor(ramPct);
        _ramDetailLabel.Text = $"{m.RamUsedMb / 1024f:F1} GB / {m.RamTotalMb / 1024f:F1} GB";

        _lastUpdateLabel.Text = $"Last update: {DateTime.Now:HH:mm:ss}";
    }

    private void UpdateHeaderUi()
    {
        if (_paused) return;
        _statusLabel.Text      = _client.StatusMessage;
        _statusDot.BackColor   = _client.IsConnected ? ColGreen
                               : _client.IsPaused    ? ColAmber
                               :                       ColRed;
        _statusLabel.ForeColor = _client.IsConnected ? ColText : ColMuted;
    }

    private static void SetBar(Panel fill, Panel track, float fraction)
    {
        fraction = Math.Clamp(fraction, 0f, 1f);
        int w = (int)(track.Width * fraction);
        fill.Width = Math.Max(w, 0);
    }

    private static Color BarColor(float fraction)
    {
        if (fraction >= 0.8f) return ColRed;
        if (fraction >= 0.6f) return ColAmber;
        return ColCyan;
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Settings helpers
    // ══════════════════════════════════════════════════════════════════════════

    private void ApplySettingsToUi()
    {
        _radioWifi.Checked   = _settings.ConnectionType.Equals("wifi",   StringComparison.OrdinalIgnoreCase);
        _radioSerial.Checked = _settings.ConnectionType.Equals("serial", StringComparison.OrdinalIgnoreCase);
        _hostBox.Text        = _settings.WifiHost;
        _portNum.Value       = Math.Clamp(_settings.WifiPort, 1, 65535);
        _intervalNum.Value   = Math.Clamp(_settings.UpdateIntervalSec, 1, 60);
        _trayCheck.Checked   = _settings.MinimizeToTray;
        UpdateConnectionGroupVisibility();
    }

    private void UpdateConnectionGroupVisibility()
    {
        _wifiGroup.Visible   = _radioWifi.Checked;
        _serialGroup.Visible = _radioSerial.Checked;
    }

    private void RefreshSerialPorts()
    {
        string current = _portCombo.Text;
        _portCombo.Items.Clear();

        foreach (var p in SerialPort.GetPortNames())
            _portCombo.Items.Add(p);

        if (!string.IsNullOrWhiteSpace(current) && _portCombo.Items.Contains(current))
            _portCombo.SelectedItem = current;
        else if (!string.IsNullOrWhiteSpace(_settings.SerialPort) && _portCombo.Items.Contains(_settings.SerialPort))
            _portCombo.SelectedItem = _settings.SerialPort;
        else if (_portCombo.Items.Count > 0)
            _portCombo.SelectedIndex = 0;
    }

    private void OnSaveSettings(object? sender, EventArgs e)
    {
        _settings.ConnectionType    = _radioWifi.Checked ? "wifi" : "serial";
        _settings.WifiHost          = _hostBox.Text.Trim();
        _settings.WifiPort          = (int)_portNum.Value;
        _settings.SerialPort        = _portCombo.Text;
        _settings.UpdateIntervalSec = (int)_intervalNum.Value;
        _settings.MinimizeToTray    = _trayCheck.Checked;
        _settings.Save();

        // Rebuild client with new settings
        _client.Dispose();
        _client = new PcHubClient(_settings);

        // Reset timer interval
        _timer.Stop();
        _timer.Interval = _settings.UpdateIntervalSec * 1000;
        _timer.Start();

        // Visual feedback
        _saveBtn.Text      = "Saved!";
        _saveBtn.BackColor = ColGreen;
        var restoreTimer = new System.Windows.Forms.Timer { Interval = 1500 };
        restoreTimer.Tick += (_, _) =>
        {
            _saveBtn.Text      = "Save Settings";
            _saveBtn.BackColor = ColCyan;
            restoreTimer.Dispose();
        };
        restoreTimer.Start();
    }

    private async Task TestConnectionAsync()
    {
        // Temporarily apply UI values to a test-settings snapshot
        var testSettings = new AppSettings
        {
            ConnectionType    = _radioWifi.Checked ? "wifi" : "serial",
            WifiHost          = _hostBox.Text.Trim(),
            WifiPort          = (int)_portNum.Value,
            SerialPort        = _portCombo.Text,
            UpdateIntervalSec = (int)_intervalNum.Value,
            MinimizeToTray    = _trayCheck.Checked
        };

        using var testClient = new PcHubClient(testSettings);
        string result = await testClient.TestConnectionAsync();

        MessageBox.Show(result, "Connection Test", MessageBoxButtons.OK,
            testClient.IsConnected ? MessageBoxIcon.Information : MessageBoxIcon.Warning);
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Window / tray behaviour
    // ══════════════════════════════════════════════════════════════════════════

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (e.CloseReason == CloseReason.UserClosing && _settings.MinimizeToTray && !_suppressClose)
        {
            e.Cancel = true;
            Hide();
            _trayIcon.ShowBalloonTip(2000, "PCHUB Agent", "Running in background.", ToolTipIcon.Info);
            return;
        }

        base.OnFormClosing(e);
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _timer.Stop();
        _timer.Dispose();
        _monitor.Dispose();
        _client.Dispose();
        _trayIcon.Visible = false;
        _trayIcon.Dispose();
        base.OnFormClosed(e);
    }

    private void ToggleVisibility()
    {
        if (Visible)
            Hide();
        else
        {
            Show();
            WindowState = FormWindowState.Normal;
            Activate();
        }
    }

    private void ExitApp()
    {
        _suppressClose = true;
        Close();
    }

    // ══════════════════════════════════════════════════════════════════════════
    // UI factory helpers
    // ══════════════════════════════════════════════════════════════════════════

    private static Panel MakeCard(int x, int y, int w, int h)
    {
        return new Panel
        {
            Location  = new Point(x, y),
            Size      = new Size(w, h),
            BackColor = ColCard,
            Padding   = new Padding(4)
        };
    }

    private static Label MakeLabel(string text, int x, int y, int w, int h,
        Color? fore = null, float fontSize = 8.5f,
        ContentAlignment align = ContentAlignment.MiddleLeft)
    {
        return new Label
        {
            Text      = text,
            Location  = new Point(x, y),
            Size      = new Size(w, h),
            ForeColor = fore ?? ColText,
            BackColor = Color.Transparent,
            Font      = new Font("Segoe UI", fontSize),
            TextAlign = align,
            AutoSize  = false
        };
    }

    private static (Panel track, Panel fill) MakeBar(int x, int y, int w, int h, Control parent)
    {
        var track = new Panel
        {
            Location  = new Point(x, y + (h < 14 ? 1 : 0)),
            Size      = new Size(w, h),
            BackColor = ColPanel
        };

        var fill = new Panel
        {
            Location  = new Point(0, 0),
            Size      = new Size(0, h),
            BackColor = ColCyan
        };

        track.Controls.Add(fill);
        parent.Controls.Add(track);
        return (track, fill);
    }

    private static Button MakeButton(string text, int x, int y, int w, int h,
        Color back, Color fore)
    {
        return new Button
        {
            Text      = text,
            Location  = new Point(x, y),
            Size      = new Size(w, h),
            BackColor = back,
            ForeColor = fore,
            FlatStyle = FlatStyle.Flat,
            Font      = new Font("Segoe UI", 8.5f),
            FlatAppearance = { BorderSize = 0 }
        };
    }

    private static void MakeCircle(Panel p)
    {
        // Use Paint event to draw a circle instead of a square
        p.Paint += (_, e) =>
        {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            using var b = new SolidBrush(p.BackColor);
            e.Graphics.Clear(p.Parent?.BackColor ?? ColPanel);
            e.Graphics.FillEllipse(b, 0, 0, p.Width - 1, p.Height - 1);
        };
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Dark context-menu renderer
    // ══════════════════════════════════════════════════════════════════════════

    private class DarkMenuRenderer : ToolStripProfessionalRenderer
    {
        public DarkMenuRenderer() : base(new DarkColorTable()) { }

        protected override void OnRenderMenuItemBackground(ToolStripItemRenderEventArgs e)
        {
            var item = e.Item;
            var g    = e.Graphics;
            var rect = new Rectangle(Point.Empty, item.Size);

            Color bg = item.Selected
                ? ColorTranslator.FromHtml("#2A2D4A")
                : ColorTranslator.FromHtml("#18192F");

            using var b = new SolidBrush(bg);
            g.FillRectangle(b, rect);
        }
    }

    private class DarkColorTable : ProfessionalColorTable
    {
        public override Color MenuBorder              => ColorTranslator.FromHtml("#2A2D4A");
        public override Color ToolStripDropDownBackground => ColorTranslator.FromHtml("#18192F");
        public override Color ImageMarginGradientBegin => ColorTranslator.FromHtml("#18192F");
        public override Color ImageMarginGradientMiddle => ColorTranslator.FromHtml("#18192F");
        public override Color ImageMarginGradientEnd   => ColorTranslator.FromHtml("#18192F");
        public override Color SeparatorDark            => ColorTranslator.FromHtml("#2A2D4A");
        public override Color SeparatorLight           => ColorTranslator.FromHtml("#2A2D4A");
    }
}
