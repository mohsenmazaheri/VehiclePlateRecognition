#include "media_frame_sources.h"

#include "qt_image.h"

#include <QCamera>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QVideoSink>

namespace vpr::qt {

MediaUrlFrameSource::MediaUrlFrameSource(QUrl url, QObject* parent)
    : QObject(parent), m_url(std::move(url))
{
}

MediaUrlFrameSource::~MediaUrlFrameSource()
{
    MediaUrlFrameSource::stop();
}

void MediaUrlFrameSource::start(FrameHandler onFrame, ErrorHandler onError, FinishedHandler onFinished)
{
    stop();
    m_onFrame = std::move(onFrame);
    m_index = 0;
    m_clock.start();

    m_player = new QMediaPlayer(this);
    m_sink = new QVideoSink(this);
    m_player->setVideoSink(m_sink);

    connect(m_sink, &QVideoSink::videoFrameChanged, this, &MediaUrlFrameSource::handleFrame);
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [onError = std::move(onError)](QMediaPlayer::Error, const QString& message) {
                if (onError)
                    onError(message.toStdString());
            });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [onFinished = std::move(onFinished)](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::EndOfMedia && onFinished)
                    onFinished();
            });

    m_player->setSource(m_url);
    m_player->play();
}

void MediaUrlFrameSource::stop()
{
    if (m_player) {
        m_player->stop();
        delete m_player;
        m_player = nullptr;
    }
    delete m_sink;
    m_sink = nullptr;
}

void MediaUrlFrameSource::handleFrame(const QVideoFrame& frame)
{
    if (!frame.isValid() || !m_onFrame)
        return;
    const QImage image = frame.toImage();
    if (image.isNull())
        return;
    // Files carry their own time. Live streams may not, so fall back to the wall clock.
    const qint64 start = frame.startTime();
    const std::int64_t ms = start >= 0 ? start / 1000 : m_clock.elapsed();
    m_onFrame(Frame{toDomainImage(image, ms), m_index++});
}

CameraFrameSource::CameraFrameSource(QObject* parent) : QObject(parent) {}

CameraFrameSource::~CameraFrameSource()
{
    CameraFrameSource::stop();
}

void CameraFrameSource::start(FrameHandler onFrame, ErrorHandler onError, FinishedHandler)
{
    stop();
    if (QMediaDevices::defaultVideoInput().isNull()) {
        if (onError)
            onError("No camera found");
        return;
    }
    m_onFrame = std::move(onFrame);
    m_index = 0;
    m_clock.start();

    m_camera = new QCamera(QMediaDevices::defaultVideoInput(), this);
    m_session = new QMediaCaptureSession(this);
    m_sink = new QVideoSink(this);
    m_session->setCamera(m_camera);
    m_session->setVideoSink(m_sink);

    connect(m_camera, &QCamera::errorOccurred, this,
            [onError = std::move(onError)](QCamera::Error, const QString& message) {
                if (onError)
                    onError(message.toStdString());
            });
    connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
        if (!frame.isValid() || !m_onFrame)
            return;
        const QImage image = frame.toImage();
        if (!image.isNull())
            m_onFrame(Frame{toDomainImage(image, m_clock.elapsed()), m_index++});
    });
    m_camera->start();
}

void CameraFrameSource::stop()
{
    if (m_camera)
        m_camera->stop();
    delete m_session;
    m_session = nullptr;
    delete m_camera;
    m_camera = nullptr;
    delete m_sink;
    m_sink = nullptr;
}

} // namespace vpr::qt
