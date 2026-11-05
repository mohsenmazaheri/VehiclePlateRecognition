// Runs the real YOLOX model. It needs scripts/fetch-dependencies.sh and skips without its files.
// One picture says nothing about accuracy on road video: this checks the connection is right.
#include "application/passage_pipeline.h"
#include "domain/plate_format.h"
#include "infrastructure/onnx/yolox_detector.h"
#include "infrastructure/qt/qt_image.h"
#include "infrastructure/tracking/byte_tracker.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QtTest>

#include <algorithm>
#include <memory>

using namespace vpr;

namespace {

const QString kModels = QStringLiteral(VPR_MODELS_DIR);

class NoPlates final : public IPlateDetector {
public:
    std::vector<Detection> detect(const Image&) override { return {}; }
};
class NoReader final : public IPlateReader {
public:
    std::optional<PlateReading> read(const Image&) override { return std::nullopt; }
};
class Collect final : public IPassageSink {
public:
    void onPassage(const PassageEvent& event) override { events.push_back(event); }
    std::vector<PassageEvent> events;
};

} // namespace

class TestRealDetector : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!QFile::exists(kModels + "/yolox_s.onnx") || !QFile::exists(kModels + "/samples/dog.jpg"))
            QSKIP("Model not downloaded, run scripts/fetch-dependencies.sh");
        m_detector = std::make_shared<YoloxVehicleDetector>((kModels + "/yolox_s.onnx").toStdWString());
        m_picture = QImage(kModels + "/samples/dog.jpg");
        QVERIFY(!m_picture.isNull());
    }

    void findsTheTruckInTheSamplePicture()
    {
        const Image image = qt::toDomainImage(m_picture, 0);
        QElapsedTimer timer;
        timer.start();
        const auto found = m_detector->detect(image);
        qInfo() << "size" << image.width << image.height << "time ms" << timer.elapsed() << "provider"
                << m_detector->executionProvider().c_str();
        for (const Detection& d : found)
            qInfo() << "box" << d.box.x << d.box.y << d.box.w << d.box.h << "score" << d.confidence;

        const auto strong = std::count_if(found.begin(), found.end(),
                                          [](const Detection& d) { return d.confidence >= 0.5F; });
        QCOMPARE(static_cast<int>(strong), 1);
        const Detection& truck = found.front();
        // The truck stands in the upper right of this picture.
        QVERIFY(truck.box.centre().x > image.width * 0.55);
        QVERIFY(truck.box.centre().y < image.height * 0.35);
    }

    void aRealDetectionCrossesTheLineOnce()
    {
        // A window sliding down the picture makes the truck travel up through the line.
        auto sink = std::make_shared<Collect>();
        PassagePipeline::Dependencies deps;
        deps.vehicleDetector = m_detector;
        deps.plateDetector = std::make_shared<NoPlates>();
        deps.plateReader = std::make_shared<NoReader>();
        deps.tracker = std::make_shared<ByteTracker>();
        deps.sink = sink;
        PipelineConfig config;
        config.line = CountingLine{{0.0, 0.3}, {1.0, 0.3}};
        PassagePipeline pipeline(std::move(deps), defaultPlateFormats(), config);

        const Image full = qt::toDomainImage(m_picture, 0);
        const double windowHeight = full.height * 0.7;
        const int steps = 30;
        for (int i = 0; i < steps; ++i) {
            const double top = (full.height - windowHeight) * i / (steps - 1);
            Image window = full.crop({0.0, top, static_cast<double>(full.width), windowHeight});
            window.originX = window.originY = 0;
            window.timestampMs = i * 40;
            pipeline.process({window, i});
        }
        pipeline.flush();

        QCOMPARE(pipeline.counters().crossed, 1L);
        QCOMPARE(sink->events.size(), std::size_t{1});
        QVERIFY(!sink->events[0].vehicleImage.empty());
        QCOMPARE(sink->events[0].status, PassageStatus::NotVisible);  // this test uses no plate models
    }

    void aMissingModelIsReported()
    {
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, YoloxVehicleDetector(L"does-not-exist.onnx"));
    }

private:
    std::shared_ptr<YoloxVehicleDetector> m_detector;
    QImage m_picture;
};

QTEST_GUILESS_MAIN(TestRealDetector)
#include "test_real_detector.moc"
