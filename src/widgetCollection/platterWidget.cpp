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

#include "platterWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QtMath>

#include <cmath>

static const int ROWS = 4;
static const qreal ROW_PITCH[ROWS] = {6.0, 3.3, 0.0, -3.3}; // outermost first
static const int ROW_DOTS[ROWS] = {90, 92, 94, 96};
static const qreal DRIFT_DEG_PER_SEC = 3.5; // per % away from the row's pitch
static const qreal LABEL_RADIUS = 0.58;     // of the platter's radius
static const qreal MAT_RADIUS = 0.80;

NPlatterWidget::NPlatterWidget(QWidget *parent) : QWidget(parent)
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000 / 30);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(tick()));
    m_clock.start();
    m_lastTick = 0;
    m_playing = false;
    m_pitch = 0;
    for (int i = 0; i < ROWS; ++i) {
        m_angle[i] = i * 1.7; // rows don't line up exactly
    }
    m_rim = QColor("#B9BDC3");
    m_mat = QColor("#151517");
    m_dot = QColor("#FF8A1A");
    m_label = QColor("#2A2B2F");
}

void NPlatterWidget::setRimColor(const QColor &c)
{
    m_rim = c;
    renderStatic();
}

void NPlatterWidget::setMatColor(const QColor &c)
{
    m_mat = c;
    renderStatic();
}

void NPlatterWidget::setDotColor(const QColor &c)
{
    m_dot = c;
    update();
}

void NPlatterWidget::setLabelColor(const QColor &c)
{
    m_label = c;
    renderStatic();
}

void NPlatterWidget::setPixmap(const QPixmap &pixmap)
{
    m_cover = pixmap;
    renderStatic();
}

void NPlatterWidget::setPlaying(bool playing)
{
    m_playing = playing;
    if (playing && !m_timer->isActive()) {
        m_lastTick = m_clock.elapsed();
        m_timer->start();
    } else if (!playing) {
        m_timer->stop();
    }
    update();
}

void NPlatterWidget::setPitch(qreal percent)
{
    m_pitch = percent;
}

void NPlatterWidget::tick()
{
    qint64 now = m_clock.elapsed();
    qreal dt = qBound(0.0, (now - m_lastTick) / 1000.0, 0.1);
    m_lastTick = now;
    for (int i = 0; i < ROWS; ++i) {
        m_angle[i] += (m_pitch - ROW_PITCH[i]) * DRIFT_DEG_PER_SEC * dt;
        if (m_angle[i] > 360 || m_angle[i] < -360) {
            m_angle[i] = fmod(m_angle[i], 360.0);
        }
    }
    update();
}

void NPlatterWidget::resizeEvent(QResizeEvent *)
{
    renderStatic();
}

static QRectF _square(const QRect &r)
{
    int side = qMin(r.width(), r.height());
    return QRectF(r.x() + (r.width() - side) / 2.0, r.y() + (r.height() - side) / 2.0, side,
                  side);
}

void NPlatterWidget::renderStatic()
{
    QRectF sq = _square(rect());
    if (sq.width() < 8) {
        m_static = QPixmap();
        update();
        return;
    }
    qreal dpr = devicePixelRatioF();
    m_static = QPixmap(size() * dpr);
    m_static.setDevicePixelRatio(dpr);
    m_static.fill(Qt::transparent);

    QPainter p(&m_static);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPointF c = sq.center();
    qreal r = sq.width() / 2.0 - 1;

    // rim: brushed metal edge with a light from the top left
    QLinearGradient rim(sq.topLeft(), sq.bottomRight());
    rim.setColorAt(0, m_rim.lighter(125));
    rim.setColorAt(0.5, m_rim);
    rim.setColorAt(1, m_rim.darker(150));
    p.setPen(QPen(m_rim.darker(200), 1));
    p.setBrush(rim);
    p.drawEllipse(c, r, r);

    // rubber mat with faint rings
    qreal matR = r * MAT_RADIUS;
    p.setPen(Qt::NoPen);
    p.setBrush(m_mat);
    p.drawEllipse(c, matR, matR);
    p.setBrush(Qt::NoBrush);
    QColor ring = m_mat.lighter(160);
    ring.setAlpha(90);
    p.setPen(QPen(ring, 1));
    for (qreal k = 0.66; k < 0.99; k += 0.08) {
        p.drawEllipse(c, matR * k, matR * k);
    }

    // the label: the cover art, cut round
    qreal labelR = r * LABEL_RADIUS;
    QRectF labelRect(c.x() - labelR, c.y() - labelR, labelR * 2, labelR * 2);
    QPainterPath circle;
    circle.addEllipse(labelRect);
    p.setPen(Qt::NoPen);
    p.setBrush(m_label);
    p.drawPath(circle);
    if (!m_cover.isNull()) {
        p.save();
        p.setClipPath(circle);
        QSizeF s = m_cover.size();
        s.scale(labelRect.size(), Qt::KeepAspectRatioByExpanding);
        QRectF target(c.x() - s.width() / 2, c.y() - s.height() / 2, s.width(), s.height());
        p.drawPixmap(target, m_cover, QRectF(m_cover.rect()));
        p.restore();
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 120), 1.5));
    p.drawEllipse(labelRect);

    // spindle
    qreal spindle = qMax(2.5, r * 0.035);
    QRadialGradient metal(c - QPointF(spindle / 3, spindle / 3), spindle * 1.4);
    metal.setColorAt(0, QColor("#F4F5F7"));
    metal.setColorAt(1, QColor("#7D8188"));
    p.setPen(QPen(QColor(0, 0, 0, 140), 1));
    p.setBrush(metal);
    p.drawEllipse(c, spindle, spindle);

    update();
}

void NPlatterWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (!m_static.isNull()) {
        p.drawPixmap(0, 0, m_static);
    }
    QRectF sq = _square(rect());
    if (sq.width() < 8) {
        return;
    }
    p.setRenderHint(QPainter::Antialiasing);
    QPointF c = sq.center();
    qreal r = sq.width() / 2.0 - 1;

    // strobe dots between the mat and the edge; lit while playing, as if
    // the strobe light were on
    qreal band = r * (1.0 - MAT_RADIUS);
    qreal pitch = band / ROWS;
    qreal dotR = qMax(0.8, pitch * 0.28);
    QColor lit = m_dot;
    QColor unlit = m_rim.darker(170);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < ROWS; ++i) {
        qreal rowR = r - pitch * (i + 0.5) - 0.5;
        QColor color = unlit;
        if (m_playing) {
            // the row that matches the pitch glows brightest
            qreal off = qAbs(m_pitch - ROW_PITCH[i]);
            color = off < 0.4 ? lit.lighter(118) : lit;
        }
        p.setBrush(color);
        qreal step = 360.0 / ROW_DOTS[i];
        for (int d = 0; d < ROW_DOTS[i]; ++d) {
            qreal a = qDegreesToRadians(m_angle[i] + d * step);
            p.drawEllipse(QPointF(c.x() + rowR * qCos(a), c.y() + rowR * qSin(a)), dotR, dotR);
        }
    }
}
