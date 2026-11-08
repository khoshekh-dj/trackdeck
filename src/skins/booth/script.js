/********************************************************************
**  TrackDeck, https://github.com/khoshekh-dj/trackdeck
**
**  "Booth" skin: an original look inspired by club DJ booth gear.
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

        PlaybackEngine["tick(qint64)"].connect(this, "on_tick");

        SkinCommon.setup(this, {
            settings: "BoothSkin",
            splitter: [300, 260],
            accent: { base: "#FF9F1A" }
        });
    } catch (err) {
        print("QtScript: " + err);
    }
}

SkinCommon.install(Main);

// the skin's styles are applied before this script loads the font, so the
// segment font is added here, where it's known
Main.prototype.extraStyle = function()
{
    return "\n#bpmValue, #timeValue { font-family: \"DSEG7 Classic\"; }";
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

// the play ring glows green while playing, in the accent colour otherwise
Main.prototype.updatePlayButton = function()
{
    if (this.playing) {
        Ui.playButton.styleSheet = "qproperty-icon: url(pause.svg); border-color: #3DDC84;";
    } else {
        Ui.playButton.styleSheet = "qproperty-icon: url(play.svg);";
    }
}
