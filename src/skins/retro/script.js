/********************************************************************
**  TrackDeck, https://github.com/khoshekh-dj/trackdeck
**
**  "Retro" skin: an original late-90s desktop player look. This skin package including all images, cascading
**  style sheets, UI forms, and JavaScript files is released under
**  Attribution-ShareAlike Unported License 3.0 (CC-BY-SA 3.0):
**  http://creativecommons.org/licenses/by-sa/3.0/
**  Credits for the skins TrackDeck's skins build on are in README.md.
**
*********************************************************************/

function Main()
{
    try {
        SkinCommon.setup(this, {
            settings: "RetroSkin",
            splitter: [230, 300]
        });
    } catch (err) {
        print("QtScript: " + err);
    }
}

SkinCommon.install(Main);

Main.prototype.updateTrackLabels = function()
{
    var title = Player.trackInfo("{%t|%F}");
    if (title == "") {
        Ui.trackTitleLabel.text = "TrackDeck";
        Ui.trackArtistLabel.text = "Drop music here to start";
        Ui.trackMetaLabel.text = "";
        return;
    }

    var artist = Player.trackInfo("%a");
    var album = Player.trackInfo("%A");
    Ui.trackTitleLabel.text = title;
    Ui.trackTitleLabel.toolTip = title;
    Ui.trackArtistLabel.text = (artist != "" && album != "") ? artist + "  —  " + album : artist + album;
    Ui.trackArtistLabel.toolTip = Ui.trackArtistLabel.text;

    var meta = [];
    var formats = ["{%M BPM}", "{%B kbps}", "{%s kHz}"];
    for (var i = 0; i < formats.length; ++i) {
        var value = Player.trackInfo(formats[i]);
        if (value != "") {
            meta.push(value);
        }
    }
    Ui.trackMetaLabel.text = meta.join("  ");
}

Main.prototype.on_stateChanged = function(state)
{
    if (state == N.PlaybackPlaying)
        Ui.playButton.styleSheet = "qproperty-icon: url(pause.svg)";
    else
        Ui.playButton.styleSheet = "qproperty-icon: url(play.svg)";
}
