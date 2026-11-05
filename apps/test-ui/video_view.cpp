#include "video_view.h"

#include <QLineF>
#include <QMouseEvent>
#include <QPainter>

VideoView::VideoView(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(480, 270);
}

void VideoView::showFrame(const QImage& image, const QVector<vpr::Track>& tracks)
{
    m_image = image;
    m_tracks = tracks;
    update();
}

void VideoView::clear()
{
    m_image = QImage();
    m_tracks.clear();
    update();
}

QRectF VideoView::imageRect() const
{
    const QSizeF source = m_image.isNull() ? QSizeF(16, 9) : QSizeF(m_image.size());
    const QSizeF fitted = source.scaled(QSizeF(size()), Qt::KeepAspectRatio);
    return {(width() - fitted.width()) / 2.0, (height() - fitted.height()) / 2.0, fitted.width(), fitted.height()};
}

void VideoView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 24, 28));
    const QRectF area = imageRect();
    if (!m_image.isNull())
        painter.drawImage(area, m_image);

    painter.setRenderHint(QPainter::Antialiasing);
    if (!m_image.isNull()) {
        const double scale = area.width() / m_image.width();
        for (const vpr::Track& track : m_tracks) {
            const QRectF box(area.x() + track.box.x * scale, area.y() + track.box.y * scale,
                             track.box.w * scale, track.box.h * scale);
            painter.setPen(QPen(QColor(90, 220, 120), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box);
            painter.drawText(box.topLeft() + QPointF(4, -4), QStringLiteral("#%1").arg(track.id));
        }
    }

    const QPointF a(area.x() + m_a.x() * area.width(), area.y() + m_a.y() * area.height());
    const QPointF b(area.x() + m_b.x() * area.width(), area.y() + m_b.y() * area.height());
    painter.setPen(QPen(QColor(255, 190, 40), 3));
    painter.drawLine(a, b);
    painter.setBrush(QColor(255, 190, 40));
    painter.drawEllipse(a, 7, 7);
    painter.drawEllipse(b, 7, 7);
}

void VideoView::mousePressEvent(QMouseEvent* event)
{
    const QRectF area = imageRect();
    const QPointF a(area.x() + m_a.x() * area.width(), area.y() + m_a.y() * area.height());
    const QPointF b(area.x() + m_b.x() * area.width(), area.y() + m_b.y() * area.height());
    const QPointF p = event->position();
    if (QLineF(p, a).length() < 16)
        m_dragging = &m_a;
    else if (QLineF(p, b).length() < 16)
        m_dragging = &m_b;
}

void VideoView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging)
        return;
    const QRectF area = imageRect();
    const QPointF p = event->position();
    m_dragging->setX(qBound(0.0, (p.x() - area.x()) / area.width(), 1.0));
    m_dragging->setY(qBound(0.0, (p.y() - area.y()) / area.height(), 1.0));
    update();
}

void VideoView::mouseReleaseEvent(QMouseEvent*)
{
    if (m_dragging) {
        m_dragging = nullptr;
        emit lineChanged(m_a.x(), m_a.y(), m_b.x(), m_b.y());
    }
}

void VideoView::setLine(QPointF a, QPointF b)
{
    m_a = a;
    m_b = b;
    update();
    emit lineChanged(m_a.x(), m_a.y(), m_b.x(), m_b.y());
}
