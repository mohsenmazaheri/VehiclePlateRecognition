// A real MP4 goes through the real chain: decoder, frame source, worker thread, YOLOX, tracker, line.
// The file is made here from one still picture, so it shows the chain works, not how accurate it is.
// Set VPR_TEST_VIDEO_OUT to keep the video.
#include "domain/plate_format.h"
#include "infrastructure/onnx/yolox_detector.h"
#include "infrastructure/qt/media_frame_sources.h"
#include "infrastructure/qt/pipeline_worker.h"
#include "infrastructure/tracking/byte_tracker.h"

#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoFrameInput>
#include <QtTest>

using namespace vpr;

namespace {

const QString kModels = QStringLiteral(VPR_MODELS_DIR);
constexpr int kFrames = 150;
constexpr int kFrameMs = 40;
constexpr int kVideoHeight = 404;

class NoPlates final : public IPlateDetector {
public:
    std::vector<Detection> detect(const Image&) override { return {}; }
};
class NoReader final : public IPlateReader {
public:
    std::optional<PlateReading> read(const Image&) override { return std::nullopt; }
};
class Discard final : public IPassageSink {
public:
    void onPassage(const PassageEvent&) override {}
};

// A window slides down the picture, so the truck in it travels up. Returns an error text, empty on success.
QString writeVideo(const QString& path, const QImage& picture)
{
    const QSize size(picture.width(), kVideoHeight);
    QVideoFrameFormat format(size, QVideoFrameFormat::Format_BGRA8888);
    format.setStreamFrameRate(1000.0 / kFrameMs);
    QVideoFrameInput input(format);

    QMediaCaptureSession session;
    QMediaRecorder recorder;
    session.setVideoFrameInput(&input);
    session.setRecorder(&recorder);
    QMediaFormat mediaFormat(QMediaFormat::MPEG4);
    mediaFormat.setVideoCodec(QMediaFormat::VideoCodec::H264);
    recorder.setMediaFormat(mediaFormat);
    recorder.setQuality(QMediaRecorder::VeryHighQuality);
    recorder.setOutputLocation(QUrl::fromLocalFile(path));
    recorder.setAutoStop(true);

    QString error;
    QEventLoop loop;
    int sent = 0;
    bool ended = false;
    const auto sendMore = [&]() {
        while (sent < kFrames) {
            const int top = (picture.height() - kVideoHeight) * sent / (kFrames - 1);
            QVideoFrame frame(picture.copy(0, top, size.width(), size.height()).convertToFormat(QImage::Format_ARGB32));
            frame.setStartTime(qint64{sent} * kFrameMs * 1000);
            frame.setEndTime(qint64{sent + 1} * kFrameMs * 1000);
            if (!input.sendVideoFrame(frame))
                return;
            ++sent;
        }
        if (!ended)
            ended = input.sendVideoFrame(QVideoFrame());
    };
    QObject::connect(&input, &QVideoFrameInput::readyToSendVideoFrame, &loop, sendMore);
    QObject::connect(&recorder, &QMediaRecorder::errorOccurred, &loop,
                     [&](QMediaRecorder::Error, const QString& message) {
                         error = message;
                         loop.quit();
                     });
    QObject::connect(&recorder, &QMediaRecorder::recorderStateChanged, &loop, [&](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState)
            loop.quit();
    });
    QTimer::singleShot(90000, &loop, [&]() {
        error = QStringLiteral("timed out after %1 frames").arg(sent);
        loop.quit();
    });
    recorder.record();
    loop.exec();
    return error;
}

} // namespace

class TestVideoFile : public QObject {
    Q_OBJECT

private slots:
    void aVideoFileRunsEndToEnd()
    {
        if (!QFile::exists(kModels + "/yolox_s.onnx") || !QFile::exists(kModels + "/samples/dog.jpg"))
            QSKIP("Model not downloaded, run scripts/fetch-dependencies.sh");

        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString keep = qEnvironmentVariable("VPR_TEST_VIDEO_OUT");
        const QString path = keep.isEmpty() ? folder.filePath(QStringLiteral("moving.mp4")) : keep;
        const QImage picture(kModels + "/samples/dog.jpg");
        QVERIFY(!picture.isNull());

        const QString error = writeVideo(path, picture);
        if (!error.isEmpty() || !QFile::exists(path) || QFile(path).size() == 0)
            QSKIP(qPrintable("This Qt cannot encode a test video: " + error));

        auto detector = std::make_shared<YoloxVehicleDetector>(kModels.toStdWString() + L"/yolox_s.onnx");
        PipelineConfig config;
        config.line = CountingLine{{0.0, 0.3}, {1.0, 0.3}};
        qt::PipelineWorker worker(
            [=]() -> PassagePipeline::Dependencies {
                return {detector, std::make_shared<NoPlates>(), std::make_shared<NoReader>(),
                        std::make_shared<ByteTracker>(), std::make_shared<Discard>()};
            },
            defaultPlateFormats(), config);

        QThread thread;
        worker.moveToThread(&thread);
        thread.start();
        QMetaObject::invokeMethod(&worker, &qt::PipelineWorker::initialize, Qt::BlockingQueuedConnection);

        QSignalSpy passages(&worker, &qt::PipelineWorker::passage);
        QSignalSpy done(&worker, &qt::PipelineWorker::finished);
        QSignalSpy failures(&worker, &qt::PipelineWorker::failed);
        QSignalSpy processed(&worker, &qt::PipelineWorker::frameProcessed);

        int frames = 0;
        int width = 0;
        int height = 0;
        std::int64_t lastMs = -1;
        QString sourceError;
        qt::MediaUrlFrameSource source{QUrl::fromLocalFile(path)};
        source.start(
            [&](const Frame& frame) {
                ++frames;
                width = frame.image.width;
                height = frame.image.height;
                lastMs = frame.image.timestampMs;
                worker.submit(frame);
            },
            [&](const std::string& message) { sourceError = QString::fromStdString(message); },
            [&worker]() { QMetaObject::invokeMethod(&worker, &qt::PipelineWorker::finish, Qt::QueuedConnection); });

        const bool finished = done.wait(60000);
        source.stop();
        QMetaObject::invokeMethod(&worker, &qt::PipelineWorker::shutdown, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();

        qInfo() << "decoded" << frames << "processed" << processed.count() << "last ms" << lastMs << "passages"
                << passages.count() << "size" << width << height << "file bytes" << QFile(path).size();
        QVERIFY2(finished, qPrintable("The source never finished: " + sourceError));
        QVERIFY2(sourceError.isEmpty(), qPrintable(sourceError));
        QCOMPARE(failures.count(), 0);
        QVERIFY(frames > kFrames / 2);
        QCOMPARE(width, picture.width());
        QCOMPARE(height, kVideoHeight);
        QVERIFY(lastMs > (kFrames / 2) * kFrameMs);  // the file's own time, not the wall clock
        QCOMPARE(passages.count(), 1);
    }
};

QTEST_MAIN(TestVideoFile)
#include "test_video_file.moc"
