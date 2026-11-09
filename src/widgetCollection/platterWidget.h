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

#ifndef N_PLATTER_WIDGET_H
#define N_PLATTER_WIDGET_H

#include <QColor>
#include <QElapsedTimer>
#include <QPixmap>
#include <QWidget>

class QTimer;

// A turntable platter seen from above: the cover art as the record label,
// a rubber mat, and four rows of strobe dots round the edge. As on a real
// deck, each row stands still at one pitch (+6, +3.3, 0 and -3.3 %) and
// drifts at any other, so the dots show the pitch while playing.
class NPlatterWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor rim_color READ rimColor WRITE setRimColor)
    Q_PROPERTY(QColor mat_color READ matColor WRITE setMatColor)
    Q_PROPERTY(QColor dot_color READ dotColor WRITE setDotColor)
    Q_PROPERTY(QColor label_color READ labelColor WRITE setLabelColor)

public:
    NPlatterWidget(QWidget *parent = 0);

    QColor rimColor() const { return m_rim; }
    void setRimColor(const QColor &c);
    QColor matColor() const { return m_mat; }
    void setMatColor(const QColor &c);
    QColor dotColor() const { return m_dot; }
    void setDotColor(const QColor &c);
    QColor labelColor() const { return m_label; }
    void setLabelColor(const QColor &c);

    bool hasHeightForWidth() const { return true; }
    int heightForWidth(int width) const { return width; }
    QSize sizeHint() const { return QSize(200, 200); }

public slots:
    void setPixmap(const QPixmap &pixmap); // a null pixmap shows a plain label
    void setPlaying(bool playing);
    void setPitch(qreal percent);

protected:
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *event);

private slots:
    void tick();

private:
    void renderStatic();

    QTimer *m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastTick;
    bool m_playing;
    qreal m_pitch;
    qreal m_angle[4]; // degrees, per strobe row

    QPixmap m_cover;
    QPixmap m_static; // rim, mat and label, rendered once per size/cover
    QColor m_rim;
    QColor m_mat;
    QColor m_dot;
    QColor m_label;
};

#endif
