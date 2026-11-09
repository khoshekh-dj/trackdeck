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

#ifndef N_PLAYBACK_ENGINE_GSTREAMER_H
#define N_PLAYBACK_ENGINE_GSTREAMER_H

#include <gst/gst.h>

#include <QList>
#include <QVector>

#include "global.h"
#include "playbackEngineInterface.h"
#include "plugin.h"

class QTimer;

class NPlaybackEngineGStreamer : public NPlaybackEngineInterface, public NPlugin
{
    Q_OBJECT
    Q_INTERFACES(NPlaybackEngineInterface NPlugin)

private:
    GstElement *m_playbin;
    GstElement *m_pitchElement;

    // live spectrum for the visualiser: frames are queued as the spectrum
    // element measures them and released when their audio is actually heard
    struct SpectrumFrame
    {
        GstClockTime due;
        QVector<float> magnitudes;
    };
    GstElement *m_spectrum;
    bool m_spectrumEnabled;
    int m_spectrumRate;
    QList<SpectrumFrame> m_spectrumQueue;
    void queueSpectrum(GstMessage *msg);
    void releaseSpectrum();
    void clearAnalyserQueues();
    bool runningTime(GstClockTime *now) const;

    // live per-channel levels for skin meters, queued the same way
    struct LevelFrame
    {
        GstClockTime due;
        QVector<float> rms;
        QVector<float> peak;
    };
    GstElement *m_level;
    bool m_levelsEnabled;
    QList<LevelFrame> m_levelQueue;
    void queueLevels(GstMessage *msg);
    void releaseLevels();
    void updateBusPollInterval();

    QTimer *m_checkStatusTimer;
    QTimer *m_emitStateTimer;
    QTimer *m_gstBusPopTimer;
    QTimer *m_outputChangedTimer; // debounces "default audio output changed"
    qreal m_speed;
    bool m_speedPostponed;
    qreal m_pitch;
    qreal m_volume;
    qreal m_position;
    bool m_positionPostponed;
    GstState m_gstState;
    gint64 m_durationNsec;
    bool m_crossfading;
    bool m_nextMediaRequestBlock;

    QString m_currentMedia;
    int m_currentContext;
    QString m_bkpMedia;
    int m_bkpContext;

    N::PlaybackState fromGstState(GstState state) const;
    bool gstSetFile(const QString &file, int context, bool prepareNext);
    void processGstMessage(GstMessage *msg);
    void fail();

public:
    NPlaybackEngineGStreamer(QObject *parent = NULL) : NPlaybackEngineInterface(parent) {}
    ~NPlaybackEngineGStreamer();
    void init();
    QString interfaceString() const { return NPlaybackEngineInterface::interfaceString(); }
    N::PluginType type() const { return N::PlaybackEngine; }

    Q_INVOKABLE bool hasMedia() const;
    Q_INVOKABLE QString currentMedia() const;
    Q_INVOKABLE N::PlaybackState state() const;

    Q_INVOKABLE qreal volume() const;
    Q_INVOKABLE qreal position() const;
    Q_INVOKABLE qint64 durationMsec() const;
    Q_INVOKABLE qreal speed() const;
    Q_INVOKABLE qreal pitch() const;

    void _emitNextMediaRequest();
    bool _nextMediaRequestBlocked();

public slots:
    Q_INVOKABLE void setMedia(const QString &file, int context);
    Q_INVOKABLE void nextMediaRespond(const QString &file, int context);
    Q_INVOKABLE void setVolume(qreal volume);
    Q_INVOKABLE void setPosition(qreal pos);
    Q_INVOKABLE void jump(qint64 msec);
    Q_INVOKABLE void setSpeed(qreal speed);
    Q_INVOKABLE void setPitch(qreal speed);

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void pause();

private slots:
    void checkStatus();
    // Reopen the audio sink so it picks up the current default output device
    // (e.g. after switching to Bluetooth headphones), keeping position and state.
    void reopenAudioOutput();

public:
    Q_INVOKABLE void _scheduleReopenAudioOutput(); // called by name from the CoreAudio listener
    // Turns the visualiser's spectrum analysis on or off (called by name).
    Q_INVOKABLE void setSpectrumEnabled(bool enabled);
    Q_INVOKABLE void setSpectrumFrameRate(int framesPerSecond);
    // Turns the per-channel level meters on or off (called by name).
    Q_INVOKABLE void setLevelsEnabled(bool enabled);

signals:
    void positionChanged(qreal pos);
    void volumeChanged(qreal volume);
    void message(N::MessageIcon icon, const QString &file, const QString &msg);
    void mediaChanged(const QString &file, int context);
    void mediaFinished(const QString &file, int context);
    void nextMediaRequested();
    void mediaFailed(const QString &file, int context);
    // magnitudes in dB (about -80..0) for equal-width bands from 0 Hz to
    // sampleRate / 2, emitted ~60 times a second while playing
    void spectrumChanged(const QVector<float> &magnitudes, int sampleRate);
    // per channel (left, right, ...) in dBFS: the RMS and the highest
    // peak of the last stretch, emitted with the spectrum while playing
    void levelsChanged(const QVector<float> &rms, const QVector<float> &peak);
    void stateChanged(N::PlaybackState state);
    void tick(qint64 msec);
};

#endif
