#pragma once
#include "domain/plate_format.h"
#include "domain/ports.h"
#include "infrastructure/qt/pipeline_worker.h"

#include <QDateTime>
#include <QImage>
#include <QMainWindow>
#include <QThread>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class ImageBox;
class Language;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QToolBar;
class VideoView;

namespace vpr::qt {
class SqlitePassageRepository;
}

// Composition root of the test tool: the one place that knows the concrete classes.
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    // dataDirectory: where passages and photos go. Empty means the normal application data folder.
    explicit MainWindow(Language& language, const QString& dataDirectory = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

    void start();
    void stop();
    void startVideo(const QString& path);
    void setLine(double ax, double ay, double bx, double by);

signals:
    void sourceFinished();

protected:
    void changeEvent(QEvent* event) override;

private:
    // Texts are kept as functions, so that they can be built again in another language.
    using Text = std::function<QString()>;

    // One row of the table. Passages of this run carry their pictures, stored ones their photo path.
    struct ShownPassage {
        QDateTime time;
        bool stored = false;
        QString plate;
        QString country;
        vpr::PassageStatus status = vpr::PassageStatus::NotVisible;
        QImage vehicle;
        QImage platePicture;
        QString vehiclePath;
    };

    void retranslate();
    void showRows();
    void addPassage(const vpr::PassageEvent& event);
    void showPassage(const ShownPassage& passage);
    void showSelectedPassage();
    void clearRun();
    void fillRow(int row, const ShownPassage& passage);
    [[nodiscard]] bool searching() const;
    void setStatus(Text text);
    void showCounters(const vpr::PassageCounters& counters);
    Text prepareDetectors();
    [[nodiscard]] bool demoSelected() const;
    [[nodiscard]] vpr::qt::SqlitePassageRepository& repository() const;
    [[nodiscard]] vpr::PlateFormatRegistry selectedFormats() const;

    Language& m_language;
    VideoView* m_view = nullptr;
    QToolBar* m_toolbar = nullptr;
    QComboBox* m_sourceBox = nullptr;
    QComboBox* m_countryBox = nullptr;
    QComboBox* m_languageBox = nullptr;
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
    std::optional<ShownPassage> m_shown;         // the passage in the photo panel

    QString m_dataDirectory;
    QString m_presetVideo;
    std::shared_ptr<vpr::IVehicleDetector> m_vehicleDetector;
    std::shared_ptr<vpr::IPlateDetector> m_plateDetector;
    std::shared_ptr<vpr::IPlateReader> m_plateReader;
    QString m_executionProvider;  // where the vehicle model runs, for example "CPU"
    Text m_status;
    Text m_detectorStatus;
    vpr::PassageCounters m_counterValues;
    qint64 m_dropped = 0;
    std::unique_ptr<vpr::qt::SqlitePassageRepository> m_repository;      // real sources
    std::unique_ptr<vpr::qt::SqlitePassageRepository> m_demoRepository;  // scripted demo, kept apart
    std::unique_ptr<vpr::IFrameSource> m_source;
    std::unique_ptr<vpr::qt::PipelineWorker> m_worker;
    QThread m_thread;
};
