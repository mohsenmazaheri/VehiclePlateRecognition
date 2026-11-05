#include "evidence_sink.h"

#include "qt_image.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace vpr::qt {

const char* statusName(PassageStatus status)
{
    switch (status) {
    case PassageStatus::Read: return "read";
    case PassageStatus::UnknownFormat: return "unknown_format";
    case PassageStatus::NotVisible: return "not_visible";
    }
    return "not_visible";
}

EvidenceSink::EvidenceSink(QString directory) : m_directory(std::move(directory))
{
    QDir().mkpath(m_directory);
}

QString EvidenceSink::baseName(const PassageEvent& event)
{
    return QStringLiteral("passage_%1_%2").arg(event.timestampMs).arg(event.trackId);
}

QString EvidenceSink::vehiclePhotoPath(const QString& directory, const PassageEvent& event)
{
    return QDir(directory).filePath(baseName(event) + QStringLiteral("_vehicle.jpg"));
}

void EvidenceSink::onPassage(const PassageEvent& event)
{
    const QDir dir(m_directory);
    const QString base = baseName(event);

    QJsonObject json;
    json[QStringLiteral("timestampMs")] = static_cast<qint64>(event.timestampMs);
    json[QStringLiteral("trackId")] = event.trackId;
    json[QStringLiteral("status")] = QString::fromLatin1(statusName(event.status));
    json[QStringLiteral("direction")] = event.direction == CrossingDirection::Forward
        ? QStringLiteral("forward") : QStringLiteral("backward");
    json[QStringLiteral("plate")] = QString::fromStdString(event.plateText);
    json[QStringLiteral("plateDisplay")] = QString::fromStdString(event.displayText);
    json[QStringLiteral("country")] = QString::fromStdString(event.countryCode);
    json[QStringLiteral("confidence")] = static_cast<double>(event.confidence);

    if (!event.vehicleImage.empty()) {
        const QString path = vehiclePhotoPath(m_directory, event);
        if (toQImage(event.vehicleImage).save(path, "JPG", 90))
            json[QStringLiteral("vehiclePhoto")] = QDir(m_directory).relativeFilePath(path);
    }
    if (!event.plateImage.empty()) {
        const QString path = dir.filePath(base + QStringLiteral("_plate.jpg"));
        if (toQImage(event.plateImage).save(path, "JPG", 95))
            json[QStringLiteral("platePhoto")] = QDir(m_directory).relativeFilePath(path);
    }

    QFile file(dir.filePath(base + QStringLiteral(".json")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(json).toJson());
}

} // namespace vpr::qt
