#include "domain/plate_format.h"
#include "infrastructure/qt/evidence_sink.h"
#include "infrastructure/qt/pipeline_worker.h"
#include "infrastructure/qt/qt_image.h"
#include "infrastructure/qt/sqlite_passage_repository.h"
#include "infrastructure/qt/synthetic_frame_source.h"
#include "infrastructure/tracking/byte_tracker.h"
#include "simulation/synthetic_scene.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

using namespace vpr;

namespace {

PassageEvent sampleEvent()
{
    PassageEvent event;
    event.timestampMs = 1000;
    event.trackId = 3;
    event.status = PassageStatus::Read;
    event.plateText = "34AB567";
    event.displayText = "34 AB 567";
    event.countryCode = "AM";
    event.confidence = 0.9F;
    event.vehicleImage = blankImage(40, 30);
    event.plateImage = blankImage(20, 10);
    return event;
}

} // namespace

class TestQtAdapters : public QObject {
    Q_OBJECT

private slots:
    void imageConversionKeepsPixels()
    {
        QImage source(4, 2, QImage::Format_RGB32);
        source.fill(QColor(10, 20, 30));
        const Image image = qt::toDomainImage(source, 5);
        QCOMPARE(image.width, 4);
        QCOMPARE(image.height, 2);
        QCOMPARE(image.timestampMs, std::int64_t(5));
        QCOMPARE(int((*image.pixels)[1]), 20);
        QCOMPARE(qt::toQImage(image).pixelColor(3, 1), QColor(10, 20, 30));
    }

    void evidenceSinkWritesJsonAndPhotos()
    {
        QTemporaryDir dir;
        qt::EvidenceSink sink(dir.path());
        sink.onPassage(sampleEvent());

        QFile file(QDir(dir.path()).filePath("passage_1000_3.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(json["plate"].toString(), QString("34AB567"));
        QCOMPARE(json["status"].toString(), QString("read"));
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(json["vehiclePhoto"].toString())));
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(json["platePhoto"].toString())));
    }

    void repositoryStoresAndSearches()
    {
        QTemporaryDir dir;
        qt::SqlitePassageRepository repository(QDir(dir.path()).filePath("p.db"), dir.path());
        QVERIFY(repository.isOpen());
        repository.onPassage(sampleEvent());
        PassageEvent other = sampleEvent();
        other.plateText = encodeUtf8(U"12ب34567");
        other.countryCode = "IR";
        repository.onPassage(other);

        QCOMPARE(repository.search({"", 10}).size(), std::size_t(2));
        const auto found = repository.search({"AB 5", 10});
        QCOMPARE(found.size(), std::size_t(1));
        QCOMPARE(found[0].plateText, std::string("34AB567"));
        QCOMPARE(repository.search({encodeUtf8(U"ب"), 10}).size(), std::size_t(1));
        QCOMPARE(repository.search({"%", 10}).size(), std::size_t(0));  // a wildcard is plain text
    }

    void purgeRemovesRowsAndFiles()
    {
        QTemporaryDir dir;
        qt::EvidenceSink sink(dir.path());
        qt::SqlitePassageRepository repository(QDir(dir.path()).filePath("p.db"), dir.path());
        const PassageEvent event = sampleEvent();
        sink.onPassage(event);
        repository.onPassage(event);

        const QString photo = qt::EvidenceSink::vehiclePhotoPath(dir.path(), event);
        QVERIFY(QFile::exists(photo));
        QCOMPARE(repository.purgeOlderThan(500), 0);
        QVERIFY(QFile::exists(photo));
        QCOMPARE(repository.purgeOlderThan(2000), 1);
        QVERIFY(!QFile::exists(photo));
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath("passage_1000_3.json")));
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath("passage_1000_3_plate.jpg")));
        QVERIFY(repository.search({"", 10}).empty());
    }

    void fullChainFromSourceToDatabase()
    {
        QTemporaryDir dir;
        const QString dbPath = QDir(dir.path()).filePath("p.db");
        const QString evidence = dir.path();
        const auto scene = sim::SyntheticScene::demo(480, 270);

        class StoreSink final : public IPassageSink {
        public:
            StoreSink(const QString& db, const QString& folder) : m_evidence(folder), m_repository(db, folder) {}
            void onPassage(const PassageEvent& event) override
            {
                m_evidence.onPassage(event);
                m_repository.onPassage(event);
            }

        private:
            qt::EvidenceSink m_evidence;
            qt::SqlitePassageRepository m_repository;
        };

        qt::PipelineWorker worker(
            [=]() -> PassagePipeline::Dependencies {
                return {std::make_shared<sim::SimVehicleDetector>(scene),
                        std::make_shared<sim::SimPlateDetector>(scene),
                        std::make_shared<sim::SimPlateReader>(scene),
                        std::make_shared<ByteTracker>(),
                        std::make_shared<StoreSink>(dbPath, evidence)};
            },
            defaultPlateFormats(), PipelineConfig{});

        QThread thread;
        worker.moveToThread(&thread);
        thread.start();
        QMetaObject::invokeMethod(&worker, &qt::PipelineWorker::initialize, Qt::BlockingQueuedConnection);

        QSignalSpy passages(&worker, &qt::PipelineWorker::passage);
        QSignalSpy done(&worker, &qt::PipelineWorker::finished);

        // Paced by the 5 ms timer. The worker may drop frames, which the chain has to tolerate.
        qt::SyntheticFrameSource source(scene, 40, 5);
        source.start([&worker](const Frame& frame) { worker.submit(frame); }, {}, [&worker]() {
            QMetaObject::invokeMethod(&worker, &qt::PipelineWorker::finish, Qt::QueuedConnection);
        });

        QVERIFY(done.wait(60000));
        QMetaObject::invokeMethod(&worker, &qt::PipelineWorker::shutdown, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();

        QCOMPARE(passages.count(), 5);
        qt::SqlitePassageRepository check(dbPath, evidence);
        const auto rows = check.search({"", 10});
        QCOMPARE(rows.size(), std::size_t(5));  // a car without a visible plate is kept too
        for (const StoredPassage& row : rows)
            QVERIFY2(QFile::exists(QString::fromStdString(row.vehicleImagePath)), row.vehicleImagePath.c_str());
    }
};

QTEST_MAIN(TestQtAdapters)
#include "test_qt_adapters.moc"
