#include "sqlite_passage_repository.h"

#include "evidence_sink.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace vpr::qt {

SqlitePassageRepository::SqlitePassageRepository(const QString& databasePath, QString evidenceDirectory)
    : m_connection(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_evidenceDirectory(std::move(evidenceDirectory))
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(databasePath);
    m_open = db.open();
    if (m_open) {
        QSqlQuery query(db);
        m_open = query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS passages ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " timestamp_ms INTEGER NOT NULL,"
            " status INTEGER NOT NULL,"
            " plate TEXT NOT NULL,"
            " display TEXT NOT NULL,"
            " country TEXT NOT NULL,"
            " confidence REAL NOT NULL,"
            " vehicle_photo TEXT NOT NULL)"));
    }
}

SqlitePassageRepository::~SqlitePassageRepository()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

void SqlitePassageRepository::onPassage(const PassageEvent& event)
{
    if (!m_open)
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection, false));
    query.prepare(QStringLiteral(
        "INSERT INTO passages (timestamp_ms, status, plate, display, country, confidence, vehicle_photo)"
        " VALUES (?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(static_cast<qint64>(event.timestampMs));
    query.addBindValue(static_cast<int>(event.status));
    query.addBindValue(QString::fromStdString(event.plateText));
    query.addBindValue(QString::fromStdString(event.displayText));
    query.addBindValue(QString::fromStdString(event.countryCode));
    query.addBindValue(static_cast<double>(event.confidence));
    query.addBindValue(event.vehicleImage.empty()
                           ? QString()
                           : EvidenceSink::vehiclePhotoPath(m_evidenceDirectory, event));
    query.exec();
}

std::vector<StoredPassage> SqlitePassageRepository::search(const PassageQuery& request)
{
    std::vector<StoredPassage> rows;
    if (!m_open)
        return rows;
    QSqlQuery query(QSqlDatabase::database(m_connection, false));
    query.prepare(QStringLiteral(
        "SELECT id, timestamp_ms, status, plate, display, country, confidence, vehicle_photo"
        " FROM passages WHERE plate LIKE ? ESCAPE '\\' ORDER BY id DESC LIMIT ?"));
    QString needle = QString::fromStdString(request.plateContains);
    needle.remove(QLatin1Char(' '));
    needle.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    needle.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    needle.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    query.addBindValue(QLatin1Char('%') + needle + QLatin1Char('%'));
    query.addBindValue(request.limit);
    if (!query.exec())
        return rows;
    while (query.next()) {
        StoredPassage row;
        row.id = query.value(0).toLongLong();
        row.timestampMs = query.value(1).toLongLong();
        row.status = static_cast<PassageStatus>(query.value(2).toInt());
        row.plateText = query.value(3).toString().toStdString();
        row.displayText = query.value(4).toString().toStdString();
        row.countryCode = query.value(5).toString().toStdString();
        row.confidence = query.value(6).toFloat();
        row.vehicleImagePath = query.value(7).toString().toStdString();
        rows.push_back(std::move(row));
    }
    return rows;
}

int SqlitePassageRepository::purgeOlderThan(std::int64_t timestampMs)
{
    if (!m_open)
        return 0;
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);

    // Retention must remove the photo too, not only the row that points to it.
    QSqlQuery photos(db);
    photos.prepare(QStringLiteral("SELECT vehicle_photo FROM passages WHERE timestamp_ms < ?"));
    photos.addBindValue(static_cast<qint64>(timestampMs));
    if (photos.exec()) {
        while (photos.next()) {
            const QString vehicle = photos.value(0).toString();
            if (vehicle.isEmpty())
                continue;
            QFile::remove(vehicle);
            QString base = vehicle;
            base.chop(QStringLiteral("_vehicle.jpg").size());
            QFile::remove(base + QStringLiteral("_plate.jpg"));
            QFile::remove(base + QStringLiteral(".json"));
        }
    }

    QSqlQuery remove(db);
    remove.prepare(QStringLiteral("DELETE FROM passages WHERE timestamp_ms < ?"));
    remove.addBindValue(static_cast<qint64>(timestampMs));
    return remove.exec() ? remove.numRowsAffected() : 0;
}

} // namespace vpr::qt
