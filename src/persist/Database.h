#pragma once

#include "Schema.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <optional>

#include <QDateTime>
#include <QVariant>

namespace mos {
struct Asset;
struct Parameter;
struct Telecommand;
struct CommandResult;
}

namespace mos::persist {

class Database {
 public:
  explicit Database(QString connectionName = QStringLiteral("mos"));
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  bool open(const QString& path, QString* error = nullptr);
  void close();
  bool isOpen() const;

  bool ensureDefaultAsset(const mos::Asset& asset, QString* error = nullptr);
  std::optional<mos::Asset> loadAsset(const QString& id, QString* error = nullptr) const;
  QVector<mos::Asset> listAssets(QString* error = nullptr) const;

  bool insertTmSample(const mos::Parameter& p, QString* error = nullptr);
  QVector<mos::Parameter> recentTm(const QString& assetId, int limit = 100,
                                   QString* error = nullptr) const;

  bool upsertCommand(const mos::Telecommand& tc, const mos::CommandResult& result,
                     QString* error = nullptr);
  QVector<mos::CommandResult> recentCommands(const QString& assetId, int limit = 50,
                                             QString* error = nullptr) const;

  QString path() const { return path_; }

 private:
  QString connectionName_;
  QString path_;
  QSqlDatabase db_;
};

}  // namespace mos::persist
