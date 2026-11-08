/********************************************************************
**  Nulloy Music Player, http://nulloy.com
**  Copyright (C) 2010-2024 Sergey Vlasov <sergey@vlasov.me>
**
**  This program can be distributed under the terms of the GNU
**  General Public License version 3.0 as published by the Free
**  Software Foundation and appearing in the file LICENSE.GPL3
**  included in the packaging of this file.  Please review the
**  following information to ensure the GNU General Public License
**  version 3.0 requirements will be met:
**
**  http://www.gnu.org/licenses/gpl-3.0.html
**
*********************************************************************/

#ifndef N_PLAYER_H
#define N_PLAYER_H

#include <QSystemTrayIcon>
#include <QWidget>

#include "global.h"

class NLogDialog;
class NMainWindow;
class NPlaybackEngineInterface;
class NPlaylistWidget;
class NSpectrumWidget;
class NLevelMeter;
class NPlatterWidget;
class NWaveformSlider;
class NCoverWidget;
class NCoverReaderInterface;
class NVolumeSlider;
class NPreferencesDialog;
class NAboutDialog;
class NScriptEngine;
class NSettings;
class NTrackInfoReader;
class NTrackInfoWidget;
class QMenu;
class NActionManager;
class QString;
class QTimer;
class NTagReaderInterface;

class NPlayer : public QWidget
{
    Q_OBJECT

private:
    void rememberPlaylist(const QString &file); // current playlist file + recents
    NSettings *m_settings;
    NActionManager *m_actionManager;
    NScriptEngine *m_scriptEngine;
    NMainWindow *m_mainWindow;
    NCoverWidget *m_coverWidget;
    NCoverReaderInterface *m_coverReader;
    NWaveformSlider *m_waveformSlider;
    NSpectrumWidget *m_spectrumWidget;
    QList<NLevelMeter *> m_levelMeters;
    NPlatterWidget *m_platterWidget;
    QString m_keyNotation; // as last applied, to notice changes
    NPreferencesDialog *m_preferencesDialog;
    NAboutDialog *m_aboutDialog;
    NVolumeSlider *m_volumeSlider;
    NTrackInfoReader *m_trackInfoReader;
    NPlaybackEngineInterface *m_playbackEngine;
    NPlaylistWidget *m_playlistWidget;
    NTrackInfoWidget *m_trackInfoWidget;
    NLogDialog *m_logDialog;
    QSystemTrayIcon *m_systemTray;
    QTimer *m_trayClickTimer;
    QTimer *m_settingsSaveTimer;
    QTimer *m_writeDefaultPlaylistTimer;
    bool m_trayIconDoubleClickCheck;

    bool eventFilter(QObject *obj, QEvent *event);
    void writePlaylist(const QString &file, N::M3uExtention ext);

    void connectSignals();
    void loadCoverArt(const QString &file);
    void prepareBpmDetection(const QString &file);
    void applyVisualiserSettings();

    void loadDefaultPlaylist();
    void loadSettings();
    void applySettings();
    void saveSettings();
    void savePlaybackState();

public:
    NPlayer();
    ~NPlayer();
    NMainWindow *mainWindow();
    NPlaybackEngineInterface *playbackEngine();
    NPlaylistWidget *playlistWidget();
    NTagReaderInterface *tagReader();
    NCoverWidget *coverWidget();
    NSettings *settings();

private slots:
    void on_playbackEngine_mediaChanged(const QString &path, int);
    void on_playbackEngine_mediaFailed(const QString &, int);
    void on_playbackEngine_stateChanged(N::PlaybackState state);
    void on_playlist_addMoreRequested();

    void on_mainWindow_closed();
    void on_mainWindow_scrolled(int delta);
    void on_trayIcon_activated(QSystemTrayIcon::ActivationReason reason);
    void on_trayClickTimer_timeout();
    void trayIconCountClicks(int clicks);
    void handleBpmDetected(const QString &file, qreal bpm);

public slots:
    void quit();
    void playPause();
    void toggleWindowVisibility();
    void showAboutDialog();
    void showPreferencesDialog();
    void showTagEditor(const QStringList &files);
    void showOpenFileDialog();
    void showOpenDirDialog();
    void showSavePlaylistDialog(); // "Save Playlist As..."

    // playlists kept as .m3u8 files in a playlists folder >>
    QString playlistsFolder() const;
    QStringList savedPlaylists() const;   // files in the playlists folder
    QStringList recentPlaylists() const;  // most recent first, existing files only
    void showOpenPlaylistDialog();
    void openPlaylist(const QString &file); // replace or add, per Preferences
    void savePlaylist();                    // to the current playlist file, else Save As
    void addSelectionToPlaylist(const QString &file);
    void addSelectionToNewPlaylist();
    // <<
    void showToolTip(const QString &text);
    void showContextMenu(const QPoint &pos);
    void showPlaylistContextMenu(const QPoint &pos);
    void readMessage(const QString &str);
    bool revealInFileManager(const QString &file, QString *error) const;

    // For skin scripts: format track info (e.g. "%t", "%a", "{%M BPM}") for the
    // current media. Returns an empty string when nothing is loaded.
    QString trackInfo(const QString &format);
    // For skin scripts: path of a blurred, darkened copy of the current cover
    // (empty when there's no cover or the feature is off)
    QString ambientImage() const { return m_ambientImagePath; }

signals:
    void ambientImageChanged(const QString &path);
    // the playing track's details changed without a track change (e.g. its
    // BPM was just detected); skins refresh their track labels
    void trackInfoChanged();

private:
    NTrackInfoReader *m_skinTrackInfoReader;
    QImage m_currentCover;
    QString m_ambientImagePath;
    int m_ambientImageCounter = 0;
    void updateAmbientImage();
};

#endif
