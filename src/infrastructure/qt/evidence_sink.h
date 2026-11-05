#pragma once
#include "domain/ports.h"

#include <QString>

namespace vpr::qt {

// Writes one JSON file and the photos of each passage into a folder.
class EvidenceSink final : public IPassageSink {
public:
    explicit EvidenceSink(QString directory);
    void onPassage(const PassageEvent& event) override;

    [[nodiscard]] static QString baseName(const PassageEvent& event);
    [[nodiscard]] static QString vehiclePhotoPath(const QString& directory, const PassageEvent& event);

private:
    QString m_directory;
};

[[nodiscard]] const char* statusName(PassageStatus status);

} // namespace vpr::qt
