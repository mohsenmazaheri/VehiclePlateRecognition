#include "image_box.h"

#include <QPainter>

#include <utility>

ImageBox::ImageBox(QString placeholder, QWidget* parent)
    : QWidget(parent), m_placeholder(std::move(placeholder)), m_text(m_placeholder)
{
}

void ImageBox::setImage(const QImage& image, const QString& missingText)
{
    m_image = image;
    m_text = missingText;
    update();
}

void ImageBox::setPlaceholder(const QString& text)
{
    m_placeholder = text;
}

void ImageBox::clear()
{
    m_image = {};
    m_text = m_placeholder;
    update();
}

void ImageBox::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    if (m_image.isNull()) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_text);
        return;
    }
    // Plate crops are small, so they are scaled up as well as down.
    const QSize size = m_image.size().scaled(this->size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, m_image);
}
