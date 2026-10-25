#include "Database.h"

#include "../core/Models.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace mos::persist {

Database::Database(QString connectionName)
    : connectionName_(std::move(connectionName))
{
}

Database::~Database()
{
  close();
}

bool Database::open(const QString& path, QString* error)
{
  close();
  path_ = path;

  if (QSqlDatabase::contains(connectionName_)) {
    db_ = QSqlDatabase::database(connectionName_);
  } else {
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
  }
  db_.setDatabaseName(path_);

  if (!db_.open()) {
    if (error) {
      *error = db_.lastError().text();
    }
    return false;
  }

  QSqlQuery pragma(db_);
  pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

  return applySchema(db_, error);
}

void Database::close()
{
  if (db_.isOpen()) {
    db_.close();
  }
  // Keep connection registered for process lifetime; reopen overwrites.
}

bool Database::isOpen() const
{
  return db_.isOpen();
}

bool Database::ensureDefaultAsset(const mos::Asset& asset, QString* error)
{
  QSqlQuery q(db_);
  q.prepare(QStringLiteral(
      "INSERT INTO assets(id, name, class_name, active) VALUES(?,?,?,?) "
      "ON CONFLICT(id) DO UPDATE SET name=excluded.name, class_name=excluded.class_name, "
      "active=excluded.active"));
  q.addBindValue(asset.id);
  q.addBindValue(asset.name);
  q.addBindValue(asset.className);
  q.addBindValue(asset.active ? 1 : 0);
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }
  return true;
}

std::optional<mos::Asset> Database::loadAsset(const QString& id, QString* error) const
{
  QSqlQuery q(db_);
  q.prepare(QStringLiteral("SELECT id, name, class_name, active FROM assets WHERE id=?"));
  q.addBindValue(id);
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return std::nullopt;
  }
  if (!q.next()) {
    return std::nullopt;
  }
  mos::Asset a;
  a.id = q.value(0).toString();
  a.name = q.value(1).toString();
  a.className = q.value(2).toString();
  a.active = q.value(3).toInt() != 0;
  return a;
}

QVector<mos::Asset> Database::listAssets(QString* error) const
{
  QVector<mos::Asset> out;
  QSqlQuery q(db_);
  if (!q.exec(QStringLiteral("SELECT id, name, class_name, active FROM assets ORDER BY id"))) {
    if (error) {
      *error = q.lastError().text();
    }
    return out;
  }
  while (q.next()) {
    mos::Asset a;
    a.id = q.value(0).toString();
    a.name = q.value(1).toString();
    a.className = q.value(2).toString();
    a.active = q.value(3).toInt() != 0;
    out.push_back(a);
  }
  return out;
}

bool Database::insertTmSample(const mos::Parameter& p, QString* error)
{
  QSqlQuery q(db_);
  q.prepare(QStringLiteral(
      "INSERT INTO tm_samples(asset_id, name, value, unit, sequence, ts_utc) "
      "VALUES(?,?,?,?,?,?)"));
  q.addBindValue(p.assetId);
  q.addBindValue(p.name);
  q.addBindValue(p.value.toString());
  // Qt null QString binds as SQL NULL; unitless params must store "".
  q.addBindValue(p.unit.isNull() ? QStringLiteral("") : p.unit);
  q.addBindValue(static_cast<qint64>(p.sequence));
  q.addBindValue(p.timestampUtc.toUTC().toString(Qt::ISODateWithMs));
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }
  return true;
}

QVector<mos::Parameter> Database::recentTm(const QString& assetId, int limit,
                                           QString* error) const
{
  QVector<mos::Parameter> out;
  QSqlQuery q(db_);
  q.prepare(QStringLiteral(
      "SELECT asset_id, name, value, unit, sequence, ts_utc FROM tm_samples "
      "WHERE asset_id=? ORDER BY id DESC LIMIT ?"));
  q.addBindValue(assetId);
  q.addBindValue(limit);
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return out;
  }
  while (q.next()) {
    mos::Parameter p;
    p.assetId = q.value(0).toString();
    p.name = q.value(1).toString();
    p.value = q.value(2);
    p.unit = q.value(3).toString();
    p.sequence = static_cast<quint64>(q.value(4).toLongLong());
    p.timestampUtc = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
    p.timestampUtc.setTimeSpec(Qt::UTC);
    out.push_back(p);
  }
  return out;
}

bool Database::upsertCommand(const mos::Telecommand& tc, const mos::CommandResult& result,
                             QString* error)
{
  QSqlQuery q(db_);
  q.prepare(QStringLiteral(
      "INSERT INTO tc_history(id, asset_id, name, args_json, status, detail, created_utc, updated_utc) "
      "VALUES(?,?,?,?,?,?,?,?) "
      "ON CONFLICT(id) DO UPDATE SET status=excluded.status, detail=excluded.detail, "
      "updated_utc=excluded.updated_utc"));
  q.addBindValue(tc.id);
  q.addBindValue(tc.assetId);
  q.addBindValue(tc.name);
  q.addBindValue(tc.argsJson);
  q.addBindValue(commandStatusToString(result.status));
  q.addBindValue(result.detail);
  q.addBindValue(tc.createdUtc.toUTC().toString(Qt::ISODateWithMs));
  q.addBindValue(result.updatedUtc.toUTC().toString(Qt::ISODateWithMs));
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }
  return true;
}

QVector<mos::CommandResult> Database::recentCommands(const QString& assetId, int limit,
                                                     QString* error) const
{
  QVector<mos::CommandResult> out;
  QSqlQuery q(db_);
  q.prepare(QStringLiteral(
      "SELECT id, asset_id, status, detail, updated_utc FROM tc_history "
      "WHERE asset_id=? ORDER BY created_utc DESC LIMIT ?"));
  q.addBindValue(assetId);
  q.addBindValue(limit);
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return out;
  }
  while (q.next()) {
    mos::CommandResult r;
    r.commandId = q.value(0).toString();
    r.assetId = q.value(1).toString();
    const QString st = q.value(2).toString();
    if (st == QLatin1String("Acked")) {
      r.status = mos::CommandStatus::Acked;
    } else if (st == QLatin1String("Sent")) {
      r.status = mos::CommandStatus::Sent;
    } else if (st == QLatin1String("Failed")) {
      r.status = mos::CommandStatus::Failed;
    } else if (st == QLatin1String("Timeout")) {
      r.status = mos::CommandStatus::Timeout;
    } else {
      r.status = mos::CommandStatus::Queued;
    }
    r.detail = q.value(3).toString();
    r.updatedUtc = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    r.updatedUtc.setTimeSpec(Qt::UTC);
    out.push_back(r);
  }
  return out;
}

}  // namespace mos::persist
