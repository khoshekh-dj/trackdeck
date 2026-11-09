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

        PlaybackEngine["tick(qint64)"].connect(this, "on_tick");

        SkinCommon.setup(this, {
            settings: "TurntableSkin",
            splitter: [360, 240],
            accent: { base: "#D7262B" }
        });
        Ui.pitchSlider["valueChanged(int)"].connect(this, "on_pitchMoved");
    } catch (err) {
        print("QtScript: " + err);
    }
}

SkinCommon.install(Main);

Main.prototype.afterShow = function()
{
    this.showSpeed(PlaybackEngine.speed());
    SkinCommon.methods.afterShow.call(this);
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
