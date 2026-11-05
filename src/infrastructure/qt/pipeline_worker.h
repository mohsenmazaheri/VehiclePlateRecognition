#pragma once
#include "application/passage_pipeline.h"
#include "domain/ports.h"

#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QVector>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace vpr::qt {

// Runs the pipeline on its own thread. It keeps only the newest frame: when processing is slower
// than the camera, old frames are dropped instead of building up delay. The tracker predicts by
// time, so vehicles survive the gaps.
class PipelineWorker final : public QObject {
    Q_OBJECT
public:
    // Called on the worker thread, because model sessions and database connections belong there.
    using Factory = std::function<PassagePipeline::Dependencies()>;

    PipelineWorker(Factory factory, PlateFormatRegistry formats, PipelineConfig config);
    ~PipelineWorker() override;

    void submit(const Frame& frame);  // thread safe

public slots:
    void initialize();
    void setLine(double ax, double ay, double bx, double by);
    void finish();
    void shutdown();  // releases sessions and connections on the thread that created them

signals:
    void frameProcessed(const QImage& image, const QVector<vpr::Track>& tracks,
                        const vpr::PassageCounters& counters, qint64 droppedFrames);
    void passage(const vpr::PassageEvent& event);
    void countersChanged(const vpr::PassageCounters& counters);
    void failed(const QString& message);
    void finished();

private:
    void processLatest();

    Factory m_factory;
    PlateFormatRegistry m_formats;
    PipelineConfig m_config;
    std::unique_ptr<PassagePipeline> m_pipeline;

    std::mutex m_mutex;
    std::optional<Frame> m_latest;
    bool m_scheduled = false;
    qint64 m_dropped = 0;
};

} // namespace vpr::qt

Q_DECLARE_METATYPE(vpr::Track)
Q_DECLARE_METATYPE(vpr::PassageCounters)
Q_DECLARE_METATYPE(vpr::PassageEvent)
