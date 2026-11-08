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

#include "player.h"

#include <QFile>
#include <QTimer>

#include <QDir>

#include <QPainter>

#include "aboutDialog.h"
#include "bpmCache.h"
#include "action.h"
#include "actionManager.h"
#include "common.h"
#include "coverWidget.h"
#include "i18nLoader.h"
#include "logDialog.h"
#include "mainWindow.h"
#include "playbackEngineInterface.h"
#include "playlistDataItem.h"
#include "playlistStorage.h"
#include "playlistWidget.h"
#include "playlistWidgetItem.h"
#include "pluginLoader.h"
#include "preferencesDialog.h"
#include "scriptEngine.h"
#include "settings.h"
#include "tagEditorDialog.h"
#include "trackInfoReader.h"
#include "trackInfoWidget.h"
#include "waveformBuilderInterface.h"
#ifndef _N_NO_UPDATE_CHECK_
#include "updateChecker.h"
#endif
#include "utils.h"
#include "volumeSlider.h"
#include "levelMeter.h"
#include "platterWidget.h"
#include "spectrumWidget.h"
#include "waveformSlider.h"

#ifndef _N_NO_SKINS_
#include "skinFileSystem.h"
#include "skinLoader.h"
#endif

#ifdef Q_OS_WIN
#include "w7TaskBar.h"
#include "winIcon.h"
#endif

#ifdef Q_OS_MAC
#include "macDock.h"
#include "macMediaKeys.h"
#endif

#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QCheckBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QMetaObject>
#include <QResizeEvent>
#include <QToolTip>

NPlayer::NPlayer()
{
    qsrand((uint)QTime::currentTime().msec());
    m_settings = NSettings::instance();

    QString styleName = m_settings->value("Style").toString();
    if (!styleName.isEmpty()) {
        QApplication::setStyle(styleName);
    }

    NI18NLoader::init();
    NPluginLoader::init();

    m_trackInfoReader = new NTrackInfoReader(tagReader(), this);
    m_skinTrackInfoReader = new NTrackInfoReader(tagReader(), this);

    m_coverReader = dynamic_cast<NCoverReaderInterface *>(NPluginLoader::getPlugin(N::CoverReader));

    m_playbackEngine = dynamic_cast<NPlaybackEngineInterface *>(
        NPluginLoader::getPlugin(N::PlaybackEngine));
    Q_ASSERT(m_playbackEngine);
    m_playbackEngine->setParent(this);

#ifndef _N_NO_SKINS_
    m_mainWindow = new NMainWindow(NSkinLoader::skinUiFormFile());
#else
    m_mainWindow = new NMainWindow();
#endif

    // loading skin script
    m_scriptEngine = new NScriptEngine(this);
#ifndef _N_NO_SKINS_
    QString scriptFileName(NSkinLoader::skinScriptFile());
#else
    QString scriptFileName(":skins/native/script.js");
#endif
    QFile scriptFile(scriptFileName);
    scriptFile.open(QIODevice::ReadOnly);
    m_scriptEngine->evaluate(scriptFile.readAll(), scriptFileName);
    scriptFile.close();
    QScriptValue skinProgram = m_scriptEngine->evaluate("Main").construct();

    m_aboutDialog = NULL;
    m_logDialog = new NLogDialog(m_mainWindow);
    m_preferencesDialog = new NPreferencesDialog(this, m_mainWindow);
    m_volumeSlider = m_mainWindow->findChild<NVolumeSlider *>("volumeSlider");
    m_coverWidget = m_mainWindow->findChild<NCoverWidget *>("coverWidget");

    m_playlistWidget = m_mainWindow->findChild<NPlaylistWidget *>("playlistWidget");
    if (QAbstractButton *repeatButton = m_mainWindow->findChild<QAbstractButton *>(
            "repeatButton")) {
        repeatButton->setChecked(m_playlistWidget->repeatMode());
    }
    // separate reader: playlist refreshes must not overwrite the duration and
    // position cached for the playing track (used by the waveform labels)
    m_playlistWidget->setTrackInfoReader(new NTrackInfoReader(tagReader(), this));

    m_trackInfoWidget = new NTrackInfoWidget();
    m_trackInfoWidget->setTrackInfoReader(m_trackInfoReader);
    connect(m_trackInfoWidget, SIGNAL(showToolTip(const QString &)), this,
            SLOT(showToolTip(const QString &)));
    QVBoxLayout *trackInfoLayout = new QVBoxLayout;
    trackInfoLayout->setContentsMargins(0, 0, 0, 0);
    trackInfoLayout->addWidget(m_trackInfoWidget);
    m_waveformSlider = m_mainWindow->findChild<NWaveformSlider *>("waveformSlider");
    m_waveformSlider->setLayout(trackInfoLayout);
    m_spectrumWidget = m_mainWindow->findChild<NSpectrumWidget *>("spectrumWidget"); // optional
    m_levelMeters = m_mainWindow->findChildren<NLevelMeter *>(); // optional
    m_platterWidget = m_mainWindow->findChild<NPlatterWidget *>("platterWidget"); // optional

#ifdef Q_OS_WIN
    NW7TaskBar::instance()->setWindow(m_mainWindow);
    NW7TaskBar::instance()->setEnabled(NSettings::instance()->value("TaskbarProgress").toBool());
    connect(m_playbackEngine, SIGNAL(positionChanged(qreal)), NW7TaskBar::instance(),
            SLOT(setProgress(qreal)));
#endif

#ifdef Q_OS_MAC
    NMacDock::instance()->registerClickHandler();
    connect(NMacDock::instance(), SIGNAL(clicked()), m_mainWindow, SLOT(show()));

    NMacMediaKeys *mediaKeys = NMacMediaKeys::instance();
    mediaKeys->registerHandlers();
    connect(mediaKeys, &NMacMediaKeys::playPauseRequested, this, &NPlayer::playPause);
    connect(mediaKeys, &NMacMediaKeys::playRequested, this, [this]() {
        if (m_playbackEngine->state() != N::PlaybackPlaying) {
            playPause();
        }
    });
    connect(mediaKeys, &NMacMediaKeys::pauseRequested, m_playbackEngine,
            &NPlaybackEngineInterface::pause);
    connect(mediaKeys, &NMacMediaKeys::stopRequested, m_playbackEngine,
            &NPlaybackEngineInterface::stop);
    connect(mediaKeys, &NMacMediaKeys::nextRequested, m_playlistWidget,
            &NPlaylistWidget::playNextItem);
    connect(mediaKeys, &NMacMediaKeys::prevRequested, m_playlistWidget,
            &NPlaylistWidget::playPrevItem);
#endif

    m_mainWindow->setTitle(QCoreApplication::applicationName() + " " +
                           QCoreApplication::applicationVersion());
    m_mainWindow->show();
    m_mainWindow->loadSettings();
    QResizeEvent e(m_mainWindow->size(), m_mainWindow->size());
    QCoreApplication::sendEvent(m_mainWindow, &e);
#ifndef _N_NO_UPDATE_CHECK_
    NUpdateChecker::instance().setCenterInWindow(m_mainWindow);
#endif

    m_actionManager = new NActionManager(this);
    // keyboard shortcuts
    foreach (NAction *action, findChildren<NAction *>()) {
        if (!action->shortcuts().isEmpty()) {
            m_mainWindow->addAction(action);
        }
    }

    m_systemTray = new QSystemTrayIcon(this);
    m_systemTray->setContextMenu(m_actionManager->trayIconMenu());
    m_systemTray->setIcon(m_mainWindow->windowIcon());
    m_trayClickTimer = new QTimer(this);
    m_trayClickTimer->setSingleShot(true);

    loadSettings();

    skinProgram.property("afterShow").call(skinProgram);

    if (NSettings::instance()->value("RestorePlaylist").toBool()) {
        loadDefaultPlaylist();
    }

    connectSignals();

    m_settingsSaveTimer = new QTimer(this);
    connect(m_settingsSaveTimer, &QTimer::timeout, [this]() { saveSettings(); });
    m_settingsSaveTimer->start(5000); // 5 seconds

    m_writeDefaultPlaylistTimer = new QTimer(this);
    m_writeDefaultPlaylistTimer->setSingleShot(true);
    connect(m_writeDefaultPlaylistTimer, &QTimer::timeout,
            [this]() { writePlaylist(NCore::defaultPlaylistPath(), N::NulloyM3u); });
}

NPlayer::~NPlayer()
{
    NPluginLoader::deinit();
    delete m_mainWindow;
    delete m_settings;
}

NMainWindow *NPlayer::mainWindow()
{
    return m_mainWindow;
}

void NPlayer::connectSignals()
{
    connect(m_playbackEngine, SIGNAL(mediaChanged(const QString &, int)), this,
            SLOT(on_playbackEngine_mediaChanged(const QString &, int)));
    connect(m_playbackEngine, SIGNAL(mediaFailed(const QString &, int)), this,
            SLOT(on_playbackEngine_mediaFailed(const QString &, int)));
    connect(m_playbackEngine, SIGNAL(stateChanged(N::PlaybackState)), this,
            SLOT(on_playbackEngine_stateChanged(N::PlaybackState)));
    connect(m_playbackEngine, SIGNAL(positionChanged(qreal)), m_waveformSlider,
            SLOT(setValue(qreal)));
    if (m_spectrumWidget) {
        connect(m_playbackEngine, SIGNAL(spectrumChanged(const QVector<float> &, int)),
                m_spectrumWidget, SLOT(setSpectrum(const QVector<float> &, int)));
    }
    // level meters need an engine that measures levels (GStreamer does)
    bool levels = m_playbackEngine->metaObject()->indexOfMethod("setLevelsEnabled(bool)") >= 0;
    foreach (NLevelMeter *meter, m_levelMeters) {
        if (levels) {
            connect(m_playbackEngine,
                    SIGNAL(levelsChanged(const QVector<float> &, const QVector<float> &)), meter,
                    SLOT(setLevels(const QVector<float> &, const QVector<float> &)));
        }
    }
    if (levels) {
        QMetaObject::invokeMethod(m_playbackEngine, "setLevelsEnabled",
                                  Q_ARG(bool, !m_levelMeters.isEmpty()));
    }
    if (QObject *builder = dynamic_cast<QObject *>(NPluginLoader::getPlugin(N::WaveformBuilder))) {
        connect(builder, SIGNAL(bpmDetected(const QString &, qreal)), this,
                SLOT(handleBpmDetected(const QString &, qreal)));
    }
    connect(m_playbackEngine, SIGNAL(tick(qint64)), m_trackInfoWidget,
            SLOT(updatePlaybackLabels(qint64)));
    connect(m_playbackEngine, SIGNAL(message(N::MessageIcon, const QString &, const QString &)),
            m_logDialog, SLOT(showMessage(N::MessageIcon, const QString &, const QString &)));

    connect(m_mainWindow, SIGNAL(closed()), this, SLOT(on_mainWindow_closed()));

    connect(m_preferencesDialog, &NPreferencesDialog::settingsChanged, this,
            &NPlayer::applySettings);

    if (QAbstractButton *playButton = m_mainWindow->findChild<QAbstractButton *>("playButton")) {
        connect(playButton, SIGNAL(clicked()), this, SLOT(playPause()));
    }

    if (QAbstractButton *stopButton = m_mainWindow->findChild<QAbstractButton *>("stopButton")) {
        connect(stopButton, SIGNAL(clicked()), m_playbackEngine, SLOT(stop()));
    }

    if (QAbstractButton *prevButton = m_mainWindow->findChild<QAbstractButton *>("prevButton")) {
        connect(prevButton, SIGNAL(clicked()), m_playlistWidget, SLOT(playPrevItem()));
    }

    if (QAbstractButton *nextButton = m_mainWindow->findChild<QAbstractButton *>("nextButton")) {
        connect(nextButton, SIGNAL(clicked()), m_playlistWidget, SLOT(playNextItem()));
    }

    if (QAbstractButton *closeButton = m_mainWindow->findChild<QAbstractButton *>("closeButton")) {
        connect(closeButton, SIGNAL(clicked()), m_mainWindow, SLOT(close()));
    }

    if (QAbstractButton *minimizeButton = m_mainWindow->findChild<QAbstractButton *>(
            "minimizeButton")) {
        connect(minimizeButton, SIGNAL(clicked()), m_mainWindow, SLOT(showMinimized()));
    }

    if (m_volumeSlider) {
        connect(m_volumeSlider, SIGNAL(sliderMoved(qreal)), m_playbackEngine,
                SLOT(setVolume(qreal)));
        connect(m_playbackEngine, SIGNAL(volumeChanged(qreal)), m_volumeSlider,
                SLOT(setValue(qreal)));
        connect(m_mainWindow, SIGNAL(scrolled(int)), this, SLOT(on_mainWindow_scrolled(int)));
    }

    if (QAbstractButton *repeatButton = m_mainWindow->findChild<QAbstractButton *>(
            "repeatButton")) {
        connect(repeatButton, SIGNAL(clicked(bool)), m_playlistWidget, SLOT(setRepeatMode(bool)));
        connect(m_playlistWidget, SIGNAL(repeatModeChanged(bool)), repeatButton,
                SLOT(setChecked(bool)));
    }

    if (QAbstractButton *shuffleButton = m_mainWindow->findChild<QAbstractButton *>(
            "shuffleButton")) {
        connect(shuffleButton, SIGNAL(clicked()), m_playlistWidget, SLOT(shufflePlaylist()));
    }

    connect(m_playlistWidget, SIGNAL(addMoreRequested()), this,
            SLOT(on_playlist_addMoreRequested()));
    connect(m_playlistWidget, &NPlaylistWidget::durationChanged, [this](int durationSec) {
        NPlaylistWidgetItem *item = m_playlistWidget->playingItem();
        if (item) {
            m_trackInfoReader->setSource(item->data(N::PathRole).toString());
        }

        m_trackInfoReader->updatePlaylistDuration(durationSec);

        m_trackInfoWidget->updatePlaylistLabels();

        QString format = NSettings::instance()->value("WindowTitleTrackInfo").toString();
        if (!format.isEmpty()) {
            QString title = m_trackInfoReader->toString(format);
            m_mainWindow->setTitle(title);
        }
    });
    connect(m_playlistWidget, &NPlaylistWidget::itemsChanged, [this]() {
        m_writeDefaultPlaylistTimer->start(100); //
    });
    connect(m_playlistWidget, &NPlaylistWidget::playingItemChanged,
            [this]() { savePlaybackState(); });
    connect(m_playlistWidget, &NPlaylistWidget::playlistFinished, [this]() {
        if (NSettings::instance()->value("QuitWhenFinished").toBool()) {
            QCoreApplication::quit();
        }
    });

    connect(m_waveformSlider, &NWaveformSlider::filesDropped,
            [this](const QList<NPlaylistDataItem> &dataItems) {
                m_playlistWidget->setItems(dataItems);
                m_playlistWidget->playRow(0);
            });
    connect(m_waveformSlider, SIGNAL(sliderMoved(qreal)), m_playbackEngine,
            SLOT(setPosition(qreal)));

    connect(m_mainWindow, SIGNAL(customContextMenuRequested(const QPoint &)), this,
            SLOT(showContextMenu(const QPoint &)));
    connect(m_playlistWidget, &NPlaylistWidget::contextMenuRequested, this,
            &NPlayer::showPlaylistContextMenu);
    connect(m_systemTray, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this,
            SLOT(on_trayIcon_activated(QSystemTrayIcon::ActivationReason)));
    connect(m_trayClickTimer, SIGNAL(timeout()), this, SLOT(on_trayClickTimer_timeout()));
}

NPlaybackEngineInterface *NPlayer::playbackEngine()
{
    return m_playbackEngine;
}

NPlaylistWidget *NPlayer::playlistWidget()
{
    return m_playlistWidget;
}

NTagReaderInterface *NPlayer::tagReader()
{
    return dynamic_cast<NTagReaderInterface *>(NPluginLoader::getPlugin(N::TagReader));
}

NCoverWidget *NPlayer::coverWidget()
{
    return m_coverWidget;
}

NSettings *NPlayer::settings()
{
    return m_settings;
}

bool NPlayer::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent *fileEvent = static_cast<QFileOpenEvent *>(event);

        if (!fileEvent->file().isEmpty()) {
            m_playlistWidget->setFiles(QStringList() << fileEvent->file());
            m_playlistWidget->playRow(0);
        }

        return false;
    }

    return QObject::eventFilter(obj, event);
}

void NPlayer::readMessage(const QString &str)
{
    if (str.isEmpty()) {
        m_mainWindow->show();
        m_mainWindow->activateWindow();
        m_mainWindow->raise();
        return;
    }
    QStringList argList = str.split(MSG_SPLITTER);
    QStringList files;
    QStringList options;
    foreach (QString arg, argList) {
        if (arg.startsWith("--")) {
            options << arg;
        } else if (QFile(arg).exists()) {
            files << arg;
        }
    }

    foreach (QString arg, options) {
        if (arg == "--next") {
            m_playlistWidget->playNextItem();
            return;
        } else if (arg == "--prev") {
            m_playlistWidget->playPrevItem();
            return;
        } else if (arg == "--stop") {
            m_playbackEngine->stop();
            return;
        } else if (arg == "--pause") {
            m_playbackEngine->play();
            return;
        }
    }

    if (!files.isEmpty()) {
        if (NSettings::instance()->value("EnqueueFiles").toBool()) {
            int lastRow = m_playlistWidget->count();
            m_playlistWidget->addFiles(files);
            if (m_playbackEngine->state() == N::PlaybackStopped ||
                NSettings::instance()->value("PlayEnqueued").toBool()) {
                m_playlistWidget->playRow(lastRow);
                m_playbackEngine->setPosition(0); // overrides setPosition() in loadDefaultPlaylist()
            }
        } else {
            m_playlistWidget->setFiles(files);
            m_playlistWidget->playRow(0);
        }
    }
}

void NPlayer::loadDefaultPlaylist()
{
    if (!QFileInfo(NCore::defaultPlaylistPath()).exists() ||
        !m_playlistWidget->setPlaylist(NCore::defaultPlaylistPath())) {
        return;
    }

    QStringList playlistRowValues = m_settings->value("PlaylistRow").toStringList();
    if (!playlistRowValues.isEmpty()) {
        int row = playlistRowValues.at(0).toInt();
        qreal pos = playlistRowValues.at(1).toFloat();

        if (row < 0 || row > m_playlistWidget->count() - 1) {
            return;
        }

        if (!m_settings->value("StartPaused").toBool()) {
            m_playlistWidget->playRow(row);
            m_playbackEngine->setPosition(pos);
        } else { // do the work that would have been done upon m_playbackEngine::mediaChanged()
            NPlaylistWidgetItem *item = m_playlistWidget->itemAtRow(row);
            m_playlistWidget->setPlayingItem(item);

            QString file = item->data(N::PathRole).toString();
            int id = item->data(N::IdRole).toInt();

            m_playbackEngine->setMedia(file, id);
            m_playbackEngine->setPosition(pos);

            loadCoverArt(file);

            prepareBpmDetection(file);
            m_waveformSlider->setMedia(file);
            m_waveformSlider->setValue(pos);
            m_waveformSlider->setPausedState(true);
            m_trackInfoWidget->updateFileLabels(file);
            // the engine doesn't report a media change until playback starts,
            // so tell the skin to show this track's title, artist and details
            QTimer::singleShot(0, this, [this]() { emit trackInfoChanged(); });
        }
    }
}

void NPlayer::writePlaylist(const QString &file, N::M3uExtention ext)
{
    QList<NPlaylistDataItem> dataItemsList;
    for (int i = 0; i < m_playlistWidget->count(); ++i) {
        NPlaylistDataItem dataItem = m_playlistWidget->itemAtRow(i)->dataItem();
        dataItemsList << dataItem;
    }
    NPlaylistStorage::writeM3u(file, dataItemsList, ext);
}

void NPlayer::loadSettings()
{
    m_systemTray->setVisible(m_settings->value("TrayIcon").toBool());

#ifndef _N_NO_UPDATE_CHECK_
    if (m_settings->value("AutoCheckUpdates").toBool()) {
        NUpdateChecker::instance().checkOnline();
    }
#endif

    m_playbackEngine->setVolume(m_settings->value("Volume").toFloat());
    m_volumeSlider->setValue(m_settings->value("Volume").toFloat());
    applyVisualiserSettings();
}

void NPlayer::applyVisualiserSettings()
{
    bool visualiser = m_spectrumWidget && m_settings->value("Visualiser").toBool();
    if (m_spectrumWidget) {
        m_spectrumWidget->setColorScheme(m_settings->value("WaveformRgb").toBool(),
                                         m_settings->value("WaveformColorScheme").toString(),
                                         QColor(m_settings->value("AccentColor").toString()));
        NSpectrumWidget::Appearance look;
        look.style = m_settings->value("VisualiserStyle").toString();
        look.density = m_settings->value("VisualiserDensity").toString();
        look.sensitivity = m_settings->value("VisualiserSensitivity").toString();
        look.trebleBoost = m_settings->value("VisualiserTrebleBoost").toString();
        look.fallSpeed = m_settings->value("VisualiserFallSpeed").toString();
        look.peaks = m_settings->value("VisualiserPeaks").toBool();
        look.colors = m_settings->value("VisualiserColors").toString();
        look.gradientFrom = QColor(m_settings->value("VisualiserGradientFrom").toString());
        look.gradientTo = QColor(m_settings->value("VisualiserGradientTo").toString());
        look.frameRate = m_settings->value("VisualiserFrameRate").toInt();
        m_spectrumWidget->setAppearance(look);
        m_spectrumWidget->setVisible(visualiser);
        if (!visualiser) {
            m_spectrumWidget->clear();
        }
    }
    QMetaObject::invokeMethod(m_playbackEngine, "setSpectrumFrameRate",
                              Q_ARG(int, m_settings->value("VisualiserFrameRate").toInt()));
    QMetaObject::invokeMethod(m_playbackEngine, "setSpectrumEnabled", Q_ARG(bool, visualiser));
}

void NPlayer::saveSettings()
{
    m_settings->setValue("Volume", QString::number(m_playbackEngine->volume()));
    m_mainWindow->saveSettings();
    savePlaybackState();
    m_actionManager->saveSettings();
}

void NPlayer::savePlaybackState()
{
    int row = m_playlistWidget->playingRow();
    qreal pos = m_playbackEngine->position();
    m_settings->setValue("PlaylistRow", QStringList()
                                            << QString::number(row) << QString::number(pos));
}

void NPlayer::applySettings()
{
    m_systemTray->setVisible(m_settings->value("TrayIcon").toBool());
    m_trackInfoWidget->loadSettings();
    m_waveformSlider->setRgbMode(m_settings->value("WaveformRgb").toBool());
    updateAmbientImage(); // "AmbientBackground" may have changed
    m_waveformSlider->setColorScheme(m_settings->value("WaveformColorScheme").toString(),
                                     QColor(m_settings->value("AccentColor").toString()));
    applyVisualiserSettings();
    m_trackInfoWidget->updateFileLabels(m_playbackEngine->currentMedia());
    m_playlistWidget->setColumnsMode(m_settings->value("PlaylistColumns").toBool());
    m_playlistWidget->processVisibleItems();
    m_actionManager->saveSettings();
}

void NPlayer::on_mainWindow_closed()
{
    if (m_settings->value("MinimizeToTray").toBool()) {
        m_systemTray->setVisible(true);
    } else {
#ifdef Q_OS_MAC
        if (m_settings->value("QuitOnClose").toBool()) {
            QCoreApplication::quit();
        }
#else
        QCoreApplication::quit();
#endif
    }
}

void NPlayer::on_trayClickTimer_timeout()
{
    if (!m_trayIconDoubleClickCheck) {
        trayIconCountClicks(1);
    }
}

void NPlayer::on_trayIcon_activated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) { // single click
        m_trayIconDoubleClickCheck = false;
        m_trayClickTimer->start(QApplication::doubleClickInterval());
    } else if (reason == QSystemTrayIcon::DoubleClick) {
        m_trayIconDoubleClickCheck = true;
        trayIconCountClicks(2);
    }
}

void NPlayer::toggleWindowVisibility()
{
    if (!m_mainWindow->isVisible() || m_mainWindow->isMinimized()) {
        m_mainWindow->show();
        m_mainWindow->activateWindow();
        m_mainWindow->raise();

    } else if (m_settings->value("MinimizeToTray").toBool()) {
        m_mainWindow->setVisible(false);
        m_systemTray->setVisible(true);
    } else {
        m_mainWindow->showMinimized();
    }
}

void NPlayer::trayIconCountClicks(int clicks)
{
    if (clicks == 1) {
        m_mainWindow->show();
        m_mainWindow->activateWindow();
        m_mainWindow->raise();
    } else if (clicks == 2) {
        toggleWindowVisibility();
    }
    if (!m_settings->value("TrayIcon").toBool()) {
        m_systemTray->setVisible(!m_mainWindow->isVisible());
    }
}

void NPlayer::quit()
{
    saveSettings();
}

void NPlayer::on_playbackEngine_mediaChanged(const QString &file, int)
{
    QString title;
    QString format = NSettings::instance()->value("WindowTitleTrackInfo").toString();
    if (!format.isEmpty()) {
        m_trackInfoReader->setSource(file);
        title = m_trackInfoReader->toString(format);
    }
    m_mainWindow->setTitle(title);
    m_systemTray->setToolTip(title);

    prepareBpmDetection(file);
    m_waveformSlider->setMedia(file);
    m_trackInfoWidget->updateFileLabels(file);
    loadCoverArt(file);

#ifdef Q_OS_MAC
    if (file.isEmpty()) {
        NMacMediaKeys::instance()->setNowPlaying(QString(), QString(), 0);
    } else {
        m_trackInfoReader->setSource(file);
        NMacMediaKeys::instance()->setNowPlaying(m_trackInfoReader->toString("{%t|%F}"),
                                                 m_trackInfoReader->toString("%a"),
                                                 m_playbackEngine->durationMsec());
    }
#endif
}

void NPlayer::loadCoverArt(const QString &file)
{
    if (!m_settings->value("ShowCoverArt").toBool()) {
        m_currentCover = QImage();
        updateAmbientImage();
        return;
    }
    QImage image;
    if (m_coverReader) {
        m_coverReader->setSource(file);
        QList<QImage> images = m_coverReader->getImages();

        if (!images.isEmpty()) {
            image = images.first();
        }
    }

    if (image.isNull()) {
        QFileInfo fileInfo(file);
        QDir dir = fileInfo.absoluteDir();
        QStringList images = dir.entryList(QStringList() << "*.jpg"
                                                         << "*.jpeg"
                                                         << "*.png",
                                           QDir::Files);

        // search for image which file name starts same as source file:
        QString baseName = fileInfo.completeBaseName();
        QString imageFile;
        foreach (QString image, images) {
            if (baseName.startsWith(QFileInfo(image).completeBaseName())) {
                imageFile = dir.absolutePath() + "/" + image;
                break;
            }
        }

        // search for cover.* or folder.* or front.*:
        if (imageFile.isEmpty()) {
            QStringList matchedImages = images.filter(
                QRegExp("^(cover|folder|front)\\..*$", Qt::CaseInsensitive));
            if (!matchedImages.isEmpty()) {
                imageFile = dir.absolutePath() + "/" + matchedImages.first();
            }
        }
        if (!imageFile.isEmpty()) {
            image = QImage(imageFile);
        }
    }

    if (image.isNull()) {
        m_coverWidget->hide();
    } else {
        m_coverWidget->show();
        m_coverWidget->setPixmap(QPixmap::fromImage(image));
    }
    if (m_platterWidget) { // a skin's turntable shows the cover as the record label
        m_platterWidget->setPixmap(image.isNull() ? QPixmap() : QPixmap::fromImage(image));
    }
    m_currentCover = image;
    updateAmbientImage();
}

void NPlayer::updateAmbientImage()
{
    QString previous = m_ambientImagePath;
    m_ambientImagePath.clear();

    if (!m_currentCover.isNull() && m_settings->value("AmbientBackground").toBool()) {
        // cheap, smooth blur: shrink to a few pixels, then scale back up in two
        // stages (one big jump leaves visible blocks). Fill a large canvas so
        // the skin can centre it without scaling (plain QWidgets only support
        // background-image, not a stretched border-image).
        const QSize canvas(1920, 1200);
        QImage tiny = m_currentCover.scaled(24, 24, Qt::KeepAspectRatioByExpanding,
                                            Qt::SmoothTransformation);
        QImage soft = tiny.scaled(96, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage blurred = soft.scaled(canvas, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGB32);
        blurred = blurred.copy((blurred.width() - canvas.width()) / 2,
                               (blurred.height() - canvas.height()) / 2, canvas.width(),
                               canvas.height());
        // darken so text and controls stay readable
        QPainter painter(&blurred);
        painter.fillRect(blurred.rect(), QColor(10, 12, 16, 95));
        painter.end();

        // a new name each time: stylesheets cache images by path
        QString path = QDir::tempPath() + QString("/trackdeck-ambient-%1-%2.jpg")
                                              .arg(QCoreApplication::applicationPid())
                                              .arg(++m_ambientImageCounter);
        if (blurred.save(path, "JPG", 88)) {
            m_ambientImagePath = path;
        }
    }

    if (!previous.isEmpty() && previous != m_ambientImagePath) {
        QFile::remove(previous);
    }
    emit ambientImageChanged(m_ambientImagePath);
}

void NPlayer::on_playbackEngine_mediaFailed(const QString &, int)
{
    on_playbackEngine_mediaChanged("", 0);
}

void NPlayer::on_playbackEngine_stateChanged(N::PlaybackState state)
{
    bool whilePlaying = m_settings->value("WhilePlayingOnTop").toBool();
    bool alwaysOnTop = m_settings->value("AlwaysOnTop").toBool();
    bool oldOnTop = m_mainWindow->isOnTop();
    bool newOnTop = (whilePlaying && state == N::PlaybackPlaying);
    if (!alwaysOnTop && (oldOnTop != newOnTop)) {
        m_mainWindow->setOnTop(newOnTop);
    }
#ifdef Q_OS_WIN
    if (NW7TaskBar::instance()->isEnabled()) {
        if (state == N::PlaybackPlaying) {
            NW7TaskBar::instance()->setState(NW7TaskBar::Normal);
        } else {
            if (m_playbackEngine->position() != 0)
                NW7TaskBar::instance()->setState(NW7TaskBar::Paused);
            else
                NW7TaskBar::instance()->setState(NW7TaskBar::NoProgress);
        }
    }
#endif

    m_waveformSlider->setPausedState(state == N::PlaybackPaused);

#ifdef Q_OS_MAC
    NMacMediaKeys::instance()->setPlaybackState(
        state == N::PlaybackPlaying  ? NMacMediaKeys::Playing
        : state == N::PlaybackPaused ? NMacMediaKeys::Paused
                                     : NMacMediaKeys::Stopped);
#endif
}

void NPlayer::showTagEditor(const QStringList &files)
{
    QStringList saved;
    {
        NTagEditorDialog dialog(files, m_mainWindow);
        if (!dialog.isValid()) {
            return;
        }
        dialog.exec();
        saved = dialog.savedFiles();
    }
    if (saved.isEmpty()) {
        return;
    }

    // show the new tags in the playlist and, for the playing track, the track info
    m_playlistWidget->refreshItemsForFiles(saved);
    NPlaylistWidgetItem *playing = m_playlistWidget->playingItem();
    if (playing && saved.contains(playing->data(N::PathRole).toString())) {
        QString file = playing->data(N::PathRole).toString();
        m_trackInfoWidget->updateFileLabels(file);
        emit trackInfoChanged();
    }
}

void NPlayer::on_playlist_addMoreRequested()
{
    if (!NSettings::instance()->value("LoadNext").toBool()) {
        return;
    }
    QDir::SortFlag flag = (QDir::SortFlag)NSettings::instance()->value("LoadNextSort").toInt();
    QString file = m_playlistWidget->playingItem()->data(N::PathRole).toString();
    QString path = QFileInfo(file).path();
    QStringList entryList =
        QDir(path).entryList(NSettings::instance()->value("FileFilters").toString().split(' '),
                             QDir::Files | QDir::NoDotAndDotDot, flag);
    int index = entryList.indexOf(QFileInfo(file).fileName());
    if (index != -1 && entryList.size() > index + 1) {
        m_playlistWidget->addFiles(QStringList() << path + "/" + entryList.at(index + 1));
    }
}

void NPlayer::playPause()
{
    if (!m_playlistWidget->hasPlayingItem()) {
        if (m_playlistWidget->count() > 0) {
            m_playlistWidget->playRow(0);
        } else {
            showOpenFileDialog();
        }
    } else {
        if (m_playbackEngine->state() == N::PlaybackPlaying) {
            m_playbackEngine->pause();
        } else {
            m_playbackEngine->play();
        }
    }
}

void NPlayer::showAboutDialog()
{
    if (!m_aboutDialog) {
        m_aboutDialog = new NAboutDialog(m_mainWindow);
    }
    m_aboutDialog->show();
}

void NPlayer::showPreferencesDialog()
{
    m_preferencesDialog->exec();
}

void NPlayer::showOpenFileDialog()
{
    QString filters = NSettings::instance()->value("FileFilters").toString();
    QStringList files = QFileDialog::getOpenFileNames(m_mainWindow, tr("Add Files"),
                                                      m_settings->value("LastDirectory").toString(),
                                                      tr("All supported") + " (" + filters + ");;" +
                                                          tr("All files") + " (*)");

    if (files.isEmpty()) {
        return;
    }

    QString lastDir = QFileInfo(files.first()).path();
    m_settings->setValue("LastDirectory", lastDir);

    bool isEmpty = (m_playlistWidget->count() == 0);
    m_playlistWidget->addFiles(files);
    if (isEmpty) {
        m_playlistWidget->playRow(0);
    }
}

void NPlayer::on_mainWindow_scrolled(int delta)
{
    QWheelEvent event(QPoint(), delta, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(m_volumeSlider, &event);
}

void NPlayer::showOpenDirDialog()
{
    QString dir = QFileDialog::getExistingDirectory(m_mainWindow, tr("Add Directory"),
                                                    m_settings->value("LastDirectory").toString(),
                                                    QFileDialog::ShowDirsOnly |
                                                        QFileDialog::DontResolveSymlinks);

    if (dir.isEmpty()) {
        return;
    }

    QString lastDir = QFileInfo(dir).path();
    m_settings->setValue("LastDirectory", lastDir);

    bool isEmpty = (m_playlistWidget->count() == 0);
    m_playlistWidget->addItems(NUtils::dirListRecursive(dir));
    if (isEmpty) {
        m_playlistWidget->playRow(0);
    }
}

void NPlayer::showSavePlaylistDialog()
{
    QString folder = playlistsFolder();
    QDir().mkpath(folder);
    QString current = m_settings->value("CurrentPlaylistFile").toString();
    QString start = current.isEmpty() ? folder + "/" + tr("Untitled") + ".m3u8" : current;
    QString file = QFileDialog::getSaveFileName(m_mainWindow, tr("Save Playlist As"), start,
                                                tr("Playlist") + " (*.m3u8 *.m3u)");
    if (file.isEmpty()) {
        return;
    }
    if (!file.endsWith(".m3u8", Qt::CaseInsensitive) && !file.endsWith(".m3u", Qt::CaseInsensitive)) {
        file.append(".m3u8");
    }
    // extended M3U (#EXTINF) in UTF-8: readable by rekordbox, Serato, Engine DJ and most players
    writePlaylist(file, N::ExtM3u);
    rememberPlaylist(file);
}

// Playlists >>

QString NPlayer::playlistsFolder() const
{
    QString folder = m_settings->value("PlaylistsFolder").toString();
    if (folder.startsWith("~/")) {
        folder = QDir::homePath() + folder.mid(1);
    }
    return QDir::cleanPath(folder);
}

QStringList NPlayer::savedPlaylists() const
{
    QDir dir(playlistsFolder());
    QStringList files;
    for (const QFileInfo &info :
         dir.entryInfoList(QStringList() << "*.m3u8" << "*.m3u", QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        files << info.absoluteFilePath();
    }
    return files;
}

QStringList NPlayer::recentPlaylists() const
{
    QStringList recent;
    for (const QString &file : m_settings->value("RecentPlaylists").toStringList()) {
        if (QFileInfo(file).isFile()) {
            recent << file;
        }
    }
    return recent;
}

void NPlayer::rememberPlaylist(const QString &file)
{
    m_settings->setValue("CurrentPlaylistFile", file);
    QStringList recent = m_settings->value("RecentPlaylists").toStringList();
    recent.removeAll(file);
    recent.prepend(file);
    while (recent.size() > 10) {
        recent.removeLast();
    }
    m_settings->setValue("RecentPlaylists", recent);
}

void NPlayer::savePlaylist()
{
    QString current = m_settings->value("CurrentPlaylistFile").toString();
    if (current.isEmpty() || !QFileInfo(QFileInfo(current).path()).isDir()) {
        showSavePlaylistDialog();
        return;
    }
    writePlaylist(current, N::ExtM3u);
    rememberPlaylist(current);
}

void NPlayer::showOpenPlaylistDialog()
{
    QString folder = playlistsFolder();
    QDir().mkpath(folder);
    QString file = QFileDialog::getOpenFileName(m_mainWindow, tr("Open Playlist"), folder,
                                                tr("Playlist") + " (*.m3u8 *.m3u)");
    if (!file.isEmpty()) {
        openPlaylist(file);
    }
}

void NPlayer::openPlaylist(const QString &file)
{
    QList<NPlaylistDataItem> items = NPlaylistStorage::readM3u(file);
    if (items.isEmpty()) {
        QMessageBox::warning(m_mainWindow, tr("Open Playlist"),
                             tr("\"%1\" has no tracks, or couldn't be read.").arg(QFileInfo(file).fileName()));
        return;
    }

    QString behaviour = m_settings->value("OpenPlaylistBehaviour").toString(); // ask, replace, add
    if (m_playlistWidget->count() == 0) {
        behaviour = "replace"; // nothing to lose
    }
    if (behaviour != "replace" && behaviour != "add") {
        QMessageBox box(m_mainWindow);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Open Playlist"));
        box.setText(tr("Open \"%1\" (%n track(s))", "", items.size())
                        .arg(QFileInfo(file).completeBaseName()));
        box.setInformativeText(tr("Replace the current list, or add these tracks to the end of it?"));
        QPushButton *replace = box.addButton(tr("Replace"), QMessageBox::AcceptRole);
        QPushButton *add = box.addButton(tr("Add to Current List"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(replace);
        QCheckBox remember(tr("Don't ask again (change this in Preferences)"));
        box.setCheckBox(&remember);
        box.exec();
        if (box.clickedButton() == replace) {
            behaviour = "replace";
        } else if (box.clickedButton() == add) {
            behaviour = "add";
        } else {
            return;
        }
        if (remember.isChecked()) {
            m_settings->setValue("OpenPlaylistBehaviour", behaviour);
        }
    }

    if (behaviour == "replace") {
        m_playlistWidget->setItems(items);
        rememberPlaylist(file); // Save now updates this playlist
    } else {
        m_playlistWidget->addItems(items);
        QStringList recent = m_settings->value("RecentPlaylists").toStringList();
        recent.removeAll(file);
        recent.prepend(file);
        m_settings->setValue("RecentPlaylists", QStringList(recent.mid(0, 10)));
    }
}

void NPlayer::addSelectionToPlaylist(const QString &file)
{
    QList<NPlaylistDataItem> selected = m_playlistWidget->selectedDataItems();
    if (selected.isEmpty()) {
        return;
    }
    QList<NPlaylistDataItem> items = NPlaylistStorage::readM3u(file);
    items << selected;
    NPlaylistStorage::writeM3u(file, items, N::ExtM3u);
}

void NPlayer::addSelectionToNewPlaylist()
{
    if (m_playlistWidget->selectedDataItems().isEmpty()) {
        return;
    }
    bool ok = false;
    QString name = QInputDialog::getText(m_mainWindow, tr("New Playlist"), tr("Playlist name:"),
                                         QLineEdit::Normal, QString(), &ok)
                       .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    name.replace(QRegExp("[/\\\\:*?\"<>|]"), "-"); // keep it a valid file name everywhere
    QString folder = playlistsFolder();
    QDir().mkpath(folder);
    QString file = folder + "/" + name + ".m3u8";
    if (QFileInfo::exists(file)) {
        if (QMessageBox::question(m_mainWindow, tr("New Playlist"),
                                  tr("\"%1\" already exists. Add the tracks to it?").arg(name)) !=
            QMessageBox::Yes) {
            return;
        }
    }
    addSelectionToPlaylist(file);
}
// << Playlists

void NPlayer::showContextMenu(const QPoint &pos)
{
    // (1, 1) offset to avoid accidental item activation
    m_actionManager->contextMenu()->exec(m_mainWindow->mapToGlobal(pos) + QPoint(1, 1));
}

void NPlayer::showPlaylistContextMenu(const QPoint &pos)
{
    m_actionManager->playlistContextMenu()->exec(pos);
}

void NPlayer::showToolTip(const QString &text)
{
    if (text.isEmpty()) {
        QToolTip::hideText();
        return;
    }

    QStringList offsetList = NSettings::instance()->value("TooltipOffset").toStringList();
    QToolTip::showText(mapToGlobal(QCursor::pos() +
                                   QPoint(offsetList.at(0).toInt(), offsetList.at(1).toInt())),
                       text);
}

bool NPlayer::revealInFileManager(const QString &file, QString *error) const
{
    QFileInfo fileInfo(file);

    if (!fileInfo.exists()) {
        *error = tr("File doesn't exist: <b>%1</b>").arg(QFileInfo(file).fileName());
        return false;
    }

    QString cmd;
    int res = 0;

    bool customFileManager = NSettings::instance()->value("CustomFileManager").toBool();
    if (customFileManager) {
        cmd = NSettings::instance()->value("CustomFileManagerCommand").toString();
        if (cmd.isEmpty()) {
            *error = tr("Custom File Manager is enabled but not configured.");
            return false;
        }
        QString filePath = file;
        QString fileName = fileInfo.fileName();
        QString canonicalPath = fileInfo.canonicalPath();
#if defined Q_OS_WIN
        filePath.replace('/', '\\');
        fileName.replace('/', '\\');
        canonicalPath.replace('/', '\\');
#else
        // escape single quote
        filePath.replace("'", "'\\''");
        fileName.replace("'", "'\\''");
        canonicalPath.replace("'", "'\\''");
#endif
        cmd.replace("%p", filePath);
        cmd.replace("%F", fileName);
        cmd.replace("%P", canonicalPath);

#if !defined Q_OS_WIN
        res = QProcess::execute("sh", QStringList{"-c", cmd});
#else
        res = QProcess::execute(cmd);
#endif
    } else {
        QString path = fileInfo.canonicalFilePath();
        QStringList args;
#if defined Q_OS_WIN
        cmd = "explorer.exe";
        args = QStringList{"/n", ",", "/select", ",", path.replace('/', '\\')};
#elif defined Q_OS_LINUX
        cmd = "xdg-open";
        args = QStringList{fileInfo.canonicalPath().replace("'", "'\\''")};
#elif defined Q_OS_MAC
        cmd = "open";
        args = QStringList{"-R", path.replace("'", "'\\''")};
#endif
        res = QProcess::execute(cmd, args);
        cmd += " " + args.join(' ');
    }

#ifndef Q_OS_WIN
    if (res != 0) {
        *error = tr("File manager command failed with exit code <b>%1</b>:").arg(res) +
                 QString("<br><br><span style=\"font-family: 'Lucida Console', Monaco, "
                         "monospace\">%1</span>")
                     .arg(cmd);
        return false;
    }
#endif

    return true;
}

QString NPlayer::trackInfo(const QString &format)
{
    QString file = m_playbackEngine->currentMedia();
    if (file.isEmpty()) {
        return QString();
    }
    m_skinTrackInfoReader->setSource(file);
    return m_skinTrackInfoReader->toString(format);
}

void NPlayer::prepareBpmDetection(const QString &file)
{
    NWaveformBuilderInterface *builder = dynamic_cast<NWaveformBuilderInterface *>(
        NPluginLoader::getPlugin(N::WaveformBuilder));
    if (!builder) {
        return;
    }

    bool wanted = m_settings->value("BpmDetection").toBool() && !file.isEmpty() &&
                  QFileInfo(file).isFile() && !NBpmCache::instance()->lookup(file, NULL);
    if (wanted) {
        // tagged tracks don't need it, and anything 15 minutes or longer is
        // most likely a DJ mix, where one BPM would be meaningless
        NTagReaderInterface *reader = tagReader();
        reader->setSource(file);
        QString tag = reader->getTag('M').trimmed();
        QString seconds = reader->getTag('D');
        if (!tag.isEmpty() && tag.toDouble() > 0) {
            wanted = false;
        } else if (!seconds.isEmpty() && seconds.toInt() >= 15 * 60) {
            wanted = false;
        }
    }
    builder->setBpmDetection(wanted);
}

void NPlayer::handleBpmDetected(const QString &file, qreal bpm)
{
    NBpmCache::instance()->insert(file, bpm);
    m_playlistWidget->refreshItemsForFile(file);
    NPlaylistWidgetItem *playing = m_playlistWidget->playingItem();
    if (playing && playing->data(N::PathRole).toString() == file) {
        m_trackInfoWidget->updateFileLabels(file);
        emit trackInfoChanged();
    }
}
