/********************************************************************
**  TrackDeck, https://github.com/khoshekh-dj/trackdeck
**
**  "Khoshekh" skin. This skin package including all images, cascading
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
            settings: "KhoshekhSkin",
            splitter: [300, 260],
            accent: {
                // the navy backgrounds (#05070F, #0A1420, #0E2630, #12303A)
                // keep the night-sky look
                base: "#2F8F9A",
                shades: {
                    "#3FA9B5": 0.18,  // hover, wave outline
                    "#24707A": -0.18, // pressed
                    "#6CC3CC": 0.35,  // light text
                    "#1E5F6E": -0.35  // deep accent
                }
            },
            ambient: true
        });
    } catch (err) {
        print("QtScript: " + err);
    }
}

SkinCommon.install(Main);

// blurred album art behind a semi-transparent player, tinted navy
Main.prototype.extraStyle = function()
{
    if (!this.ambientImage) {
        return "";
    }
    // centred, unscaled background-image (plain QWidgets don't support a
    // stretched border-image); everything above it is made see-through
    // NB: written as "url" + "(" so the skin loader, which rewrites every
    // literal url( in skin files to url(skin:...), leaves this path alone
    return "\n#borderWidget { background: #05070F url" + "(\"" + this.ambientImage + "\") no-repeat center center; }"
         + "\n#splitter, #splitTop, #splitter::handle { background: transparent; }"
         + "\n#playlistWidget { background: rgba(2, 4, 10, 110); }"
         + "\n#coverWidget { background: rgba(10, 20, 32, 120); }"
         + "\n#waveformSlider { qproperty-background: rgba(10, 20, 32, 125); }";
}

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
    Ui.trackMetaLabel.text = meta.join("   ·   ");
}

Main.prototype.on_stateChanged = function(state)
{
    if (state == N.PlaybackPlaying)
        Ui.playButton.styleSheet = "qproperty-icon: url(pause.svg)";
    else
        Ui.playButton.styleSheet = "qproperty-icon: url(play.svg)";
}
