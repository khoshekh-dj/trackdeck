/********************************************************************
**  TrackDeck, https://github.com/khoshekh-dj/trackdeck
**  Copyright (C) 2026 Khoshekh
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

#ifndef N_SPECTRUM_WIDGET_H
#define N_SPECTRUM_WIDGET_H

#include <QColor>
#include <QElapsedTimer>
#include <QPixmap>
#include <QVector>
#include <QWidget>

class QTimer;

// Live spectrum with falling peak caps, fed by the playback engine's
// spectrumChanged() signal. Bands are spaced logarithmically from 30 Hz to
// 16 kHz; the number of bars follows the widget's width.
class NSpectrumWidget : public QWidget
{
    Q_OBJECT
    // a skin can pin the style (e.g. "segments"); empty follows Preferences
    Q_PROPERTY(QString forced_style READ forcedStyle WRITE setForcedStyle)

public:
    // Appearance, as chosen in Preferences. String values match the settings.
    struct Appearance
    {
        QString style = "bars";         // bars, segments, mirrored, curve
        QString density = "normal";     // wide, normal, narrow
        QString sensitivity = "normal"; // low, normal, high
        QString trebleBoost = "normal"; // off, normal, strong
        QString fallSpeed = "normal";   // slow, normal, fast
        bool peaks = true;
        int frameRate = 30;
        QString colors = "waveform"; // waveform, accent, gradient
        QColor gradientFrom = QColor("#FF3D9A");
        QColor gradientTo = QColor("#3D8BFF");
    };

    NSpectrumWidget(QWidget *parent = 0);
    ~NSpectrumWidget() {}

    // the waveform's colour settings, used when colors == "waveform"
    void setColorScheme(bool rgb, const QString &scheme, const QColor &accent);
    void setAppearance(const Appearance &appearance);

    QString forcedStyle() const { return m_forcedStyle; }
    void setForcedStyle(const QString &style);

public slots:
    void setSpectrum(const QVector<float> &magnitudes, int sampleRate);
    void clear();

protected:
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *event);
    void moveEvent(QMoveEvent *event);
    void showEvent(QShowEvent *event);
    void changeEvent(QEvent *event);
    bool eventFilter(QObject *watched, QEvent *event);

private slots:
    void tick();
    void updateBackground();

private:
    int barCount() const;
    int barWidth() const;
    int barGap() const;
    QColor barColor(int bar, int bars) const;
    void resetBars(int n);
    void renderCache();
    void paintBars(QPainter &painter);
    void paintCurve(QPainter &painter);

    QTimer *m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastTick;
    qint64 m_lastFrame;

    QVector<float> m_target; // 0..1 per bar, from the latest frame
    QVector<float> m_level;  // what's drawn, eased towards m_target
    QVector<float> m_peak;
    QVector<float> m_peakHold; // seconds left before the cap starts falling

    bool m_rgb;
    QString m_scheme;
    QColor m_accent;
    Appearance m_appearance;
    QString m_userStyle;   // the style chosen in Preferences
    QString m_forcedStyle; // the skin's override, if any

    // bars pre-rendered at full height (lit and unlit); each frame copies
    // the visible part of each bar instead of painting gradients
    QPixmap m_litBars;
    QPixmap m_unlitBars;
    QVector<QColor> m_colors;
    bool m_cacheDirty;

    // What's behind the widget (album art, skin backgrounds), captured once.
    // Painting it ourselves lets the widget be opaque, so Qt doesn't redraw
    // every translucent layer behind it on every frame.
    QPixmap m_background;
    // m_background with the unlit bars already drawn on: one opaque copy per
    // frame instead of two full-size alpha blends
    QPixmap m_base;
    // curve style: gradient brushes are expensive to build (Qt computes a
    // colour table for each), so they're made once per size/colour change
    QBrush m_curveArea;
    QBrush m_curveLine;
    QBrush m_curvePeak;
    void scheduleBackgroundUpdate();
};

#endif
