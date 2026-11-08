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

#include "playbackEngineGstreamer.h"

#include <QTimer>
#include <QtGlobal>
#include <gst/pbutils/missing-plugins.h>

#include "common.h"

#ifdef Q_OS_MAC
#include <CoreAudio/CoreAudio.h>

// kAudioObjectPropertyElementMain (macOS 12+) == kAudioObjectPropertyElementMaster == 0
static const AudioObjectPropertyAddress _defaultOutputAddress = {
    kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, 0};

// called on a CoreAudio thread
static OSStatus _on_default_output_changed(AudioObjectID, UInt32, const AudioObjectPropertyAddress *,
                                           void *userData)
{
    QMetaObject::invokeMethod(reinterpret_cast<QObject *>(userData), "_scheduleReopenAudioOutput",
                              Qt::QueuedConnection);
    return noErr;
}
#endif

#define NSEC_IN_MSEC 1000000
#define CROSSFADING_MIN_DURATION_MSEC 1000 // 1 second
#define SHORT_TICK_THRESHOLD_MSEC 30000    // 30 seconds
#define SHORT_TICK_MSEC 20
#define LONG_TICK_MSEC 100
#define STATE_CHANGE_DEBOUNCE_MSEC 50
#define GST_BUS_POP_MSEC 50
#define GST_BUS_POP_SPECTRUM_MSEC 15 // poll faster while the visualiser is on
#define SPECTRUM_BANDS 1024          // ~21.5 Hz per band at 44.1 kHz

static void _on_about_to_finish(GstElement *, gpointer userData)
{
    NPlaybackEngineGStreamer *obj = reinterpret_cast<NPlaybackEngineGStreamer *>(userData);
    if ((obj->durationMsec() < CROSSFADING_MIN_DURATION_MSEC) || obj->_nextMediaRequestBlocked()) {
        return;
    }
    obj->_emitNextMediaRequest();
}

N::PlaybackState NPlaybackEngineGStreamer::fromGstState(GstState state) const
{
    switch (state) {
        case GST_STATE_PAUSED:
            return N::PlaybackPaused;
        case GST_STATE_PLAYING:
            return N::PlaybackPlaying;
        default:
            return N::PlaybackStopped;
    }
}

void NPlaybackEngineGStreamer::init()
{
    if (m_init) {
        return;
    }

    int argc;
    const char **argv;
    GError *err = NULL;
    NCore::cArgs(&argc, &argv);
    gst_init(&argc, (char ***)&argv);
    if (!gst_init_check(&argc, (char ***)&argv, &err)) {
        emit message(N::Critical, tr("Playback error"),
                     err ? QString::fromUtf8(err->message) : tr("Unknown error"));
        if (err) {
            g_error_free(err);
        }
    }
    m_playbin = gst_element_factory_make("playbin", NULL);
    g_signal_connect(m_playbin, "about-to-finish", G_CALLBACK(_on_about_to_finish), this);
    gst_element_add_property_notify_watch(m_playbin, "volume", TRUE);

    m_spectrumEnabled = false;
    m_spectrumRate = 0;
    m_spectrum = gst_element_factory_make("spectrum", NULL);
    if (m_spectrum) {
        g_object_set(m_spectrum, "bands", SPECTRUM_BANDS, "threshold", -80, "interval",
                     (guint64)(GST_SECOND / 30), "post-messages", FALSE, "message-magnitude",
                     TRUE, "message-phase", FALSE, NULL);
    }
    m_levelsEnabled = false;
    m_level = gst_element_factory_make("level", NULL);
    if (m_level) {
        g_object_set(m_level, "interval", (guint64)(GST_SECOND / 30), "post-messages", FALSE,
                     NULL);
    }
    // both analysers sit in the audio path: level -> spectrum
    if (m_spectrum && m_level) {
        GstElement *bin = gst_bin_new("analysers");
        gst_bin_add_many(GST_BIN(bin), m_level, m_spectrum, NULL);
        gst_element_link(m_level, m_spectrum);
        GstPad *sink = gst_element_get_static_pad(m_level, "sink");
        GstPad *src = gst_element_get_static_pad(m_spectrum, "src");
        gst_element_add_pad(bin, gst_ghost_pad_new("sink", sink));
        gst_element_add_pad(bin, gst_ghost_pad_new("src", src));
        gst_object_unref(sink);
        gst_object_unref(src);
        g_object_set(m_playbin, "audio-filter", bin, NULL);
    } else if (m_spectrum) {
        g_object_set(m_playbin, "audio-filter", m_spectrum, NULL);
    } else if (m_level) {
        g_object_set(m_playbin, "audio-filter", m_level, NULL);
    }

    // FIXME: https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/-/issues/1798
    //m_pitchElement = gst_element_factory_make("pitch", NULL);
    m_pitchElement = NULL;
    if (!m_pitchElement) {
        //emit message(N::Critical, "Playback Engine", "Failed to create pitch element");
    } else {
        GstElement *sink = gst_element_factory_make("autoaudiosink", NULL);
        GstElement *bin = gst_bin_new(NULL);
        gst_bin_add_many(GST_BIN(bin), m_pitchElement, sink, NULL);
        gst_element_link_many(m_pitchElement, sink, NULL);

        GstPad *pad = gst_element_get_static_pad(m_pitchElement, "sink");
        gst_element_add_pad(bin, gst_ghost_pad_new("sink", pad));
        gst_object_unref(pad);

        g_object_set(m_playbin, "audio-sink", bin, NULL);
    }

#ifdef _TESTS_
    GstElement *sink = gst_element_factory_make("fakesink", NULL);
    g_object_set(sink, "sync", TRUE, NULL);
    g_object_set(m_playbin, "audio-sink", sink, NULL);

    sink = gst_element_factory_make("fakesink", NULL);
    g_object_set(sink, "sync", TRUE, NULL);
    g_object_set(m_playbin, "video-sink", sink, NULL);
#endif

    m_speed = 1.0;
    m_speedPostponed = false;
    m_pitch = 1.0;
    m_volume = -1.0;
    m_position = 0.0;
    m_gstState = GST_STATE_NULL;
    m_positionPostponed = false;
    m_currentMedia = "";
    m_currentContext = 0;
    m_bkpMedia = "";
    m_bkpContext = 0;
    m_durationNsec = GST_CLOCK_TIME_NONE;
    m_crossfading = false;
    m_nextMediaRequestBlock = false;

    m_checkStatusTimer = new QTimer(this);
    connect(m_checkStatusTimer, SIGNAL(timeout()), this, SLOT(checkStatus()));

    m_outputChangedTimer = new QTimer(this);
    m_outputChangedTimer->setSingleShot(true);
    m_outputChangedTimer->setInterval(500); // coalesce bursts; let Bluetooth devices settle
    connect(m_outputChangedTimer, SIGNAL(timeout()), this, SLOT(reopenAudioOutput()));
#ifdef Q_OS_MAC
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &_defaultOutputAddress,
                                   _on_default_output_changed, this);
#endif

    m_emitStateTimer = new QTimer(this);
    m_emitStateTimer->setSingleShot(true);
    m_emitStateTimer->setInterval(STATE_CHANGE_DEBOUNCE_MSEC);
    connect(m_emitStateTimer, &QTimer::timeout,
            [this]() { emit stateChanged(fromGstState(m_gstState)); });

    m_gstBusPopTimer = new QTimer(this);
    m_gstBusPopTimer->setInterval(GST_BUS_POP_MSEC);
    connect(m_gstBusPopTimer, &QTimer::timeout, [this]() {
        GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(m_playbin));
        GstMessage *msg;
        while ((msg = gst_bus_pop(bus)) != NULL) {
            processGstMessage(msg);
            gst_message_unref(msg);
        }
        gst_object_unref(bus);
        releaseSpectrum();
        releaseLevels();
    });

    m_init = true;
}

NPlaybackEngineGStreamer::~NPlaybackEngineGStreamer()
{
    if (!m_init) {
        return;
    }

#ifdef Q_OS_MAC
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &_defaultOutputAddress,
                                      _on_default_output_changed, this);
#endif
    stop();
    gst_object_unref(m_playbin);
}

void NPlaybackEngineGStreamer::_scheduleReopenAudioOutput()
{
    m_outputChangedTimer->start();
}

void NPlaybackEngineGStreamer::reopenAudioOutput()
{
    if (!hasMedia() || m_crossfading) {
        return;
    }
    GstState current = GST_STATE_NULL;
    gst_element_get_state(m_playbin, &current, NULL, 0);
    if (current != GST_STATE_PLAYING && current != GST_STATE_PAUSED) {
        return; // stopped: the next play() opens the current default device anyway
    }

    gint64 posNsec = 0;
    bool havePos = gst_element_query_position(m_playbin, GST_FORMAT_TIME, &posNsec);

    // READY releases the audio device; PAUSED reopens it on the current default.
    // Seek while paused so the track doesn't briefly restart from the beginning.
    gst_element_set_state(m_playbin, GST_STATE_READY);
    gst_element_set_state(m_playbin, GST_STATE_PAUSED);
    gst_element_get_state(m_playbin, NULL, NULL, 2 * GST_SECOND); // wait for preroll
    if (havePos && posNsec > 0) {
        gst_element_seek(m_playbin, m_speed, GST_FORMAT_TIME,
                         GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                         GST_SEEK_TYPE_SET, posNsec, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
        gst_element_get_state(m_playbin, NULL, NULL, 2 * GST_SECOND);
    }
    if (current == GST_STATE_PLAYING) {
        gst_element_set_state(m_playbin, GST_STATE_PLAYING);
    }
}

bool NPlaybackEngineGStreamer::gstSetFile(const QString &file, int context, bool prepareNext)
{
    if (file.isEmpty()) {
        stop();
        m_currentMedia = "";
        m_currentContext = 0;
        emit mediaChanged(m_currentMedia, m_currentContext);
        return false;
    }

    if (!QFile(file).exists()) {
        emit message(N::Warning, file, tr("No such file or directory"));
        fail();
        return false;
    }

    GError *err = NULL;
    gchar *uri = g_filename_to_uri(QFileInfo(file).absoluteFilePath().toUtf8().constData(), NULL,
                                   &err);
    if (uri) {
        m_currentMedia = file;
        m_currentContext = context;
        if (!prepareNext) {
            gst_element_set_state(m_playbin, GST_STATE_NULL);
        }
        g_object_set(m_playbin, "uri", uri, NULL);
        g_free(uri);
    } else {
        emit message(N::Critical, file, err ? QString::fromUtf8(err->message) : tr("Invalid path"));
        if (err) {
            g_error_free(err);
        }
        fail();
        return false;
    }
    return true;
}

void NPlaybackEngineGStreamer::setMedia(const QString &file, int context)
{
    clearAnalyserQueues();
    m_crossfading = false;
    m_position = 0.0;
    m_nextMediaRequestBlock = true;

    if (!gstSetFile(file, context, false)) {
        return;
    }
}

void NPlaybackEngineGStreamer::nextMediaRespond(const QString &file, int context)
{
    if (!m_crossfading) {
        return;
    }
    m_bkpMedia = m_currentMedia;
    m_bkpContext = m_currentContext;

    gstSetFile(file, context, true);
}

qreal NPlaybackEngineGStreamer::speed() const
{
    return m_speed;
}

void NPlaybackEngineGStreamer::setSpeed(qreal speed)
{
    m_speed = speed;
    m_speedPostponed = true;
}

qreal NPlaybackEngineGStreamer::pitch() const
{
    return m_pitch;
}

void NPlaybackEngineGStreamer::setPitch(qreal pitch)
{
    if (!m_pitchElement) {
        return;
    }
    m_pitch = pitch;
    g_object_set(m_pitchElement, "pitch", m_pitch, NULL);
}

void NPlaybackEngineGStreamer::setVolume(qreal volume)
{
    m_volume = qBound(0.0, volume, 1.0);
    g_object_set(m_playbin, "volume", m_volume, NULL);
}

qreal NPlaybackEngineGStreamer::volume() const
{
    return m_volume;
}

void NPlaybackEngineGStreamer::setPosition(qreal pos)
{
    clearAnalyserQueues();
    if (!hasMedia() || pos < 0.0 || pos > 1.0) {
        return;
    }

    if (m_crossfading) {
        // abort cross-fading:
        if (!gstSetFile(m_bkpMedia, m_bkpContext, false)) {
            fail();
            return;
        }
        gst_element_set_state(m_playbin, GST_STATE_PLAYING);
    }
    m_position = pos;
    m_positionPostponed = true;
}

void NPlaybackEngineGStreamer::jump(qint64 msec)
{
    clearAnalyserQueues();
    if (!hasMedia()) {
        return;
    }

    if (m_crossfading) {
        // abort cross-fading:
        if (!gstSetFile(m_bkpMedia, m_bkpContext, false)) {
            fail();
            return;
        }
    }
    m_position += ((qreal)msec * NSEC_IN_MSEC) / m_durationNsec;
    m_positionPostponed = true;
}

qreal NPlaybackEngineGStreamer::position() const
{
    return m_position;
}

qint64 NPlaybackEngineGStreamer::durationMsec() const
{
    return m_durationNsec / NSEC_IN_MSEC;
}

void NPlaybackEngineGStreamer::play()
{
    if (!hasMedia()) {
        return;
    }

    m_gstBusPopTimer->start();
    m_checkStatusTimer->start(LONG_TICK_MSEC);
    gst_element_set_state(m_playbin, GST_STATE_PLAYING);
}

void NPlaybackEngineGStreamer::pause()
{
    clearAnalyserQueues();
    if (!hasMedia()) {
        return;
    }

    gst_element_set_state(m_playbin, GST_STATE_PAUSED);

    m_checkStatusTimer->stop();
    m_gstBusPopTimer->stop();

    m_gstState = GST_STATE_PAUSED;
    emit stateChanged(fromGstState(m_gstState));

    checkStatus();
}

void NPlaybackEngineGStreamer::stop()
{
    clearAnalyserQueues();
    m_crossfading = false;
    m_nextMediaRequestBlock = true;
    gst_element_set_state(m_playbin, GST_STATE_NULL);
    m_durationNsec = 0;
    m_position = 0.0;

    m_gstState = GST_STATE_NULL;
    emit stateChanged(N::PlaybackStopped);
    emit positionChanged(m_position);

    m_checkStatusTimer->stop();
    m_gstBusPopTimer->stop();
}

bool NPlaybackEngineGStreamer::hasMedia() const
{
    return !m_currentMedia.isEmpty();
}

QString NPlaybackEngineGStreamer::currentMedia() const
{
    return m_currentMedia;
}

N::PlaybackState NPlaybackEngineGStreamer::state() const
{
    return fromGstState(m_gstState);
}

void NPlaybackEngineGStreamer::processGstMessage(GstMessage *msg)
{
    //qDebug() << "message type:" << GST_MESSAGE_TYPE_NAME(msg);
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS: {
            stop();
            emit mediaFinished(m_currentMedia, m_currentContext);
            break;
        }
        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *err = NULL;
            gst_message_parse_error(msg, &err, &debug);
            g_free(debug);

            emit message(N::Critical, QFileInfo(m_currentMedia).absoluteFilePath(),
                         err ? QString::fromUtf8(err->message) : tr("Unknown error"));
            fail();
            if (err) {
                g_error_free(err);
            }
            break;
        }
        case GST_MESSAGE_ELEMENT: {
            if (m_spectrum && GST_MESSAGE_SRC(msg) == GST_OBJECT(m_spectrum)) {
                queueSpectrum(msg);
                break;
            }
            if (m_level && GST_MESSAGE_SRC(msg) == GST_OBJECT(m_level)) {
                queueLevels(msg);
                break;
            }
            if (gst_is_missing_plugin_message(msg)) {
                QString str = tr("Missing GStreamer plugin:<br/>");
                gchar *detail = gst_missing_plugin_message_get_installer_detail(msg);
                if (detail) {
                    QStringList fields = QString::fromUtf8(detail).split('|').mid(3);
                    str += QString::fromUtf8(detail).split('|').mid(3).join("<br/>");
                    g_free(detail);
                } else {
                    str += tr("Unknown plugin");
                }
                emit message(N::Critical, QFileInfo(m_currentMedia).absoluteFilePath(), str);
                fail();
            }
            break;
        }
        case GST_MESSAGE_DURATION_CHANGED: {
            m_durationNsec = GST_CLOCK_TIME_NONE;
            break;
        }
        case GST_MESSAGE_STREAM_START: {
            m_crossfading = false;
            m_nextMediaRequestBlock = false;
            if (m_speed != 1.0) {
                m_speedPostponed = true;
            }
            m_durationNsec = GST_CLOCK_TIME_NONE;
            emit mediaChanged(m_currentMedia, m_currentContext);
            break;
        }
        case GST_MESSAGE_PROPERTY_NOTIFY: {
            const gchar *name;
            const GValue *value;
            gst_message_parse_property_notify(msg, NULL, &name, &value);
            if (QString(name) == "volume") {
                gdouble gstVolume = g_value_get_double(value);
                if (gstVolume != m_volume) {
                    m_volume = gstVolume;
                    emit volumeChanged(m_volume);
                }
            }
            break;
        }
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_playbin)) {
                GstState newState, oldState, pendingState;
                gst_message_parse_state_changed(msg, &oldState, &newState, &pendingState);
                //qDebug() << "state changed old:" << gst_element_state_get_name(oldState)
                //         << " new:" << gst_element_state_get_name(newState)
                //         << " pending:" << gst_element_state_get_name(pendingState);
                if (newState != m_gstState) {
                    m_gstState = newState;
                    m_emitStateTimer->start();
                }
            }
            break;
        }
        default:
            break;
    }
}

void NPlaybackEngineGStreamer::checkStatus()
{
    if (!GST_CLOCK_TIME_IS_VALID(m_durationNsec)) {
        gst_element_query_duration(m_playbin, GST_FORMAT_TIME, &m_durationNsec);
    }

    if (GST_CLOCK_TIME_IS_VALID(m_durationNsec)) {
        gint64 gstPos = 0;
        if (gst_element_query_position(m_playbin, GST_FORMAT_TIME, &gstPos)) {
            if (!m_positionPostponed) {
                m_position = (qreal)gstPos / m_durationNsec;
                emit positionChanged(m_position);
            }
            emit tick(gstPos / NSEC_IN_MSEC); // the position in the track, at any speed
        }

        if (m_positionPostponed || m_speedPostponed) {
            if (m_positionPostponed) {
                gstPos = m_position * m_durationNsec;
            }
            gst_element_seek(m_playbin, m_speed, GST_FORMAT_TIME,
                             GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT |
                                          GST_SEEK_FLAG_SNAP_NEAREST),
                             GST_SEEK_TYPE_SET, gstPos, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
            m_positionPostponed = false;
            m_speedPostponed = false;
        }

        if (m_durationNsec / NSEC_IN_MSEC <= SHORT_TICK_THRESHOLD_MSEC) {
            m_checkStatusTimer->setInterval(SHORT_TICK_MSEC);
        }
    }
}

void NPlaybackEngineGStreamer::fail()
{
    stop();

    emit mediaFailed(m_currentMedia, m_currentContext);

    m_currentMedia = "";
    m_currentContext = 0;
}

void NPlaybackEngineGStreamer::_emitNextMediaRequest()
{
    m_crossfading = true;
    emit nextMediaRequested();
}

bool NPlaybackEngineGStreamer::_nextMediaRequestBlocked()
{
    return m_nextMediaRequestBlock;
}

// Turns an analyser's messages on or off; false if the element is missing.
static bool _setPostMessages(GstElement *element, bool enabled)
{
    if (!element) {
        return false;
    }
    g_object_set(element, "post-messages", enabled ? TRUE : FALSE, NULL);
    return enabled;
}

void NPlaybackEngineGStreamer::setSpectrumEnabled(bool enabled)
{
    m_spectrumEnabled = _setPostMessages(m_spectrum, enabled);
    updateBusPollInterval();
    if (!m_spectrumEnabled) {
        m_spectrumQueue.clear();
        m_spectrumRate = 0;
    }
}

void NPlaybackEngineGStreamer::setLevelsEnabled(bool enabled)
{
    m_levelsEnabled = _setPostMessages(m_level, enabled);
    updateBusPollInterval();
    if (!m_levelsEnabled) {
        m_levelQueue.clear();
    }
}

void NPlaybackEngineGStreamer::updateBusPollInterval()
{
    m_gstBusPopTimer->setInterval((m_spectrumEnabled || m_levelsEnabled)
                                      ? GST_BUS_POP_SPECTRUM_MSEC
                                      : GST_BUS_POP_MSEC);
}

void NPlaybackEngineGStreamer::setSpectrumFrameRate(int framesPerSecond)
{
    guint64 interval = GST_SECOND / qBound(15, framesPerSecond, 60);
    if (m_spectrum) {
        g_object_set(m_spectrum, "interval", interval, NULL);
    }
    if (m_level) {
        g_object_set(m_level, "interval", interval, NULL);
    }
}

// Analyser frames (spectrum, levels) are queued when the element measures
// them and released when their audio is heard.

// when a frame's audio is heard: the middle of the measured stretch
static GstClockTime _frameDue(const GstStructure *s)
{
    GstClockTime runningTime = 0, duration = 0;
    gst_structure_get_clock_time(s, "running-time", &runningTime);
    gst_structure_get_clock_time(s, "duration", &duration);
    return runningTime + duration / 2;
}

template <typename Frame>
static void _enqueue(QList<Frame> &queue, const Frame &frame)
{
    queue.append(frame);
    if (queue.size() > 120) { // ~2 s; something is off, don't grow forever
        queue.removeFirst();
    }
}

// How many frames at the front of the queue are due. After a seek the
// running time restarts, so frames from far ahead are dropped.
template <typename Frame>
static int _dueFrames(QList<Frame> &queue, GstClockTime now)
{
    int due = 0;
    while (due < queue.size() && queue.at(due).due <= now) {
        ++due;
    }
    if (due == 0 && !queue.isEmpty() && queue.first().due > now + 2 * GST_SECOND) {
        queue.clear();
    }
    return due;
}

bool NPlaybackEngineGStreamer::runningTime(GstClockTime *now) const
{
    GstClock *clock = gst_element_get_clock(m_playbin);
    if (!clock) {
        return false;
    }
    *now = gst_clock_get_time(clock) - gst_element_get_base_time(m_playbin);
    gst_object_unref(clock);
    return true;
}

// the level element posts its values as a GValueArray (or, in newer
// versions, a GstValueArray) of doubles, one per channel
static QVector<float> _levelValues(const GstStructure *s, const char *field)
{
    QVector<float> values;
    const GValue *v = gst_structure_get_value(s, field);
    if (!v) {
        return values;
    }
    if (GST_VALUE_HOLDS_ARRAY(v)) {
        guint n = gst_value_array_get_size(v);
        for (guint i = 0; i < n; ++i) {
            values.append((float)g_value_get_double(gst_value_array_get_value(v, i)));
        }
        return values;
    }
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    if (G_VALUE_HOLDS(v, G_TYPE_VALUE_ARRAY)) {
        GValueArray *array = (GValueArray *)g_value_get_boxed(v);
        for (guint i = 0; array && i < array->n_values; ++i) {
            values.append((float)g_value_get_double(g_value_array_get_nth(array, i)));
        }
    }
    G_GNUC_END_IGNORE_DEPRECATIONS
    return values;
}

void NPlaybackEngineGStreamer::queueLevels(GstMessage *msg)
{
    if (!m_levelsEnabled) {
        return;
    }
    const GstStructure *s = gst_message_get_structure(msg);
    if (!s || !gst_structure_has_name(s, "level")) {
        return;
    }
    LevelFrame frame;
    frame.due = _frameDue(s);
    frame.rms = _levelValues(s, "rms");
    frame.peak = _levelValues(s, "peak");
    if (frame.rms.isEmpty() || frame.rms.size() != frame.peak.size()) {
        return;
    }
    _enqueue(m_levelQueue, frame);
}

void NPlaybackEngineGStreamer::releaseLevels()
{
    GstClockTime now;
    if (!m_levelsEnabled || m_levelQueue.isEmpty() || !runningTime(&now)) {
        return;
    }
    int due = _dueFrames(m_levelQueue, now);
    if (due == 0) {
        return;
    }
    // the newest due frame, but with the loudest peak of the skipped ones
    // so short transients still reach the meter
    QVector<float> peak = m_levelQueue.at(0).peak;
    for (int i = 1; i < due; ++i) {
        const QVector<float> &p = m_levelQueue.at(i).peak;
        if (p.size() != peak.size()) {
            peak = p;
            continue;
        }
        for (int c = 0; c < p.size(); ++c) {
            peak[c] = qMax(peak.at(c), p.at(c));
        }
    }
    QVector<float> rms = m_levelQueue.at(due - 1).rms;
    m_levelQueue.erase(m_levelQueue.begin(), m_levelQueue.begin() + due);
    emit levelsChanged(rms, peak);
}

void NPlaybackEngineGStreamer::queueSpectrum(GstMessage *msg)
{
    if (!m_spectrumEnabled) {
        return;
    }
    const GstStructure *s = gst_message_get_structure(msg);
    if (!s || !gst_structure_has_name(s, "spectrum")) {
        return;
    }
    const GValue *list = gst_structure_get_value(s, "magnitude");
    if (!list || !GST_VALUE_HOLDS_LIST(list)) {
        return;
    }

    SpectrumFrame frame;
    frame.due = _frameDue(s);
    guint n = gst_value_list_get_size(list);
    frame.magnitudes.resize(n);
    for (guint i = 0; i < n; ++i) {
        frame.magnitudes[i] = g_value_get_float(gst_value_list_get_value(list, i));
    }

    if (!m_spectrumRate) {
        GstPad *pad = gst_element_get_static_pad(m_spectrum, "sink");
        if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
            gst_structure_get_int(gst_caps_get_structure(caps, 0), "rate", &m_spectrumRate);
            gst_caps_unref(caps);
        }
        gst_object_unref(pad);
    }
    _enqueue(m_spectrumQueue, frame);
}

void NPlaybackEngineGStreamer::releaseSpectrum()
{
    GstClockTime now;
    if (!m_spectrumEnabled || m_spectrumQueue.isEmpty() || !runningTime(&now)) {
        return;
    }
    // emit the newest frame that is due; older due frames are skipped
    int due = _dueFrames(m_spectrumQueue, now);
    if (due == 0) {
        return;
    }
    QVector<float> magnitudes = m_spectrumQueue.at(due - 1).magnitudes;
    m_spectrumQueue.erase(m_spectrumQueue.begin(), m_spectrumQueue.begin() + due);
    emit spectrumChanged(magnitudes, m_spectrumRate > 0 ? m_spectrumRate : 44100);
}

void NPlaybackEngineGStreamer::clearAnalyserQueues()
{
    m_spectrumQueue.clear();
    m_levelQueue.clear();
    m_spectrumRate = 0; // the next track may have a different rate
}
