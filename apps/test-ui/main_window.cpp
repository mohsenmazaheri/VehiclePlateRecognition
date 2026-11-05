#include "main_window.h"

#include "domain/plate_format.h"
#include "infrastructure/qt/evidence_sink.h"
#include "image_box.h"
#include "infrastructure/qt/media_frame_sources.h"
#include "infrastructure/qt/qt_image.h"
#include "infrastructure/qt/sqlite_passage_repository.h"
#include "infrastructure/qt/synthetic_frame_source.h"
#include "infrastructure/tracking/byte_tracker.h"
#include "simulation/synthetic_scene.h"
#include "video_view.h"

#ifdef VPR_HAS_ONNX
#include "infrastructure/onnx/plate_models.h"
#include "infrastructure/onnx/yolox_detector.h"
#endif

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

using namespace vpr;

namespace {

constexpr qint64 kRetentionMs = 30LL * 24 * 60 * 60 * 1000;

// Stand ins for models that are missing or not trained yet.
class NoVehicleDetector final : public IVehicleDetector {
public:
    std::vector<Detection> detect(const Image&) override { return {}; }
};
class NoPlateDetector final : public IPlateDetector {
public:
    std::vector<Detection> detect(const Image&) override { return {}; }
};
class NoPlateReader final : public IPlateReader {
public:
    std::optional<PlateReading> read(const Image&) override { return std::nullopt; }
};

// Keeps the wall clock in the database, because file timestamps start at zero on every run.
class StoreSink final : public IPassageSink {
public:
    StoreSink(const QString& database, const QString& evidence) : m_evidence(evidence), m_repository(database, evidence) {}

    void onPassage(const PassageEvent& source) override
    {
        PassageEvent event = source;
        event.timestampMs = QDateTime::currentMSecsSinceEpoch();
        m_evidence.onPassage(event);
        m_repository.onPassage(event);
    }

private:
    qt::EvidenceSink m_evidence;
    qt::SqlitePassageRepository m_repository;
};

QString statusText(PassageStatus status)
{
    switch (status) {
    case PassageStatus::Read: return MainWindow::tr("Read");
    case PassageStatus::UnknownFormat: return MainWindow::tr("Unknown format");
    case PassageStatus::NotVisible: return MainWindow::tr("No plate read");
    }
    return {};
}

#ifdef VPR_HAS_ONNX
// VPR_MODELS, or a models folder next to the program or in any folder above it. Build folders made by
// IDEs, such as build/Desktop_Qt_..._Debug/apps/test-ui, sit at different depths below the project.
QString findModel(const QString& fileName)
{
    QStringList folders;
    if (qEnvironmentVariableIsSet("VPR_MODELS"))
        folders << qEnvironmentVariable("VPR_MODELS");
    for (QDir folder(QCoreApplication::applicationDirPath());;) {
        folders << folder.filePath(QStringLiteral("models"));
        if (!folder.cdUp())
            break;
    }
    for (const QString& folder : std::as_const(folders)) {
        const QString path = QDir(folder).filePath(fileName);
        if (QFile::exists(path))
            return QDir::cleanPath(path);
    }
    return {};
}
#endif

} // namespace

MainWindow::MainWindow(const QString& dataDirectory, QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(tr("Vehicle Plate Recognition: test tool"));
    resize(1200, 680);

    m_dataDirectory = dataDirectory.isEmpty()
                          ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          : dataDirectory;
    // The demo has its own store, so that its scripted plates never mix with real results.
    const auto openStore = [](const QString& folder) {
        const QString evidence = QDir(folder).filePath(QStringLiteral("evidence"));
        QDir().mkpath(evidence);
        auto store = std::make_unique<qt::SqlitePassageRepository>(QDir(folder).filePath(QStringLiteral("passages.db")),
                                                                   evidence);
        store->purgeOlderThan(QDateTime::currentMSecsSinceEpoch() - kRetentionMs);
        return store;
    };
    m_repository = openStore(m_dataDirectory);
    m_demoRepository = openStore(QDir(m_dataDirectory).filePath(QStringLiteral("demo")));

    auto* toolbar = addToolBar(tr("Source"));
    toolbar->setMovable(false);
    m_sourceBox = new QComboBox(this);
    m_sourceBox->addItem(tr("Synthetic demo"));
    m_sourceBox->addItem(tr("Video file..."));
    m_sourceBox->addItem(tr("Camera"));
    m_sourceBox->addItem(tr("Stream URL (RTSP)..."));
    toolbar->addWidget(m_sourceBox);
    m_countryBox = new QComboBox(this);
    m_countryBox->addItem(tr("Plates: Armenia"));
    m_countryBox->addItem(tr("Plates: Iran"));
    m_countryBox->addItem(tr("Plates: Armenia and Iran"));
    m_countryBox->setToolTip(tr("Only plates of these countries are accepted as read. Others are listed as unknown format."));
    toolbar->addWidget(m_countryBox);
    m_startButton = new QPushButton(tr("Start"), this);
    m_stopButton = new QPushButton(tr("Stop"), this);
    m_stopButton->setEnabled(false);
    toolbar->addWidget(m_startButton);
    toolbar->addWidget(m_stopButton);

    m_view = new VideoView(this);
    m_plateText = new QLabel(this);
    QFont plateFont = m_plateText->font();
    plateFont.setPointSizeF(plateFont.pointSizeF() * 1.8);
    plateFont.setBold(true);
    m_plateText->setFont(plateFont);
    m_plateText->setAlignment(Qt::AlignCenter);
    m_plateText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_platePhoto = new ImageBox(tr("Plate photo"), this);
    m_platePhoto->setFixedHeight(70);
    m_vehiclePhoto = new ImageBox(tr("Vehicle photo appears when a vehicle crosses the line"), this);
    m_vehiclePhoto->setMinimumHeight(200);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search stored plates"));
    m_search->setClearButtonEnabled(true);
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({tr("Time"), tr("Plate"), tr("Country"), tr("Status")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_counters = new QLabel(this);
    m_counters->setWordWrap(true);

    auto* side = new QWidget(this);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->addWidget(m_plateText);
    sideLayout->addWidget(m_platePhoto);
    sideLayout->addWidget(m_vehiclePhoto, 2);
    sideLayout->addWidget(m_search);
    sideLayout->addWidget(m_table, 3);
    sideLayout->addWidget(m_counters);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(m_view);
    splitter->addWidget(side);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    setCentralWidget(splitter);

    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::start);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stop);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::showRows);
    connect(m_sourceBox, &QComboBox::currentIndexChanged, this, [this]() {
        if (searching())
            showRows();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &MainWindow::showSelectedPassage);

    clearRun();  // the list shows this run only, stored passages are reached through search
    showStatus(tr("Ready. Drag either end of the yellow line to move it."));
}

MainWindow::~MainWindow()
{
    stop();
}

void MainWindow::start()
{
    stop();

    const int choice = m_sourceBox->currentIndex();
    const auto scene = sim::SyntheticScene::demo();
    const bool demo = demoSelected();
    std::unique_ptr<IFrameSource> source;

    if (demo) {
        source = std::make_unique<qt::SyntheticFrameSource>(scene);
    } else if (choice == 1) {
        const QString path = !m_presetVideo.isEmpty()
                                 ? m_presetVideo
                                 : QFileDialog::getOpenFileName(this, tr("Open video"), {},
                                                                tr("Video (*.mp4 *.mov *.mkv *.avi);;All files (*)"));
        m_presetVideo.clear();
        if (path.isEmpty())
            return;
        source = std::make_unique<qt::MediaUrlFrameSource>(QUrl::fromLocalFile(path));
    } else if (choice == 2) {
        source = std::make_unique<qt::CameraFrameSource>();
    } else {
        const QString url = QInputDialog::getText(this, tr("Stream URL"), tr("Address, for example rtsp://host/stream"));
        if (url.isEmpty())
            return;
        source = std::make_unique<qt::MediaUrlFrameSource>(QUrl(url));
    }

    const QString detectorStatus = demo ? QString() : prepareDetectors();
    m_runStatus = detectorStatus;
    const std::shared_ptr<IVehicleDetector> vehicleDetector = m_vehicleDetector;
    const std::shared_ptr<IPlateDetector> plateDetector = m_plateDetector;
    const std::shared_ptr<IPlateReader> plateReader = m_plateReader;

    const QDir store(demo ? QDir(m_dataDirectory).filePath(QStringLiteral("demo")) : m_dataDirectory);
    const QString database = store.filePath(QStringLiteral("passages.db"));
    const QString evidence = store.filePath(QStringLiteral("evidence"));
    const qt::PipelineWorker::Factory factory = [=]() -> PassagePipeline::Dependencies {
        PassagePipeline::Dependencies deps;
        if (demo) {
            deps.vehicleDetector = std::make_shared<sim::SimVehicleDetector>(scene);
            deps.plateDetector = std::make_shared<sim::SimPlateDetector>(scene);
            deps.plateReader = std::make_shared<sim::SimPlateReader>(scene);
        } else {
            deps.vehicleDetector = vehicleDetector ? vehicleDetector : std::make_shared<NoVehicleDetector>();
            deps.plateDetector = plateDetector ? plateDetector : std::make_shared<NoPlateDetector>();
            deps.plateReader = plateReader ? plateReader : std::make_shared<NoPlateReader>();
        }
        deps.tracker = std::make_shared<ByteTracker>();
        deps.sink = std::make_shared<StoreSink>(database, evidence);
        return deps;
    };

    PipelineConfig config;
    config.reportWhenRead = true;  // show a passage as soon as the vehicle reaches the line
    if (!demo) {
        config.readEveryNFrames = 2;
        config.maxPlateReadsPerFrame = 1;
        config.minReadingConfidence = 0.9F;  // from a first look at clear and blurred Yerevan footage
    }
    config.line = CountingLine{{m_view->lineA().x(), m_view->lineA().y()}, {m_view->lineB().x(), m_view->lineB().y()}};
    clearRun();
    m_worker =std::make_unique<qt::PipelineWorker>(factory, selectedFormats(), config);
    m_worker->moveToThread(&m_thread);
    m_thread.start();
    QMetaObject::invokeMethod(m_worker.get(), &qt::PipelineWorker::initialize, Qt::BlockingQueuedConnection);

    connect(m_view, &VideoView::lineChanged, m_worker.get(), &qt::PipelineWorker::setLine);
    connect(m_worker.get(), &qt::PipelineWorker::frameProcessed, this,
            [this](const QImage& image, const QVector<Track>& tracks, const PassageCounters& counters, qint64 dropped) {
                m_view->showFrame(image, tracks);
                m_dropped = dropped;
                showCounters(counters);
            });
    connect(m_worker.get(), &qt::PipelineWorker::countersChanged, this, &MainWindow::showCounters);
    connect(m_worker.get(), &qt::PipelineWorker::passage, this, &MainWindow::addPassage);
    connect(m_worker.get(), &qt::PipelineWorker::failed, this,
            [this](const QString& message) { showStatus(tr("Processing error: %1").arg(message)); });
    connect(m_worker.get(), &qt::PipelineWorker::finished, this, [this]() {
        showStatus(tr("Source finished. %1").arg(m_runStatus));
        qInfo().noquote() << "Source finished:" << m_counters->text();
        emit sourceFinished();
    });

    m_source = std::move(source);
    qt::PipelineWorker* worker = m_worker.get();
    m_source->start([worker](const Frame& frame) { worker->submit(frame); },
                    [this](const std::string& message) {
                        const QString text = QString::fromStdString(message);
                        QMetaObject::invokeMethod(this, [this, text]() { showStatus(tr("Source error: %1").arg(text)); },
                                                  Qt::QueuedConnection);
                    },
                    [worker]() { QMetaObject::invokeMethod(worker, &qt::PipelineWorker::finish, Qt::QueuedConnection); });

    m_startButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    showStatus(demo ? tr("Synthetic demo: scripted plates of both countries, kept apart from real results. It shows no real accuracy.")
                    : detectorStatus);
}

void MainWindow::startVideo(const QString& path)
{
    m_presetVideo = path;
    m_sourceBox->setCurrentIndex(1);
    start();
}

QString MainWindow::prepareDetectors()
{
#ifdef VPR_HAS_ONNX
    if (!m_vehicleDetector) {
        const QString model = findModel(QStringLiteral("yolox_s.onnx"));
        if (model.isEmpty())
            return tr("No vehicle model found, so no vehicles are detected. Run scripts/fetch-dependencies.sh.");
        try {
            auto detector = std::make_shared<YoloxVehicleDetector>(model.toStdWString());
            m_detectorName = tr("YOLOX-S on %1").arg(QString::fromStdString(detector->executionProvider()));
            m_vehicleDetector = std::move(detector);
        } catch (const std::exception& error) {
            return tr("Vehicle model failed to load, so no vehicles are detected: %1").arg(QString::fromUtf8(error.what()));
        }
    }
    if (!m_plateDetector || !m_plateReader) {
        const QString detector = findModel(QStringLiteral("yolo-v9-t-640-license-plates-end2end.onnx"));
        const QString reader = findModel(QStringLiteral("cct_s_v2_global.onnx"));
        const QString config = findModel(QStringLiteral("cct_s_v2_global_plate_config.yaml"));
        if (detector.isEmpty() || reader.isEmpty() || config.isEmpty())
            return tr("Vehicles: %1. Plate models not found, so no plate is read. Run scripts/fetch-dependencies.sh.")
                .arg(m_detectorName);
        try {
            m_plateDetector = std::make_shared<YoloV9PlateDetector>(detector.toStdWString());
            m_plateReader = std::make_shared<SlotPlateReader>(reader.toStdWString(), config.toStdWString());
        } catch (const std::exception& error) {
            m_plateDetector.reset();
            m_plateReader.reset();
            return tr("Vehicles: %1. Plate models failed to load, so no plate is read: %2")
                .arg(m_detectorName, QString::fromUtf8(error.what()));
        }
    }
    return tr("Vehicles: %1. Plates: trial models, Latin letters only, so Iranian plates are not read yet.").arg(m_detectorName);
#else
    return tr("Built without ONNX Runtime, so no vehicles are detected. Run scripts/fetch-dependencies.sh and rebuild.");
#endif
}

void MainWindow::stop()
{
    if (m_source) {
        m_source->stop();
        m_source.reset();
    }
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker.get(), &qt::PipelineWorker::finish, Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(m_worker.get(), &qt::PipelineWorker::shutdown, Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
        m_worker.reset();
    }
    if (m_startButton) {
        m_startButton->setEnabled(true);
        m_stopButton->setEnabled(false);
    }
}

bool MainWindow::searching() const
{
    return !m_search->text().trimmed().isEmpty();
}

void MainWindow::fillRow(int row, const ShownPassage& passage)
{
    m_table->setItem(row, 0, new QTableWidgetItem(passage.time));
    m_table->setItem(row, 1, new QTableWidgetItem(passage.plate));
    m_table->setItem(row, 2, new QTableWidgetItem(passage.country));
    m_table->setItem(row, 3, new QTableWidgetItem(passage.status));
}

// Without search text the table lists this run, newest first. With it, the store of the chosen source.
void MainWindow::showRows()
{
    const QSignalBlocker blocker(m_table);
    m_table->clearContents();
    if (!searching()) {
        m_searchResults.clear();
        const int count = static_cast<int>(m_runPassages.size());
        m_table->setRowCount(count);
        for (int i = 0; i < count; ++i)
            fillRow(i, m_runPassages[static_cast<std::size_t>(count - 1 - i)]);
        return;
    }

    m_searchResults.clear();
    for (const StoredPassage& row : repository().search({m_search->text().trimmed().toStdString(), 200})) {
        ShownPassage passage;
        passage.time = QDateTime::fromMSecsSinceEpoch(row.timestampMs).toString(QStringLiteral("dd MMM HH:mm:ss"));
        passage.plate = QString::fromStdString(row.displayText);
        passage.country = QString::fromStdString(row.countryCode);
        passage.status = statusText(row.status);
        passage.vehiclePath = QString::fromStdString(row.vehicleImagePath);
        m_searchResults.push_back(std::move(passage));
    }
    m_table->setRowCount(static_cast<int>(m_searchResults.size()));
    for (int i = 0; i < static_cast<int>(m_searchResults.size()); ++i)
        fillRow(i, m_searchResults[static_cast<std::size_t>(i)]);
}

void MainWindow::addPassage(const PassageEvent& event)
{
    ShownPassage passage;
    passage.time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    passage.plate = QString::fromStdString(event.displayText);
    passage.country = QString::fromStdString(event.countryCode);
    passage.status = statusText(event.status);
    // Kept small, a long run holds hundreds of these.
    const QImage vehicle = qt::toQImage(event.vehicleImage);
    passage.vehicle = vehicle.width() > 480 ? vehicle.scaledToWidth(480, Qt::SmoothTransformation) : vehicle;
    passage.platePicture = qt::toQImage(event.plateImage);
    m_runPassages.push_back(passage);

    if (!searching()) {
        const QSignalBlocker blocker(m_table);
        m_table->insertRow(0);
        fillRow(0, passage);
        m_table->selectRow(0);
    }
    showPassage(passage);
}

void MainWindow::showPassage(const ShownPassage& passage)
{
    m_plateText->setText(passage.plate.isEmpty() ? passage.status
                                                 : QStringLiteral("%1   %2").arg(passage.plate, passage.country));
    QImage vehicle = passage.vehicle;
    QImage plate = passage.platePicture;
    if (vehicle.isNull() && !passage.vehiclePath.isEmpty()) {
        vehicle.load(passage.vehiclePath);
        QString base = passage.vehiclePath;
        base.chop(QStringLiteral("_vehicle.jpg").size());
        plate.load(base + QStringLiteral("_plate.jpg"));
    }
    m_vehiclePhoto->setImage(vehicle, tr("No vehicle photo"));
    m_platePhoto->setImage(plate, tr("No plate photo"));
}

void MainWindow::showSelectedPassage()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const auto index = static_cast<std::size_t>(row);
    if (searching()) {
        if (index < m_searchResults.size())
            showPassage(m_searchResults[index]);
    } else if (index < m_runPassages.size()) {
        showPassage(m_runPassages[m_runPassages.size() - 1 - index]);
    }
}

void MainWindow::clearRun()
{
    m_runPassages.clear();
    m_dropped = 0;
    m_plateText->setText(tr("No passage yet"));
    m_vehiclePhoto->clear();
    m_platePhoto->clear();
    showCounters({});
    showRows();
}

bool MainWindow::demoSelected() const
{
    return m_sourceBox->currentIndex() == 0;
}

qt::SqlitePassageRepository& MainWindow::repository() const
{
    return demoSelected() ? *m_demoRepository : *m_repository;
}

PlateFormatRegistry MainWindow::selectedFormats() const
{
    PlateFormatRegistry formats;
    const int choice = m_countryBox->currentIndex();
    if (choice == 0 || choice == 2) {
        formats.add(armeniaPrivateFormat());
        formats.add(armeniaPrivateThreeDigitFormat());
    }
    if (choice == 1 || choice == 2)
        formats.add(iranPrivateFormat());
    return formats;
}

void MainWindow::showCounters(const PassageCounters& counters)
{
    m_counters->setText(tr("Crossed %1 | Read %2 | Unknown %3 | No plate %4 | Dropped frames %5")
                            .arg(counters.crossed).arg(counters.read).arg(counters.unknownFormat)
                            .arg(counters.notVisible).arg(m_dropped));
}

void MainWindow::showStatus(const QString& text)
{
    statusBar()->showMessage(text);
}

void MainWindow::setLine(double ax, double ay, double bx, double by)
{
    m_view->setLine({ax, ay}, {bx, by});
}
