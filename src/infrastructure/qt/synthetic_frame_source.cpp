#include "synthetic_frame_source.h"

#include "domain/plate_format.h"
#include "qt_image.h"

#include <QFont>
#include <QImage>
#include <QPainter>

namespace vpr::qt {

SyntheticFrameSource::SyntheticFrameSource(std::shared_ptr<const sim::SyntheticScene> scene, int frameStepMs,
                                           int timerIntervalMs, QObject* parent)
    : QObject(parent), m_scene(std::move(scene)), m_frameStepMs(frameStepMs), m_timer(this)
{
    m_timer.setInterval(timerIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &SyntheticFrameSource::tick);
}

void SyntheticFrameSource::start(FrameHandler onFrame, ErrorHandler, FinishedHandler onFinished)
{
    m_onFrame = std::move(onFrame);
    m_onFinished = std::move(onFinished);
    m_index = 0;
    m_timer.start();
}

void SyntheticFrameSource::stop()
{
    m_timer.stop();
}

void SyntheticFrameSource::tick()
{
    const std::int64_t ms = m_index * m_frameStepMs;
    // Run past the last vehicle, so the tracker has time to close its tracks.
    if (ms > m_scene->durationMs() + 1500) {
        m_timer.stop();
        if (m_onFinished)
            m_onFinished();
        return;
    }

    QImage image(m_scene->width(), m_scene->height(), QImage::Format_RGB888);
    image.fill(QColor(70, 74, 80));
    QPainter painter(&image);
    painter.setPen(QPen(QColor(210, 210, 210), 2, Qt::DashLine));
    for (const double x : {0.18, 0.38, 0.58, 0.78})
        painter.drawLine(QPointF(x * image.width(), 0), QPointF(x * image.width(), image.height()));

    for (const sim::VehiclePose& pose : m_scene->posesAt(ms)) {
        const QRectF body(pose.box.x, pose.box.y, pose.box.w, pose.box.h);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(40, 90, 150));
        painter.drawRoundedRect(body, 12, 12);
        painter.setBrush(QColor(170, 205, 230));
        painter.drawRect(QRectF(body.x() + body.width() * 0.15, body.y() + body.height() * 0.15,
                                body.width() * 0.7, body.height() * 0.3));
        if (pose.vehicle->plateVisible) {
            const QRectF plate(pose.plateBox.x, pose.plateBox.y, pose.plateBox.w, pose.plateBox.h);
            painter.setBrush(Qt::white);
            painter.drawRect(plate);
            painter.setPen(Qt::black);
            QFont font = painter.font();
            font.setPixelSize(static_cast<int>(plate.height() * 0.6));
            painter.setFont(font);
            painter.drawText(plate, Qt::AlignCenter, QString::fromStdString(pose.vehicle->plate));
        }
    }
    painter.end();

    if (m_onFrame) {
        const std::int64_t index = m_index;
        m_onFrame(Frame{toDomainImage(image, ms), index});
    }
    ++m_index;
}

} // namespace vpr::qt
