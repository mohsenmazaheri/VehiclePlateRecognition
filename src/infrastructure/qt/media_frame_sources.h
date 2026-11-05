#pragma once
// Frame sources based on Qt Multimedia. Each class is one way to get pictures into the library.
// A vendor camera SDK would be one more IFrameSource next to these, with no change to the core.
#include "domain/ports.h"

#include <QElapsedTimer>
#include <QObject>
#include <QUrl>
#include <QVideoFrame>

#include <memory>

class QCamera;
class QMediaCaptureSession;
class QMediaPlayer;
class QVideoSink;

namespace vpr::qt {

// A video file, or a network stream such as rtsp://camera/stream.
class MediaUrlFrameSource final : public QObject, public IFrameSource {
    Q_OBJECT
public:
    explicit MediaUrlFrameSource(QUrl url, QObject* parent = nullptr);
    ~MediaUrlFrameSource() override;

    void start(FrameHandler onFrame, ErrorHandler onError, FinishedHandler onFinished) override;
    void stop() override;

private:
    void handleFrame(const QVideoFrame& frame);

    QUrl m_url;
    QMediaPlayer* m_player = nullptr;
    QVideoSink* m_sink = nullptr;
    FrameHandler m_onFrame;
    std::int64_t m_index = 0;
    QElapsedTimer m_clock;
};

// A camera the operating system can see, such as a webcam.
class CameraFrameSource final : public QObject, public IFrameSource {
    Q_OBJECT
public:
    explicit CameraFrameSource(QObject* parent = nullptr);
    ~CameraFrameSource() override;

    void start(FrameHandler onFrame, ErrorHandler onError, FinishedHandler onFinished) override;
    void stop() override;

private:
    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_session = nullptr;
    QVideoSink* m_sink = nullptr;
    FrameHandler m_onFrame;
    std::int64_t m_index = 0;
    QElapsedTimer m_clock;
};

} // namespace vpr::qt
