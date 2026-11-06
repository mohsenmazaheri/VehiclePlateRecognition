#pragma once
#include <QImage>
#include <QString>
#include <QWidget>

// Shows one picture as large as fits, keeping its shape, or a short text when there is none.
class ImageBox final : public QWidget {
public:
    explicit ImageBox(QString placeholder, QWidget* parent = nullptr);

    void setImage(const QImage& image, const QString& missingText = {});
    void setPlaceholder(const QString& text);  // shown after clear()
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_image;
    QString m_placeholder;
    QString m_text;
};
