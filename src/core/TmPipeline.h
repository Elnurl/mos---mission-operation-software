#pragma once

#include "Models.h"

#include <QHash>
#include <QObject>
#include <QVector>

namespace mos {
class IProtocolAdapter;
namespace persist {
class Database;
}
}

namespace mos {

/** ingest → decode → store → publish live parameters. */
class TmPipeline : public QObject {
  Q_OBJECT
 public:
  TmPipeline(persist::Database* db, IProtocolAdapter* adapter, QObject* parent = nullptr);

  QHash<QString, Parameter> latestByName() const { return latest_; }

 public slots:
  void onRawTelemetry(const QString& assetId, const QByteArray& payload);

 signals:
  void parametersUpdated(const QString& assetId, const QVector<Parameter>& params);
  void ingestError(const QString& message);

 private:
  persist::Database* db_ = nullptr;
  IProtocolAdapter* adapter_ = nullptr;
  QHash<QString, Parameter> latest_;
};

}  // namespace mos
