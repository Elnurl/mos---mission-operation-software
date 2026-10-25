#include "Schema.h"

#include <QSqlError>
#include <QSqlQuery>

namespace mos::persist {

QString schemaVersion()
{
  return QStringLiteral("1");
}

bool applySchema(QSqlDatabase& db, QString* error)
{
  QSqlQuery q(db);

  const char* statements[] = {
      "CREATE TABLE IF NOT EXISTS meta ("
      "  key TEXT PRIMARY KEY,"
      "  value TEXT NOT NULL"
      ")",

      "CREATE TABLE IF NOT EXISTS assets ("
      "  id TEXT PRIMARY KEY,"
      "  name TEXT NOT NULL,"
      "  class_name TEXT NOT NULL DEFAULT '',"
      "  active INTEGER NOT NULL DEFAULT 1"
      ")",

      "CREATE TABLE IF NOT EXISTS config ("
      "  key TEXT PRIMARY KEY,"
      "  value TEXT NOT NULL"
      ")",

      "CREATE TABLE IF NOT EXISTS tm_samples ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  asset_id TEXT NOT NULL,"
      "  name TEXT NOT NULL,"
      "  value TEXT NOT NULL,"
      "  unit TEXT NOT NULL DEFAULT '',"
      "  sequence INTEGER NOT NULL DEFAULT 0,"
      "  ts_utc TEXT NOT NULL,"
      "  FOREIGN KEY(asset_id) REFERENCES assets(id)"
      ")",

      "CREATE TABLE IF NOT EXISTS tc_history ("
      "  id TEXT PRIMARY KEY,"
      "  asset_id TEXT NOT NULL,"
      "  name TEXT NOT NULL,"
      "  args_json TEXT NOT NULL DEFAULT '',"
      "  status TEXT NOT NULL,"
      "  detail TEXT NOT NULL DEFAULT '',"
      "  created_utc TEXT NOT NULL,"
      "  updated_utc TEXT NOT NULL,"
      "  FOREIGN KEY(asset_id) REFERENCES assets(id)"
      ")",

      "CREATE INDEX IF NOT EXISTS idx_tm_asset_ts ON tm_samples(asset_id, ts_utc)",
      "CREATE INDEX IF NOT EXISTS idx_tc_asset_ts ON tc_history(asset_id, created_utc)",
  };

  for (const char* sql : statements) {
    if (!q.exec(QLatin1String(sql))) {
      if (error) {
        *error = q.lastError().text();
      }
      return false;
    }
  }

  q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', ?)"));
  q.addBindValue(schemaVersion());
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }

  return true;
}

}  // namespace mos::persist
