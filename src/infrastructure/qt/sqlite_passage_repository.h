#pragma once
#include "domain/ports.h"

#include <QString>

namespace vpr::qt {

// Local SQLite store. Use one instance per thread: Qt database connections are not shareable.
// evidenceDirectory is the folder where EvidenceSink keeps the photos.
class SqlitePassageRepository final : public IPassageRepository, public IPassageSink {
public:
    SqlitePassageRepository(const QString& databasePath, QString evidenceDirectory);
    ~SqlitePassageRepository() override;

    SqlitePassageRepository(const SqlitePassageRepository&) = delete;
    SqlitePassageRepository& operator=(const SqlitePassageRepository&) = delete;

    void onPassage(const PassageEvent& event) override;
    [[nodiscard]] std::vector<StoredPassage> search(const PassageQuery& query) override;
    int purgeOlderThan(std::int64_t timestampMs) override;

    [[nodiscard]] bool isOpen() const { return m_open; }

private:
    QString m_connection;
    QString m_evidenceDirectory;
    bool m_open = false;
};

} // namespace vpr::qt
