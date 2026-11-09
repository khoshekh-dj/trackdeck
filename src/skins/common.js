/********************************************************************
**  TrackDeck, https://github.com/khoshekh-dj/trackdeck
**
**  Shared skin code, built into TrackDeck and run before a skin's own
**  script. TrackDeck's skins (Booth, Khoshekh, Mixer, Modern, Retro,
**  Turntable) take their window behaviour and accent colour handling
**  from here. Released under Attribution-ShareAlike Unported License
**  3.0 (CC-BY-SA 3.0): http://creativecommons.org/licenses/by-sa/3.0/
**
*********************************************************************/

var SkinCommon = {};

// --- colours -----------------------------------------------------------------

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

// Swaps a skin's own accent for the chosen accent colour. The skin's sheet
// uses accent.base as "#RRGGBB" and as "rgba(r, g, b,"; accent.shades maps
// other colours in the sheet to how much lighter (> 0) or darker (< 0)
// than the accent they are.
SkinCommon.applyAccent = function(sheet, accent)
{
    var chosen = String(Settings.value("AccentColor") || accent.base);
    if (!/^#[0-9A-Fa-f]{6}$/.test(chosen) || chosen.toUpperCase() == accent.base) {
        return sheet;
    }
    var rgb = hexToRgb(chosen);
    var base = hexToRgb(accent.base);
    sheet = replaceAll(sheet, accent.base, rgbToHex(rgb));
    for (var hex in accent.shades || {}) {
        sheet = replaceAll(sheet, hex, rgbToHex(shade(rgb, accent.shades[hex])));
    }
    return replaceAll(sheet, "rgba(" + base.join(", ") + ",", "rgba(" + rgb.join(", ") + ",");
};

// --- window ------------------------------------------------------------------

// Call from the skin's Main() constructor. options:
//   settings: prefix of the skin's own settings, e.g. "BoothSkin"
//   splitter: default [cover, playlist] sizes
//   accent:   { base: "#RRGGBB", shades: {...} } to follow the accent colour
//   ambient:  true to show the blurred cover art behind the player
SkinCommon.setup = function(skin, options)
{
    skin.options = options;

    Ui.titleWidget.enableDoubleClick();
    Ui.titleWidget.doubleClicked.connect(Ui.mainWindow.toggleMaximize);

    PlaybackEngine["stateChanged(N::PlaybackState)"].connect(skin, "on_stateChanged");
    PlaybackEngine["mediaChanged(const QString &, int)"].connect(skin, "on_mediaChanged");
    Player["trackInfoChanged()"].connect(skin, "updateTrackLabels"); // e.g. BPM detected
    if (options.accent) {
        Settings["valueChanged(QString,QVariant)"].connect(skin, "on_settingChanged");
    }
    if (options.ambient) {
        Player["ambientImageChanged(QString)"].connect(skin, "on_ambientImageChanged");
    }

    Ui.mainWindow["newTitle(const QString &)"].connect(skin, "setTitle");
    Ui.mainWindow["fullScreenEnabled(bool)"].connect(skin, "on_fullScreenEnabled");
    Ui.mainWindow["showPlaybackControlsEnabled(bool)"].connect(skin, "on_showPlaybackControlsEnabled");
    Ui.mainWindow["maximizeEnabled(bool)"].connect(skin, "on_maximizeEnabled");
    Ui.mainWindow.resized.connect(skin, "on_resized");

    if (!Settings.value(options.settings + "/Splitter")) {
        Settings.setValue(options.settings + "/Splitter", options.splitter);
    }
    if (!Settings.value(options.settings + "/SplitterFullScreen")) {
        Settings.setValue(options.settings + "/SplitterFullScreen", [999999, 0]);
    }
    Ui.splitter["splitterMoved(int, int)"].connect(skin, "on_splitterMoved");

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

    skin.marginsBkp_ = Ui.borderWidget.layout().contentsMargins();

    if (WS_WM_TILING) {
        Ui.titleWidget.setVisible(false);
    }
};

// Shared methods. SkinCommon.install(Main) adds them to a skin; a skin
// replaces one by defining its own after the install call, and can still
// call the shared one as SkinCommon.methods.name.call(this, ...).
SkinCommon.methods = {};

SkinCommon.methods.afterShow = function()
{
    Ui.splitter.setSizes(Settings.value(this.options.settings + "/Splitter"));
    Ui.menuButton["clicked()"].connect(this, "showMenu");
    this.updateTrackLabels();
    this.on_resized();

    // the skin's stylesheet, before accent/ambient changes are applied to it
    this.baseStyleSheet = Ui.mainWindow.styleSheet;
    if (this.options.ambient) {
        this.ambientImage = Player.ambientImage();
    }
    this.applyStyle();
};

SkinCommon.methods.applyStyle = function()
{
    if (this.baseStyleSheet === undefined) {
        return;
    }
    var sheet = this.baseStyleSheet;
    if (this.options.accent) {
        sheet = SkinCommon.applyAccent(sheet, this.options.accent);
    }
    Ui.mainWindow.styleSheet = sheet + this.extraStyle();
};

// rules added after the skin's own stylesheet
SkinCommon.methods.extraStyle = function()
{
    return "";
};

SkinCommon.methods.on_settingChanged = function(key, value)
{
    if (key == "AccentColor") {
        this.applyStyle();
    }
};

SkinCommon.methods.on_ambientImageChanged = function(path)
{
    this.ambientImage = path;
    this.applyStyle();
};

SkinCommon.methods.showMenu = function()
{
    var menuPos = Ui.menuButton.pos;
    menuPos.y += Ui.menuButton.height;
    Player.showContextMenu(menuPos);
};

SkinCommon.methods.on_mediaChanged = function(file, id)
{
    this.updateTrackLabels();
};

SkinCommon.methods.on_resized = function()
{
    if (Q_WS == "mac") {
        Ui.sizeGrip.move(Ui.sizeGrip.parentWidget().width - Ui.sizeGrip.width - 3,
                         Ui.sizeGrip.parentWidget().height - Ui.sizeGrip.height - 3);
        Ui.sizeGrip.raise();
    }
};

SkinCommon.methods.on_splitterMoved = function(pos, index)
{
    if (Ui.mainWindow.isFullSceen()) {
        Settings.setValue(this.options.settings + "/SplitterFullScreen", Ui.splitter.sizes());
    } else {
        Settings.setValue(this.options.settings + "/Splitter", Ui.splitter.sizes());
    }
};

SkinCommon.methods.setTitle = function(title)
{
    Ui.titleLabel.text = title;
    Ui.titleLabel.toolTip = title;
};

SkinCommon.methods.on_fullScreenEnabled = function(enabled)
{
    if (enabled) {
        Ui.splitter.setSizes(Settings.value(this.options.settings + "/SplitterFullScreen"));
    } else {
        Ui.splitter.setSizes(Settings.value(this.options.settings + "/Splitter"));
    }
    if (Settings.value("ShowPlaybackControls")) {
        Ui.controlsContainer.setVisible(!enabled);
    }
    Ui.titleWidget.setVisible(!enabled);
    this.setBorderVisible(!enabled);
};

SkinCommon.methods.on_showPlaybackControlsEnabled = function(enabled)
{
    if (!Ui.mainWindow.isFullSceen()) {
        Ui.controlsContainer.setVisible(enabled);
    }
};

SkinCommon.methods.on_maximizeEnabled = function(enabled)
{
    this.setBorderVisible(!enabled);
};

SkinCommon.methods.setBorderVisible = function(enabled)
{
    if (!enabled) {
        Ui.borderWidget.layout().setContentsMargins(0, 0, 0, 0);
    } else {
        Ui.borderWidget.layout().setContentsMargins(this.marginsBkp_);
    }
};

SkinCommon.install = function(Main)
{
    for (var name in SkinCommon.methods) {
        Main.prototype[name] = SkinCommon.methods[name];
    }
};
