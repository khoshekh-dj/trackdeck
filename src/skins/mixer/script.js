/********************************************************************
**  TrackDeck, https://github.com/khoshekh-dj/trackdeck
**
**  "Mixer" skin: an original look inspired by club mixers.
**  Includes the DSEG7 font (SIL Open Font License 1.1, DSEG-LICENSE.txt). This skin package including all images, cascading
**  style sheets, UI forms, and JavaScript files is released under
**  Attribution-ShareAlike Unported License 3.0 (CC-BY-SA 3.0):
**  http://creativecommons.org/licenses/by-sa/3.0/
**  Credits for the skins TrackDeck's skins build on are in README.md.
**
*********************************************************************/

function Main()
{
    try {
        // segment-display digits for the BPM and time readouts
        addApplicationFont("DSEG7Classic-Bold.ttf");
        this.duration = 0;
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

        if (!Settings.value("MixerSkin/Splitter")) {
            Settings.setValue("MixerSkin/Splitter", [420, 240]);
        }
        if (!Settings.value("MixerSkin/SplitterFullScreen")) {
            Settings.setValue("MixerSkin/SplitterFullScreen", [999999, 0]);
        }
        Ui.splitter["splitterMoved(int, int)"].connect(this, "on_splitterMoved");

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
    Ui.splitter.setSizes(Settings.value("MixerSkin/Splitter"));
    Ui.menuButton["clicked()"].connect(this, "showMenu");
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
        Ui.bpmValue.text = "---.-";
        Ui.bpmCaption.text = "BPM";
        Ui.keyValue.text = "--";
        Ui.timeValue.text = "-0:00";
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
    var detected = bpm.indexOf("*") >= 0;
    bpm = bpm.replace("*", "");
    if (bpm != "" && !isNaN(parseFloat(bpm))) {
        bpm = parseFloat(bpm).toFixed(1);
    } else {
        bpm = "---.-";
    }
    Ui.bpmValue.text = bpm;
    Ui.bpmCaption.text = detected ? "BPM \u00B7 DETECTED" : "BPM";

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
    this.showRemaining(0);
}

Main.prototype.showRemaining = function(msec)
{
    var left = Math.max(0, Math.round(this.duration - msec / 1000));
    var m = Math.floor(left / 60);
    var sec = left % 60;
    var text = "-" + m + ":" + (sec < 10 ? "0" : "") + sec;
    if (Ui.timeValue.text != text) {
        Ui.timeValue.text = text;
    }
}

Main.prototype.on_tick = function(msec)
{
    this.showRemaining(msec);
}

Main.prototype.on_stateChanged = function(state)
{
    this.playing = (state == N.PlaybackPlaying);
    this.updatePlayButton();
}

// the play pad lights green while playing, in the accent colour otherwise
Main.prototype.updatePlayButton = function()
{
    if (this.playing) {
        Ui.playButton.styleSheet = "qproperty-icon: url(pause.svg); border-color: #3DDC84; background: rgba(61, 220, 132, 40);";
    } else {
        Ui.playButton.styleSheet = "qproperty-icon: url(play.svg);";
    }
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
        Settings.setValue("MixerSkin/SplitterFullScreen", Ui.splitter.sizes());
    } else {
        Settings.setValue("MixerSkin/Splitter", Ui.splitter.sizes());
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
        Ui.splitter.setSizes(Settings.value("MixerSkin/SplitterFullScreen"));
    } else {
        Ui.splitter.setSizes(Settings.value("MixerSkin/Splitter"));
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

    // the skin's accent is blue; swap in the chosen accent colour
    var accent = String(Settings.value("AccentColor") || "#2FB6FF");
    if (/^#[0-9A-Fa-f]{6}$/.test(accent) && accent.toUpperCase() != "#2FB6FF") {
        var rgb = hexToRgb(accent);
        sheet = replaceAll(sheet, "#2FB6FF", rgbToHex(rgb));
        sheet = replaceAll(sheet, "rgba(47, 182, 255,",
                           "rgba(" + rgb[0] + ", " + rgb[1] + ", " + rgb[2] + ",");
    }
    // the skin's styles are applied before this script loads the font, so
    // the segment font is added here, where it's known
    sheet += "\n#bpmValue, #timeValue { font-family: \"DSEG7 Classic\"; }";
    Ui.mainWindow.styleSheet = sheet;
}

Main.prototype.on_settingChanged = function(key, value)
{
    if (key == "AccentColor") {
        this.applyStyle();
    }
}
