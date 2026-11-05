#include "pipeline_worker.h"

#include "qt_image.h"

#include <exception>

namespace vpr::qt {

namespace {

// Forwards to the configured sink and notifies the UI.
class TeeSink final : public IPassageSink {
public:
    TeeSink(std::shared_ptr<IPassageSink> next, PipelineWorker* worker) : m_next(std::move(next)), m_worker(worker) {}

    void onPassage(const PassageEvent& event) override
    {
        emit m_worker->passage(event);  // the display first, writing photos takes longer
        m_next->onPassage(event);
    }

private:
    std::shared_ptr<IPassageSink> m_next;
    PipelineWorker* m_worker;
};

} // namespace

PipelineWorker::PipelineWorker(Factory factory, PlateFormatRegistry formats, PipelineConfig config)
    : m_factory(std::move(factory)), m_formats(std::move(formats)), m_config(config)
{
    qRegisterMetaType<vpr::Track>();
    qRegisterMetaType<QVector<vpr::Track>>();
    qRegisterMetaType<vpr::PassageCounters>();
    qRegisterMetaType<vpr::PassageEvent>();
}

PipelineWorker::~PipelineWorker() = default;

void PipelineWorker::initialize()
{
    PassagePipeline::Dependencies deps = m_factory();
    deps.sink = std::make_shared<TeeSink>(deps.sink, this);
    m_pipeline = std::make_unique<PassagePipeline>(std::move(deps), m_formats, m_config);
}

void PipelineWorker::submit(const Frame& frame)
{
    std::lock_guard lock(m_mutex);
    if (m_latest)
        ++m_dropped;
    m_latest = frame;
    if (!m_scheduled) {
        m_scheduled = true;
        QMetaObject::invokeMethod(this, &PipelineWorker::processLatest, Qt::QueuedConnection);
    }
}

void PipelineWorker::processLatest()
{
    std::optional<Frame> frame;
    qint64 dropped = 0;
    {
        std::lock_guard lock(m_mutex);
        frame.swap(m_latest);
        m_scheduled = false;
        dropped = m_dropped;
    }
    if (!frame || !m_pipeline)
        return;

    TrackUpdate update;
    try {
        update = m_pipeline->process(*frame);
    } catch (const std::exception& error) {
        emit failed(QString::fromUtf8(error.what()));
        return;
    }
    emit frameProcessed(toQImage(frame->image),
                        QVector<vpr::Track>(update.active.begin(), update.active.end()),
                        m_pipeline->counters(), dropped);
}

void PipelineWorker::setLine(double ax, double ay, double bx, double by)
{
    m_config.line = CountingLine{{ax, ay}, {bx, by}};
    if (m_pipeline)
        m_pipeline->setLine(m_config.line);
}

void PipelineWorker::finish()
{
    processLatest();
    if (m_pipeline) {
        m_pipeline->flush();
        emit countersChanged(m_pipeline->counters());
    }
    emit finished();
}

void PipelineWorker::shutdown()
{
    m_pipeline.reset();
}

} // namespace vpr::qt
