/********************************************************************
**  TrackDeck, https://github.com/khoshekh-dj/trackdeck
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

#include "levelMeter.h"

#include <cmath>

#include <QPainter>
#include <QTimer>

// dBFS between these maps onto the meter's height
static const double FLOOR_DB = -40;
static const double CEILING_DB = 0;

static float _scale(float db)
{
    return (float)qBound(0.0, (db - FLOOR_DB) / (CEILING_DB - FLOOR_DB), 1.0);
}
static const double FALL_PER_SEC = 1.2;  // of the full height
static const double PEAK_HOLD_SEC = 0.8;
static const double PEAK_FALL_PER_SEC = 0.6;
static const qint64 FRAME_TIMEOUT_MSEC = 150; // no frames: paused or stopped

NLevelMeter::NLevelMeter(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000 / 30);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(tick()));
    m_clock.start();
    m_lastTick = 0;
    m_lastFrame = -FRAME_TIMEOUT_MSEC;

    m_channel = 0;
    m_target = 0;
    m_peakTarget = 0;
    m_level = 0;
    m_peak = 0;
    m_peakHold = 0;

    m_segments = 16;
    m_gap = 2;
    m_midFrom = 10;
    m_highFrom = 14;
    m_low = QColor("#3DDC84");
    m_mid = QColor("#FFB020");
    m_high = QColor("#FF3B3B");
    m_off = QColor("#16181C");
}

void NLevelMeter::setSegments(int n)
{
    m_segments = qBound(2, n, 64);
    update();
}

void NLevelMeter::setSegmentGap(int gap)
{
    m_gap = qMax(0, gap);
    update();
}

void NLevelMeter::setLowColor(const QColor &c)
{
    m_low = c;
    update();
}

void NLevelMeter::setMidColor(const QColor &c)
{
    m_mid = c;
    update();
}

void NLevelMeter::setHighColor(const QColor &c)
{
    m_high = c;
    update();
}

void NLevelMeter::setOffColor(const QColor &c)
{
    m_off = c;
    update();
}

void NLevelMeter::setMidFrom(int n)
{
    m_midFrom = n;
    update();
}

void NLevelMeter::setHighFrom(int n)
{
    m_highFrom = n;
    update();
}

QSize NLevelMeter::sizeHint() const
{
    return QSize(14, 160);
}

QSize NLevelMeter::minimumSizeHint() const
{
    return QSize(6, 40);
}

void NLevelMeter::setLevels(const QVector<float> &rms, const QVector<float> &peak)
{
    if (!isVisible() || rms.isEmpty() || peak.size() != rms.size()) {
        return;
    }
    // a mono track shows the same level on both meters
    int c = qMin(m_channel, rms.size() - 1);
    m_target = _scale(rms.at(c));
    m_peakTarget = qMax(m_peakTarget, _scale(peak.at(c)));

    m_lastFrame = m_clock.elapsed();
    if (!m_timer->isActive()) {
        m_lastTick = m_clock.elapsed();
        m_timer->start();
    }
}

void NLevelMeter::clear()
{
    m_target = 0;
    m_peakTarget = 0;
    m_lastFrame = -FRAME_TIMEOUT_MSEC;
}

void NLevelMeter::tick()
{
    qint64 now = m_clock.elapsed();
    double dt = qBound(0.0, (now - m_lastTick) / 1000.0, 0.1);
    m_lastTick = now;

    if (now - m_lastFrame > FRAME_TIMEOUT_MSEC) {
        m_target = 0;
        m_peakTarget = 0;
    }
    // fast attack, steady fall, like a hardware meter
    if (m_target > m_level) {
        m_level += (m_target - m_level) * 0.6f;
    } else {
        m_level = qMax(m_target, m_level - (float)(FALL_PER_SEC * dt));
    }
    float peakNow = qMax(m_level, m_peakTarget);
    m_peakTarget = 0; // taken; the next frames bring new peaks
    if (peakNow >= m_peak) {
        m_peak = peakNow;
        m_peakHold = PEAK_HOLD_SEC;
    } else if (m_peakHold > 0) {
        m_peakHold -= dt;
    } else {
        m_peak = qMax(m_level, m_peak - (float)(PEAK_FALL_PER_SEC * dt));
    }

    update();
    if (m_level < 0.001f && m_peak < 0.001f && now - m_lastFrame > FRAME_TIMEOUT_MSEC) {
        m_timer->stop();
    }
}

void NLevelMeter::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QRect r = contentsRect();
    if (r.height() < m_segments) {
        return;
    }
    double step = (r.height() + m_gap) / (double)m_segments;
    int lit = qRound(m_level * m_segments);
    int peak = m_peak > 0.001f ? qBound(0, (int)std::ceil(m_peak * m_segments) - 1, m_segments - 1)
                               : -1;
    for (int i = 0; i < m_segments; ++i) {
        // segment 0 at the bottom
        int y0 = r.bottom() + 1 - qRound((i + 1) * step) + m_gap;
        int y1 = r.bottom() + 1 - qRound(i * step);
        QRect seg(r.left(), y0, r.width(), qMax(1, y1 - y0));
        QColor on = i >= m_highFrom ? m_high : (i >= m_midFrom ? m_mid : m_low);
        if (i < lit || i == peak) {
            painter.fillRect(seg, on);
        } else {
            QColor dim = on;
            dim.setAlphaF(0.2);
            painter.fillRect(seg, m_off);
            painter.fillRect(seg, dim);
        }
    }
}
