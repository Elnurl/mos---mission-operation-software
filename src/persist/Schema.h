#pragma once

#include <QSqlDatabase>
#include <QString>

namespace mos::persist {

/** Apply / migrate SQLite schema for config + TM/TC history. */
bool applySchema(QSqlDatabase& db, QString* error = nullptr);

QString schemaVersion();

}  // namespace mos::persist
