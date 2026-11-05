#pragma once
#include "domain/ports.h"
#include "simulation/synthetic_scene.h"

#include <QObject>
#include <QTimer>

#include <memory>

namespace vpr::qt {

// Paints the synthetic scene frame by frame. timerIntervalMs = 0 runs as fast as possible (tests).
class SyntheticFrameSource final : public QObject, public IFrameSource {
    Q_OBJECT
public:
    SyntheticFrameSource(std::shared_ptr<const sim::SyntheticScene> scene, int frameStepMs = 40,
                         int timerIntervalMs = 40, QObject* parent = nullptr);

    void start(FrameHandler onFrame, ErrorHandler onError, FinishedHandler onFinished) override;
    void stop() override;

private:
    void tick();

    std::shared_ptr<const sim::SyntheticScene> m_scene;
    int m_frameStepMs;
    QTimer m_timer;
    FrameHandler m_onFrame;
    FinishedHandler m_onFinished;
    std::int64_t m_index = 0;
};

} // namespace vpr::qt
