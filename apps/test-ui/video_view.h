#pragma once
#include "domain/types.h"

#include <QImage>
#include <QPointF>
#include <QVector>
#include <QWidget>

// Shows the frame with its tracks. The counting line can be dragged by either end.
class VideoView final : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);

    void showFrame(const QImage& image, const QVector<vpr::Track>& tracks);
    void clear();
    void setLine(QPointF a, QPointF b);
    [[nodiscard]] QPointF lineA() const { return m_a; }
    [[nodiscard]] QPointF lineB() const { return m_b; }

signals:
    void lineChanged(double ax, double ay, double bx, double by);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] QRectF imageRect() const;

    QImage m_image;
    QVector<vpr::Track> m_tracks;
    QPointF m_a{0.05, 0.8};
    QPointF m_b{0.95, 0.8};
    QPointF* m_dragging = nullptr;
};
