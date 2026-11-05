#pragma once
#include "domain/plate_format.h"
#include "domain/ports.h"
#include "infrastructure/qt/pipeline_worker.h"

#include <QImage>
#include <QMainWindow>
#include <QThread>

#include <memory>
#include <vector>

class ImageBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class VideoView;

namespace vpr::qt {
class SqlitePassageRepository;
}

// Composition root of the test tool: the one place that knows the concrete classes.
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    // dataDirectory: where passages and photos go. Empty means the normal application data folder.
    explicit MainWindow(const QString& dataDirectory = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

    void start();
    void stop();
    void startVideo(const QString& path);

signals:
    void sourceFinished();

public:
    void setLine(double ax, double ay, double bx, double by);

private:
    // One row of the table. Passages of this run carry their pictures, stored ones their photo path.
    struct ShownPassage {
        QString time;
        QString plate;
        QString country;
        QString status;
        QImage vehicle;
        QImage platePicture;
        QString vehiclePath;
    };

    void showRows();
    void addPassage(const vpr::PassageEvent& event);
    void showPassage(const ShownPassage& passage);
    void showSelectedPassage();
    void clearRun();
    void fillRow(int row, const ShownPassage& passage);
    [[nodiscard]] bool searching() const;
    void showStatus(const QString& text);
    void showCounters(const vpr::PassageCounters& counters);
    QString prepareDetectors();
    [[nodiscard]] bool demoSelected() const;
    [[nodiscard]] vpr::qt::SqlitePassageRepository& repository() const;
    [[nodiscard]] vpr::PlateFormatRegistry selectedFormats() const;

    VideoView* m_view = nullptr;
    QComboBox* m_sourceBox = nullptr;
    QComboBox* m_countryBox = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QLineEdit* m_search = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_plateText = nullptr;
    ImageBox* m_platePhoto = nullptr;
    ImageBox* m_vehiclePhoto = nullptr;
    QLabel* m_counters = nullptr;
    std::vector<ShownPassage> m_runPassages;     // this run, oldest first
    std::vector<ShownPassage> m_searchResults;   // from the store, newest first

    QString m_dataDirectory;
    QString m_presetVideo;
    std::shared_ptr<vpr::IVehicleDetector> m_vehicleDetector;
    std::shared_ptr<vpr::IPlateDetector> m_plateDetector;
    std::shared_ptr<vpr::IPlateReader> m_plateReader;
    QString m_detectorName;
    QString m_runStatus;
    qint64 m_dropped = 0;
    std::unique_ptr<vpr::qt::SqlitePassageRepository> m_repository;      // real sources
    std::unique_ptr<vpr::qt::SqlitePassageRepository> m_demoRepository;  // scripted demo, kept apart
    std::unique_ptr<vpr::IFrameSource> m_source;
    std::unique_ptr<vpr::qt::PipelineWorker> m_worker;
    QThread m_thread;
};
