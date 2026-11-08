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

#ifndef N_LEVEL_METER_H
#define N_LEVEL_METER_H

#include <QColor>
#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

class QTimer;

// A vertical LED level meter, like a mixer's channel meter. It sums the
// playback engine's spectrum frames into one overall level, so it moves with
// the music the same way the visualiser does. The level is the mix of both
// channels, not left and right separately.
class NLevelMeter : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int segments READ segments WRITE setSegments)
    Q_PROPERTY(int segment_gap READ segmentGap WRITE setSegmentGap)
    Q_PROPERTY(QColor low_color READ lowColor WRITE setLowColor)
    Q_PROPERTY(QColor mid_color READ midColor WRITE setMidColor)
    Q_PROPERTY(QColor high_color READ highColor WRITE setHighColor)
    Q_PROPERTY(QColor off_color READ offColor WRITE setOffColor)
    Q_PROPERTY(int mid_from READ midFrom WRITE setMidFrom)
    Q_PROPERTY(int high_from READ highFrom WRITE setHighFrom)

public:
    NLevelMeter(QWidget *parent = 0);

    int segments() const { return m_segments; }
    void setSegments(int n);
    int segmentGap() const { return m_gap; }
    void setSegmentGap(int gap);
    QColor lowColor() const { return m_low; }
    void setLowColor(const QColor &c);
    QColor midColor() const { return m_mid; }
    void setMidColor(const QColor &c);
    QColor highColor() const { return m_high; }
    void setHighColor(const QColor &c);
    QColor offColor() const { return m_off; }
    void setOffColor(const QColor &c);
    // the first segment (counted from the bottom, from 0) in each colour
    int midFrom() const { return m_midFrom; }
    void setMidFrom(int n);
    int highFrom() const { return m_highFrom; }
    void setHighFrom(int n);

    QSize sizeHint() const;
    QSize minimumSizeHint() const;

public slots:
    void setSpectrum(const QVector<float> &magnitudes, int sampleRate);
    void clear();

protected:
    void paintEvent(QPaintEvent *event);

private slots:
    void tick();

private:
    QTimer *m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastTick;
    qint64 m_lastFrame;

    float m_target; // 0..1
    float m_level;
    float m_peak;
    double m_peakHold;

    int m_segments;
    int m_gap;
    int m_midFrom;
    int m_highFrom;
    QColor m_low;
    QColor m_mid;
    QColor m_high;
    QColor m_off;
};

#endif
