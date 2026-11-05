// Runs the real plate models. They come from scripts/fetch-dependencies.sh, and the test skips without them.
// Hand checked vehicle photos can sit in data/plate-samples, each named after its plate, for example
// 12AB345.jpg. That folder holds personal data, so it stays out of git and this part skips on CI.
// VPR_PLATE_SAMPLES points the test at another folder.
#include "infrastructure/onnx/plate_models.h"
#include "infrastructure/qt/qt_image.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QtTest>

#include <algorithm>
#include <memory>
#include <vector>

using namespace vpr;

namespace {

const QString kModels = QStringLiteral(VPR_MODELS_DIR);
const QString kSamples = qEnvironmentVariableIsSet("VPR_PLATE_SAMPLES") ? qEnvironmentVariable("VPR_PLATE_SAMPLES")
                                                                      : QStringLiteral(VPR_PLATE_SAMPLES_DIR);
const QString kDetectorFile = QStringLiteral("/yolo-v9-t-640-license-plates-end2end.onnx");
const QString kReaderFile = QStringLiteral("/cct_s_v2_global.onnx");
const QString kConfigFile = QStringLiteral("/cct_s_v2_global_plate_config.yaml");

} // namespace

class TestRealPlates : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!QFile::exists(kModels + kDetectorFile) || !QFile::exists(kModels + kReaderFile)
            || !QFile::exists(kModels + kConfigFile))
            QSKIP("Plate models not downloaded, run scripts/fetch-dependencies.sh");
        m_detector = std::make_unique<YoloV9PlateDetector>((kModels + kDetectorFile).toStdWString());
        m_reader = std::make_unique<SlotPlateReader>((kModels + kReaderFile).toStdWString(),
                                                     (kModels + kConfigFile).toStdWString());
    }

    void anEmptyPictureHasNoPlate()
    {
        QImage grey(400, 300, QImage::Format_RGB888);
        grey.fill(Qt::gray);
        QVERIFY(m_detector->detect(qt::toDomainImage(grey, 0)).empty());
    }

    void anyPlateCropGivesAnAnswerWithoutFailing()
    {
        QImage grey(120, 40, QImage::Format_RGB888);
        grey.fill(Qt::gray);
        const auto reading = m_reader->read(qt::toDomainImage(grey, 0));
        if (reading) {
            QVERIFY(reading->confidence >= 0.0F);
            QVERIFY(reading->confidence <= 1.0F);
        }
    }

    void missingFilesAreReported()
    {
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, YoloV9PlateDetector(L"does-not-exist.onnx"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, SlotPlateReader(L"does-not-exist.onnx", L"does-not-exist.yaml"));
    }

    void handCheckedPhotosAreRead()
    {
        const QStringList files = QDir(kSamples).entryList({QStringLiteral("*.jpg"), QStringLiteral("*.png")}, QDir::Files);
        if (files.isEmpty())
            QSKIP("No hand checked photos in data/plate-samples");

        int correct = 0;
        for (const QString& file : files) {
            const Image vehicle = qt::toDomainImage(QImage(QDir(kSamples).filePath(file)), 0);
            QElapsedTimer timer;
            timer.start();
            const auto plates = m_detector->detect(vehicle);
            const qint64 detectMs = timer.restart();
            QString text;
            float confidence = 0.0F;
            if (!plates.empty()) {
                const auto best = std::max_element(plates.begin(), plates.end(), [](const Detection& a, const Detection& b) {
                    return a.confidence < b.confidence;
                });
                if (const auto reading = m_reader->read(vehicle.crop(best->box))) {
                    text = QString::fromStdString(reading->text);
                    confidence = reading->confidence;
                }
            }
            const qint64 readMs = timer.elapsed();
            const bool right = text == QFileInfo(file).completeBaseName();
            correct += right ? 1 : 0;
            qInfo().noquote() << file << "read" << text << "confidence" << confidence << "detect ms" << detectMs
                              << "read ms" << readMs << (right ? "right" : "WRONG");
        }
        qInfo() << "correct" << correct << "of" << files.size();
        QCOMPARE(correct, static_cast<int>(files.size()));
    }

private:
    std::unique_ptr<YoloV9PlateDetector> m_detector;
    std::unique_ptr<SlotPlateReader> m_reader;
};

QTEST_GUILESS_MAIN(TestRealPlates)
#include "test_real_plates.moc"
