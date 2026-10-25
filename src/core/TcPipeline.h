#pragma once

#include "Models.h"

#include <QHash>
#include <QObject>

namespace mos {
class ICfsBridge;
class IProtocolAdapter;
namespace persist {
class Database;
}
}

namespace mos {

/** validate → encode → egress → history + status. */
class TcPipeline : public QObject {
  Q_OBJECT
 public:
  TcPipeline(persist::Database* db, IProtocolAdapter* adapter, ICfsBridge* bridge,
             QObject* parent = nullptr);

  bool send(const QString& assetId, const QString& mnemonic, const QString& argsJson,
            QString* error = nullptr);

 public slots:
  void onCommandAck(const QString& assetId, const QString& commandId, bool success,
                    const QString& detail);

 signals:
  void commandUpdated(const CommandResult& result);
  void commandError(const QString& message);

 private:
  persist::Database* db_ = nullptr;
  IProtocolAdapter* adapter_ = nullptr;
  ICfsBridge* bridge_ = nullptr;
  QHash<QString, Telecommand> inflight_;
};

}  // namespace mos
