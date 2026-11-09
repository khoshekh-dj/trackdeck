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

#include "spectrumWidget.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QEvent>
#include <QTimer>

#include <cmath>

static const double MIN_FREQ = 30.0;
static const double MAX_FREQ = 16000.0;
static const int MIN_BARS = 12;
static const int MAX_BARS = 96;
static const double PEAK_HOLD_SEC = 0.35;
static const qint64 FRAME_TIMEOUT_MSEC = 150; // no frames: paused or stopped
static const double CAP_HEIGHT = 2.0;
static const int SEGMENT_HEIGHT = 4; // LED style
static const int SEGMENT_GAP = 2;

// Tilted levels between these map onto the bar height. Higher sensitivity
// shifts the window down, so quieter material fills more of the height.
static void _range(const QString &sensitivity, double *floorDb, double *ceilingDb)
{
    if (sensitivity == "low") {
        *floorDb = -60;
        *ceilingDb = -20;
    } else if (sensitivity == "high") {
        *floorDb = -72;
        *ceilingDb = -32;
    } else {
        *floorDb = -66;
        *ceilingDb = -26;
    }
}

// Music loses roughly 5 dB per octave towards the top; tilting it back
// makes the treble bars move as much as the bass ones.
static double _tilt(const QString &boost)
{
    if (boost == "off") {
        return 0;
    }
    if (boost == "strong") {
        return 7.5;
    }
    return 5.0;
}

// bar height per second, and the same for the peak caps
static void _fall(const QString &speed, double *bars, double *peaks)
{
    if (speed == "slow") {
        *bars = 0.9;
        *peaks = 0.3;
    } else if (speed == "fast") {
        *bars = 2.8;
        *peaks = 1.0;
    } else {
        *bars = 1.6;
        *peaks = 0.55;
    }
}

NSpectrumWidget::NSpectrumWidget(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setMinimumSize(80, 40);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_rgb = true;
    m_scheme = "rgb";
    m_accent = QColor("#3D8BFF");

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(1000 / 30);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(tick()));
    m_clock.start();
    m_lastTick = 0;
    m_lastFrame = -FRAME_TIMEOUT_MSEC;
    m_cacheDirty = true;
}

void NSpectrumWidget::scheduleBackgroundUpdate()
{
    // until the new snapshot exists, let Qt paint what's behind us as usual
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    m_background = QPixmap();
    m_base = QPixmap();
    QTimer::singleShot(0, this, SLOT(updateBackground()));
}

void NSpectrumWidget::updateBackground()
{
    if (!isVisible() || size().isEmpty()) {
        return;
    }
    qreal dpr = devicePixelRatioF();
    QPixmap background(size() * dpr);
    background.setDevicePixelRatio(dpr);
    background.fill(Qt::transparent);

    // render each ancestor on its own (no children), outermost first
    QList<QWidget *> chain;
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        chain.prepend(w);
        if (w->isWindow()) {
            break;
        }
    }
    QPainter painter(&background);
    for (QWidget *w : chain) {
        QPoint offset = mapTo(w, QPoint(0, 0));
        w->render(&painter, QPoint(0, 0), QRegion(QRect(offset, size())),
                  QWidget::DrawWindowBackground);
    }
    painter.end();

    m_background = background;
    m_cacheDirty = true; // recompose m_base
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    update();
}

void NSpectrumWidget::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    scheduleBackgroundUpdate();
}

void NSpectrumWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // the skin's script swaps the window's style sheet (album art, accent
    // colour); watch the ancestors so the snapshot follows
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        w->removeEventFilter(this);
        w->installEventFilter(this);
        if (w->isWindow()) {
            break;
        }
    }
    scheduleBackgroundUpdate();
}

void NSpectrumWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        scheduleBackgroundUpdate();
    }
}

bool NSpectrumWidget::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
        case QEvent::StyleChange:
        case QEvent::PaletteChange:
        case QEvent::Resize:
            scheduleBackgroundUpdate();
            break;
        default:
            break;
    }
    return QWidget::eventFilter(watched, event);
}

int NSpectrumWidget::barWidth() const
{
    if (m_appearance.density == "wide") {
        return 14;
    }
    if (m_appearance.density == "narrow") {
        return 5;
    }
    return 9;
}

int NSpectrumWidget::barGap() const
{
    if (m_appearance.density == "wide") {
        return 4;
    }
    if (m_appearance.density == "narrow") {
        return 2;
    }
    return 3;
}

int NSpectrumWidget::barCount() const
{
    int n = (width() + barGap()) / (barWidth() + barGap());
    return qBound(MIN_BARS, n, MAX_BARS);
}

void NSpectrumWidget::resetBars(int n)
{
    m_target.fill(0, n);
    m_level.fill(0, n);
    m_peak.fill(0, n);
    m_peakHold.fill(0, n);
    m_cacheDirty = true;
}

void NSpectrumWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (barCount() != m_level.size()) {
        resetBars(barCount());
    }
    m_cacheDirty = true;
    scheduleBackgroundUpdate();
}

void NSpectrumWidget::setColorScheme(bool rgb, const QString &scheme, const QColor &accent)
{
    m_rgb = rgb;
    m_scheme = scheme;
    if (accent.isValid()) {
        m_accent = accent;
    }
    m_cacheDirty = true;
    update();
}

void NSpectrumWidget::setAppearance(const Appearance &appearance)
{
    m_appearance = appearance;
    m_userStyle = appearance.style;
    if (!m_forcedStyle.isEmpty()) {
        m_appearance.style = m_forcedStyle;
    }
    m_timer->setInterval(1000 / qBound(15, appearance.frameRate, 60));
    if (barCount() != m_level.size()) {
        resetBars(barCount());
    }
    m_cacheDirty = true;
    update();
}

void NSpectrumWidget::setForcedStyle(const QString &style)
{
    m_forcedStyle = style;
    m_appearance.style = style.isEmpty() ? m_userStyle : style;
    m_cacheDirty = true;
    update();
}

void NSpectrumWidget::setSpectrum(const QVector<float> &magnitudes, int sampleRate)
{
    if (!isVisible()) { // switched off, but a level meter keeps the data coming
        return;
    }
    int n = barCount();
    if (m_target.size() != n) {
        resetBars(n);
    }
    int bins = magnitudes.size();
    if (bins < 2 || sampleRate <= 0) {
        return;
    }
    double floorDb, ceilingDb;
    _range(m_appearance.sensitivity, &floorDb, &ceilingDb);
    double tilt = _tilt(m_appearance.trebleBoost);
    double binWidth = sampleRate / 2.0 / bins;
    double top = qMin(MAX_FREQ, sampleRate / 2.0);

    for (int b = 0; b < n; ++b) {
        double f0 = MIN_FREQ * std::pow(top / MIN_FREQ, (double)b / n);
        double f1 = MIN_FREQ * std::pow(top / MIN_FREQ, (double)(b + 1) / n);
        int i0 = qBound(0, (int)std::floor(f0 / binWidth), bins - 1);
        int i1 = qBound(i0 + 1, (int)std::ceil(f1 / binWidth), bins);
        // loudest bin in the band (narrow bass bands fall within one bin)
        float db = -200;
        for (int i = i0; i < i1; ++i) {
            db = qMax(db, magnitudes.at(i));
        }
        double tilted = db + tilt * std::log2(std::sqrt(f0 * f1) / 1000.0);
        m_target[b] = (float)qBound(0.0, (tilted - floorDb) / (ceilingDb - floorDb), 1.0);
    }

    m_lastFrame = m_clock.elapsed();
    if (!m_timer->isActive()) {
        m_lastTick = m_clock.elapsed();
        m_timer->start();
    }
}

void NSpectrumWidget::clear()
{
    m_target.fill(0);
    m_lastFrame = -FRAME_TIMEOUT_MSEC;
}

void NSpectrumWidget::tick()
{
    qint64 now = m_clock.elapsed();
    double dt = qBound(0.0, (now - m_lastTick) / 1000.0, 0.1);
    m_lastTick = now;

    if (now - m_lastFrame > FRAME_TIMEOUT_MSEC) { // paused or stopped: let the bars drop
        m_target.fill(0);
    }
    double barFall, peakFall;
    _fall(m_appearance.fallSpeed, &barFall, &peakFall);

    bool active = false;
    for (int b = 0; b < m_level.size(); ++b) {
        float target = m_target.at(b);
        float level = m_level.at(b);
        if (target > level) {
            level += (target - level) * 0.7f; // quick, but not a hard jump
        } else {
            level = qMax(target, level - (float)(barFall * dt));
        }
        m_level[b] = level;

        if (level >= m_peak.at(b)) {
            m_peak[b] = level;
            m_peakHold[b] = PEAK_HOLD_SEC;
        } else if (m_peakHold.at(b) > 0) {
            m_peakHold[b] -= dt;
        } else {
            m_peak[b] = qMax(level, m_peak.at(b) - (float)(peakFall * dt));
        }
        if (level > 0.001f || m_peak.at(b) > 0.001f) {
            active = true;
        }
    }

    update();
    if (!active && now - m_lastFrame > FRAME_TIMEOUT_MSEC) {
        m_timer->stop();
    }
}

QColor NSpectrumWidget::barColor(int bar, int bars) const
{
    double t = bars > 1 ? (double)bar / (bars - 1) : 0;
    if (m_appearance.colors == "gradient") {
        const QColor &a = m_appearance.gradientFrom;
        const QColor &b = m_appearance.gradientTo;
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                                a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF() + (b.blueF() - a.blueF()) * t);
    }
    if (m_appearance.colors == "accent" || !m_rgb || m_scheme == "accent") {
        return m_accent;
    }
    // bass red, mids green, highs blue, as on the waveform
    double saturation = m_scheme == "vivid" ? 0.95 : 0.72;
    return QColor::fromHsvF(0.66 * t, saturation, 1.0);
}

// Pre-renders every bar at full height, lit and unlit, in the current style.
void NSpectrumWidget::renderCache()
{
    m_cacheDirty = false;
    int n = m_level.size();
    qreal dpr = devicePixelRatioF();
    QSize size = this->size() * dpr;
    m_colors.resize(n);
    for (int b = 0; b < n; ++b) {
        m_colors[b] = barColor(b, n);
    }
    if (m_appearance.style == "curve" && n > 0) {
        auto gradient = [&](qreal alpha) {
            QLinearGradient across(0, 0, width(), 0);
            for (int b = 0; b < n; ++b) {
                QColor c = m_colors.at(b);
                c.setAlphaF(alpha);
                across.setColorAt(n > 1 ? (double)b / (n - 1) : 0, c);
            }
            return QBrush(across);
        };
        m_curveArea = gradient(0.35);
        m_curveLine = gradient(1.0);
        m_curvePeak = gradient(0.5);
    }
    if (n == 0 || size.isEmpty() || m_appearance.style == "curve") {
        m_litBars = QPixmap();
        m_unlitBars = QPixmap();
        m_base = m_background;
        return;
    }

    m_litBars = QPixmap(size);
    m_unlitBars = QPixmap(size);
    m_litBars.setDevicePixelRatio(dpr);
    m_unlitBars.setDevicePixelRatio(dpr);
    m_litBars.fill(Qt::transparent);
    m_unlitBars.fill(Qt::transparent);

    QPainter lit(&m_litBars);
    QPainter unlit(&m_unlitBars);
    lit.setRenderHint(QPainter::Antialiasing);
    unlit.setRenderHint(QPainter::Antialiasing);
    lit.setPen(Qt::NoPen);
    unlit.setPen(Qt::NoPen);

    bool mirrored = m_appearance.style == "mirrored";
    bool segments = m_appearance.style == "segments";
    double slot = (double)(width() + barGap()) / n;
    double w = qMax(2.0, slot - barGap());
    double h = height();

    for (int b = 0; b < n; ++b) {
        QColor color = m_colors.at(b);
        QColor track = color;
        track.setAlphaF(segments ? 0.10 : 0.07);
        QColor dim = color;
        dim.setAlphaF(0.55);

        // brightest at the tips: the top for bars, both ends when mirrored
        QLinearGradient gradient(0, h, 0, 0);
        if (mirrored) {
            gradient.setColorAt(0, color);
            gradient.setColorAt(0.5, dim);
            gradient.setColorAt(1, color);
        } else {
            gradient.setColorAt(0, dim);
            gradient.setColorAt(1, color);
        }
        lit.setBrush(gradient);
        unlit.setBrush(track);

        double x = b * slot;
        if (segments) {
            for (double y = h - SEGMENT_HEIGHT; y >= 0; y -= SEGMENT_HEIGHT + SEGMENT_GAP) {
                QRectF rect(x, y, w, SEGMENT_HEIGHT);
                unlit.drawRoundedRect(rect, 1, 1);
                lit.drawRoundedRect(rect, 1, 1);
            }
        } else {
            QRectF rect(x, 0, w, h);
            unlit.drawRoundedRect(rect, 2, 2);
            lit.drawRoundedRect(rect, 2, 2);
        }
    }

    lit.end();
    unlit.end();

    m_base = QPixmap();
    if (!m_background.isNull()) {
        m_base = m_background;
        QPainter base(&m_base);
        base.drawPixmap(0, 0, m_unlitBars);
    }
}

void NSpectrumWidget::paintBars(QPainter &painter)
{
    int n = m_level.size();
    if (!m_base.isNull()) {
        // opaque: a straight copy, no blending
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawPixmap(0, 0, m_base);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    } else {
        painter.drawPixmap(0, 0, m_unlitBars);
    }

    bool mirrored = m_appearance.style == "mirrored";
    bool segments = m_appearance.style == "segments";
    qreal dpr = m_litBars.devicePixelRatio();
    double slot = (double)(width() + barGap()) / n;
    double w = qMax(2.0, slot - barGap());
    double h = height();
    double pitch = SEGMENT_HEIGHT + SEGMENT_GAP;
    double usable = mirrored ? h / 2 - CAP_HEIGHT - 1 : h - CAP_HEIGHT - 2;

    auto blit = [&](const QRectF &target) {
        QRectF source(target.x() * dpr, target.y() * dpr, target.width() * dpr,
                      target.height() * dpr);
        painter.drawPixmap(target, m_litBars, source);
    };

    for (int b = 0; b < n; ++b) {
        double x = b * slot;
        double length = m_level.at(b) * usable;
        double peak = m_peak.at(b) * usable;
        if (segments) { // light whole segments only
            length = std::ceil(length / pitch) * pitch;
            peak = std::ceil(peak / pitch) * pitch;
        }

        if (length >= 1) {
            if (mirrored) {
                blit(QRectF(x, h / 2 - length, w + 1, length));
                blit(QRectF(x, h / 2, w + 1, length));
            } else {
                blit(QRectF(x, h - length, w + 1, length));
            }
        }

        if (!m_appearance.peaks || m_peak.at(b) <= 0.01) {
            continue;
        }
        QColor cap = m_colors.at(b).lighter(135);
        if (segments) { // the peak is one lit segment
            double y = h - peak;
            if (peak > length) {
                blit(QRectF(x, y, w + 1, pitch));
            }
        } else if (mirrored) {
            painter.fillRect(QRectF(x, h / 2 - peak - CAP_HEIGHT - 1, w, CAP_HEIGHT), cap);
            painter.fillRect(QRectF(x, h / 2 + peak + 1, w, CAP_HEIGHT), cap);
        } else {
            painter.fillRect(QRectF(x, h - peak - CAP_HEIGHT - 1, w, CAP_HEIGHT), cap);
        }
    }
}

// Points along a smooth curve through the bar centres (Catmull-Rom, sampled).
static QVector<QPointF> _curvePoints(const QVector<float> &values, double width, double h,
                                     double usable)
{
    int n = values.size();
    double slot = width / n;
    QVector<QPointF> p(n);
    for (int b = 0; b < n; ++b) {
        // baseline 3 px up, so the line stays visible on silent bands
        p[b] = QPointF((b + 0.5) * slot, h - 3 - values.at(b) * (usable - 3));
    }
    const int steps = 4;
    QVector<QPointF> out;
    out.reserve(n * steps + 2);
    out << QPointF(0, p.first().y());
    for (int b = 0; b + 1 < n; ++b) {
        QPointF p0 = p.at(qMax(0, b - 1)), p1 = p.at(b), p2 = p.at(b + 1),
                p3 = p.at(qMin(n - 1, b + 2));
        for (int i = 0; i < steps; ++i) {
            double t = (double)i / steps, t2 = t * t, t3 = t2 * t;
            out << 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                          (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
        }
    }
    out << p.last() << QPointF(width, p.last().y());
    return out;
}

// A line drawn as a thin filled band: much cheaper than stroking a path.
static QPolygonF _band(const QVector<QPointF> &points, double thickness)
{
    QPolygonF band;
    band.reserve(points.size() * 2);
    for (const QPointF &pt : points) {
        band << pt;
    }
    for (int i = points.size() - 1; i >= 0; --i) {
        band << points.at(i) + QPointF(0, thickness);
    }
    return band;
}

void NSpectrumWidget::paintCurve(QPainter &painter)
{
    double w = width();
    double h = height();
    double usable = h - CAP_HEIGHT - 2;
    int n = m_level.size();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    QVector<QPointF> line = _curvePoints(m_level, w, h, usable);
    QPolygonF area(line);
    area << QPointF(w, h) << QPointF(0, h);
    // the area's edge is hidden under the line, so it needs no antialiasing
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(m_curveArea);
    painter.drawPolygon(area);

    painter.setBrush(m_curveLine);
    painter.drawPolygon(_band(line, 2.0));

    if (m_appearance.peaks) {
        painter.setBrush(m_curvePeak);
        painter.drawPolygon(_band(_curvePoints(m_peak, w, h, usable), 1.0));
    }
}

void NSpectrumWidget::paintEvent(QPaintEvent *)
{
    if (m_level.isEmpty()) {
        return;
    }
    if (m_cacheDirty) {
        renderCache();
    }
    QPainter painter(this);
    if (m_appearance.style == "curve" && !m_background.isNull()) {
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawPixmap(0, 0, m_background);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    if (m_appearance.style == "curve") {
        paintCurve(painter);
    } else if (!m_litBars.isNull()) {
        paintBars(painter);
    }
}
