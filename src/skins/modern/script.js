/********************************************************************
**  TrackDeck, https://github.com/khoshekh-dj/trackdeck
**
**  "Modern" skin. This skin package including all images, cascading
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
            settings: "ModernSkin",
            splitter: [300, 260],
            accent: {
                base: "#3D8BFF",
                shades: {
                    "#5B9DFF": 0.18,  // hover
                    "#2F6FD6": -0.18, // pressed
                    "#6FA8FF": 0.30   // light text
                }
            },
            ambient: true
        });
    } catch (err) {
        print("QtScript: " + err);
    }
}

SkinCommon.install(Main);

// blurred album art behind a semi-transparent player
Main.prototype.extraStyle = function()
{
    if (!this.ambientImage) {
        return "";
    }
    // centred, unscaled background-image (plain QWidgets don't support a
    // stretched border-image); everything above it is made see-through
    // NB: written as "url" + "(" so the skin loader, which rewrites every
    // literal url( in skin files to url(skin:...), leaves this path alone
    return "\n#borderWidget { background: #0F1115 url" + "(\"" + this.ambientImage + "\") no-repeat center center; }"
         + "\n#splitter, #splitTop, #splitter::handle { background: transparent; }"
         + "\n#playlistWidget { background: rgba(15, 17, 21, 105); }"
         + "\n#coverWidget { background: rgba(23, 26, 33, 120); }"
         + "\n#waveformSlider { qproperty-background: rgba(18, 21, 27, 125); }";
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
