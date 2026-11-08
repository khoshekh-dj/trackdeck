/********************************************************************
**  TrackDeck, https://github.com/khoshekh-dj/trackdeck
**
**  "Turntable" skin: an original look inspired by classic direct-drive
**  DJ turntables. This skin package including all images, cascading
**  style sheets, UI forms, and JavaScript files is released under
**  Attribution-ShareAlike Unported License 3.0 (CC-BY-SA 3.0):
**  http://creativecommons.org/licenses/by-sa/3.0/
**  Credits for the skins TrackDeck's skins build on are in README.md.
**
*********************************************************************/

function Main()
{
    try {
        this.duration = 0;
        this.bpm = 0;
        this.bpmDetected = false;
        this.lastMsec = 0;
        this.settingPitch = false;
        this.playing = false;
        Ui.playlistWidget.alternatingRowColors = true;

        Ui.titleWidget.enableDoubleClick();
        Ui.titleWidget.doubleClicked.connect(Ui.mainWindow.toggleMaximize);

        PlaybackEngine["stateChanged(N::PlaybackState)"].connect(this, "on_stateChanged");
        PlaybackEngine["mediaChanged(const QString &, int)"].connect(this, "on_mediaChanged");
        Player["trackInfoChanged()"].connect(this, "updateTrackLabels"); // e.g. BPM detected
        PlaybackEngine["tick(qint64)"].connect(this, "on_tick");
        Settings["valueChanged(QString,QVariant)"].connect(this, "on_settingChanged");

        Ui.mainWindow["newTitle(const QString &)"].connect(this, "setTitle");
        Ui.mainWindow["fullScreenEnabled(bool)"].connect(this, "on_fullScreenEnabled");
        Ui.mainWindow["showPlaybackControlsEnabled(bool)"].connect(this, "on_showPlaybackControlsEnabled");
        Ui.mainWindow["maximizeEnabled(bool)"].connect(this, "on_maximizeEnabled");
        Ui.mainWindow.resized.connect(this, "on_resized");

        if (!Settings.value("TurntableSkin/Splitter")) {
            Settings.setValue("TurntableSkin/Splitter", [360, 240]);
        }
        if (!Settings.value("TurntableSkin/SplitterFullScreen")) {
            Settings.setValue("TurntableSkin/SplitterFullScreen", [999999, 0]);
        }
        Ui.splitter["splitterMoved(int, int)"].connect(this, "on_splitterMoved");
        Ui.pitchSlider["valueChanged(int)"].connect(this, "on_pitchMoved");

        Ui.mainWindow.windowFlags = (Ui.mainWindow.windowFlags | Qt.FramelessWindowHint | Qt.WindowCloseButtonHint) ^ (Qt.WindowTitleHint | Qt.Dialog);

        if (Q_WS == "mac") {
            Ui.playlistWidget.setAttribute(Qt.WA_MacShowFocusRect, false);
            Ui.sizeGrip.setParent(Ui.borderWidget);
        } else {
            Ui.sizeGrip.hide();
            Ui.mainWindow.setSizeGripEnabled(true);
        }

        // window buttons go where the platform expects them
        if (Q_WS != "mac" && WS_WM_BUTTON_DIRECTION != "left") {
            Ui.titleWidget.layout().addWidget(Ui.minimizeButton);
            Ui.titleWidget.layout().addWidget(Ui.closeButton);
        }

        this.marginsBkp_ = Ui.borderWidget.layout().contentsMargins();

        if (WS_WM_TILING) {
            Ui.titleWidget.setVisible(false);
        }
    } catch (err) {
        print("QtScript: " + err);
    }
}

Main.prototype.afterShow = function()
{
    Ui.splitter.setSizes(Settings.value("TurntableSkin/Splitter"));
    Ui.menuButton["clicked()"].connect(this, "showMenu");
    this.showSpeed(PlaybackEngine.speed());
    this.updateTrackLabels();
    this.on_resized();

    // the skin's stylesheet, before accent/ambient changes are applied to it
    this.baseStyleSheet = Ui.mainWindow.styleSheet;
    this.applyStyle();
}

Main.prototype.showMenu = function()
{
    var menuPos = Ui.menuButton.pos;
    menuPos.y += Ui.menuButton.height;
    Player.showContextMenu(menuPos);
}

Main.prototype.on_mediaChanged = function(file, id)
{
    this.updateTrackLabels();
}

Main.prototype.updateTrackLabels = function()
{
    var title = Player.trackInfo("{%t|%F}");
    if (title == "") {
        Ui.trackTitleLabel.text = "TrackDeck";
        Ui.trackArtistLabel.text = "Drop music here to start";
        Ui.trackMetaLabel.text = "";
        this.bpm = 0;
        this.bpmDetected = false;
        this.showBpm();
        Ui.keyValue.text = "--";
        Ui.timeValue.text = "-0:00";
        this.lastMsec = 0;
        this.duration = 0;
        return;
    }

    var artist = Player.trackInfo("%a");
    var album = Player.trackInfo("%A");
    Ui.trackTitleLabel.text = title;
    Ui.trackTitleLabel.toolTip = title;
    Ui.trackArtistLabel.text = (artist != "" && album != "") ? artist + "  —  " + album : artist + album;
    Ui.trackArtistLabel.toolTip = Ui.trackArtistLabel.text;

    // BPM: a detected value ends with "*"; the segment font has no "*", so
    // the caption says it instead
    var bpm = String(Player.trackInfo("%M"));
    this.bpmDetected = bpm.indexOf("*") >= 0;
    bpm = parseFloat(bpm.replace("*", ""));
    this.bpm = isNaN(bpm) ? 0 : bpm;
    this.showBpm();

    var key = Player.trackInfo("%k");
    Ui.keyValue.text = key != "" ? key : "--";

    var meta = [];
    var formats = ["{%B kbps}", "{%s kHz}"];
    for (var i = 0; i < formats.length; ++i) {
        var value = Player.trackInfo(formats[i]);
        if (value != "") {
            meta.push(value);
        }
    }
    Ui.trackMetaLabel.text = meta.join("   \u00B7   ");

    this.duration = parseInt(Player.trackInfo("%D")) || 0;
    this.lastMsec = 0;
    this.showRemaining(0);
}

// the track's BPM at the current pitch, as a DJ would read it
Main.prototype.showBpm = function()
{
    var speed = PlaybackEngine.speed();
    if (this.bpm > 0) {
        Ui.bpmValue.text = (this.bpm * speed).toFixed(1);
    } else {
        Ui.bpmValue.text = "---";
    }
    Ui.bpmCaption.text = this.bpmDetected ? "BPM \u00B7 DETECTED" : "BPM";
}

// --- pitch -------------------------------------------------------------------

// fader units are hundredths of a percent: -800..800 is -8..+8 %
Main.prototype.on_pitchMoved = function(value)
{
    if (this.settingPitch) {
        return;
    }
    // a small detent at zero, so the fader finds the centre
    if (value != 0 && Math.abs(value) < 25) {
        Ui.pitchSlider.value = 0; // comes back here with 0
        return;
    }
    PlaybackEngine.setSpeed(1 + value / 10000);
    this.showSpeed(1 + value / 10000);
}

Main.prototype.showSpeed = function(speed)
{
    var units = Math.round((speed - 1) * 10000);
    if (Ui.pitchSlider.value != units) {
        this.settingPitch = true;
        Ui.pitchSlider.value = units;
        this.settingPitch = false;
    }
    var percent = (speed - 1) * 100;
    var text = (percent > 0.05 ? "+" : "") + (Math.abs(percent) < 0.05 ? "0.0" : percent.toFixed(1)) + "%";
    Ui.pitchValue.text = text;
    Ui.platterWidget.setPitch(percent);
    this.speed = speed;
    this.showBpm();
    this.showRemaining(this.lastMsec);
}

// the time left at the current speed, i.e. as long as it will really take
Main.prototype.showRemaining = function(msec)
{
    var speed = PlaybackEngine.speed();
    var left = Math.max(0, Math.round((this.duration - msec / 1000) / speed));
    var m = Math.floor(left / 60);
    var sec = left % 60;
    var text = "-" + m + ":" + (sec < 10 ? "0" : "") + sec;
    if (Ui.timeValue.text != text) {
        Ui.timeValue.text = text;
    }
}

Main.prototype.on_tick = function(msec)
{
    this.lastMsec = msec;
    // speed can also change from the keyboard shortcuts
    var speed = PlaybackEngine.speed();
    if (speed != this.speed) {
        this.showSpeed(speed);
    }
    this.showRemaining(msec);
}

Main.prototype.on_stateChanged = function(state)
{
    this.playing = (state == N.PlaybackPlaying);
    this.updatePlayButton();
}

// start/stop shows pause while playing; the platter's strobe and the
// target light come on
Main.prototype.updatePlayButton = function()
{
    if (this.playing) {
        Ui.playButton.styleSheet = "qproperty-icon: url(pause.svg);";
        Ui.targetLight.styleSheet = "background: qradialgradient(cx: 0.5, cy: 0.5, radius: 0.5, fx: 0.4, fy: 0.4, stop: 0 #FFFFFF, stop: 0.6 #F4F1E6, stop: 1 #B9B4A4); border-color: #6E7178;";
    } else {
        Ui.playButton.styleSheet = "qproperty-icon: url(play.svg);";
        Ui.targetLight.styleSheet = "";
    }
    Ui.platterWidget.setPlaying(this.playing);
}

Main.prototype.on_resized = function()
{
    if (Q_WS == "mac") {
        Ui.sizeGrip.move(Ui.sizeGrip.parentWidget().width - Ui.sizeGrip.width - 3,
                         Ui.sizeGrip.parentWidget().height - Ui.sizeGrip.height - 3);
        Ui.sizeGrip.raise();
    }
}

Main.prototype.on_splitterMoved = function(pos, index)
{
    if (Ui.mainWindow.isFullSceen()) {
        Settings.setValue("TurntableSkin/SplitterFullScreen", Ui.splitter.sizes());
    } else {
        Settings.setValue("TurntableSkin/Splitter", Ui.splitter.sizes());
    }
}

Main.prototype.setTitle = function(title)
{
    Ui.titleLabel.text = title;
    Ui.titleLabel.toolTip = title;
}

Main.prototype.on_fullScreenEnabled = function(enabled)
{
    if (enabled) {
        Ui.splitter.setSizes(Settings.value("TurntableSkin/SplitterFullScreen"));
    } else {
        Ui.splitter.setSizes(Settings.value("TurntableSkin/Splitter"));
    }

    if (Settings.value("ShowPlaybackControls")) {
        Ui.controlsContainer.setVisible(!enabled);
    }
    Ui.titleWidget.setVisible(!enabled);
    this.setBorderVisible(!enabled);
}

Main.prototype.on_showPlaybackControlsEnabled = function(enabled)
{
    if (!Ui.mainWindow.isFullSceen()) {
        Ui.controlsContainer.setVisible(enabled);
    }
}

Main.prototype.on_maximizeEnabled = function(enabled)
{
    this.setBorderVisible(!enabled);
}

Main.prototype.setBorderVisible = function(enabled)
{
    if (!enabled) {
        Ui.borderWidget.layout().setContentsMargins(0, 0, 0, 0);
    } else {
        Ui.borderWidget.layout().setContentsMargins(this.marginsBkp_);
    }
}

// --- accent colour & album-art background ----------------------------------

function hexToRgb(hex)
{
    hex = String(hex).replace("#", "");
    return [parseInt(hex.substr(0, 2), 16), parseInt(hex.substr(2, 2), 16), parseInt(hex.substr(4, 2), 16)];
}

function rgbToHex(rgb)
{
    var s = "#";
    for (var i = 0; i < 3; ++i) {
        var v = Math.max(0, Math.min(255, Math.round(rgb[i]))).toString(16).toUpperCase();
        s += (v.length < 2 ? "0" : "") + v;
    }
    return s;
}

// t > 0 mixes towards white, t < 0 towards black
function shade(rgb, t)
{
    var target = t > 0 ? 255 : 0;
    var k = Math.abs(t);
    return [rgb[0] + (target - rgb[0]) * k, rgb[1] + (target - rgb[1]) * k, rgb[2] + (target - rgb[2]) * k];
}

function replaceAll(text, from, to)
{
    return text.split(from).join(to);
}

Main.prototype.applyStyle = function()
{
    if (this.baseStyleSheet === undefined) {
        return;
    }
    var sheet = this.baseStyleSheet;

    // the skin's accent is red; swap in the chosen accent colour
    var accent = String(Settings.value("AccentColor") || "#D7262B");
    if (/^#[0-9A-Fa-f]{6}$/.test(accent) && accent.toUpperCase() != "#D7262B") {
        var rgb = hexToRgb(accent);
        sheet = replaceAll(sheet, "#D7262B", rgbToHex(rgb));
        sheet = replaceAll(sheet, "rgba(215, 38, 43,",
                           "rgba(" + rgb[0] + ", " + rgb[1] + ", " + rgb[2] + ",");
    }
    Ui.mainWindow.styleSheet = sheet;
}

Main.prototype.on_settingChanged = function(key, value)
{
    if (key == "AccentColor") {
        this.applyStyle();
    }
}
