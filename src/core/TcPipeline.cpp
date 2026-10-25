#include "TcPipeline.h"

#include "Database.h"
#include "ICfsBridge.h"
#include "IProtocolAdapter.h"

#include <QUuid>

namespace mos {

TcPipeline::TcPipeline(persist::Database* db, IProtocolAdapter* adapter, ICfsBridge* bridge,
                       QObject* parent)
    : QObject(parent), db_(db), adapter_(adapter), bridge_(bridge)
{
}

bool TcPipeline::send(const QString& assetId, const QString& mnemonic, const QString& argsJson,
                      QString* error)
{
  Telecommand tc;
  tc.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  tc.assetId = assetId;
  tc.name = mnemonic.trimmed().isEmpty() ? QStringLiteral("NOOP") : mnemonic.trimmed();
  tc.argsJson = argsJson;
  tc.createdUtc = QDateTime::currentDateTimeUtc();

  QByteArray encoded;
  if (!adapter_->encodeTelecommand(tc, &encoded, error)) {
    return false;
  }

  CommandResult result;
  result.commandId = tc.id;
  result.assetId = assetId;
  result.status = CommandStatus::Queued;
  result.detail = QStringLiteral("queued");
  result.updatedUtc = tc.createdUtc;
  if (!db_->upsertCommand(tc, result, error)) {
    return false;
  }
  inflight_.insert(tc.id, tc);
  emit commandUpdated(result);

  if (!bridge_->sendTelecommand(assetId, encoded, error)) {
    result.status = CommandStatus::Failed;
    result.detail = error ? *error : QStringLiteral("send failed");
    result.updatedUtc = QDateTime::currentDateTimeUtc();
    db_->upsertCommand(tc, result, error);
    emit commandUpdated(result);
    return false;
  }

  result.status = CommandStatus::Sent;
  result.detail = QStringLiteral("sent to bridge");
  result.updatedUtc = QDateTime::currentDateTimeUtc();
  db_->upsertCommand(tc, result, error);
  emit commandUpdated(result);
  return true;
}

void TcPipeline::onCommandAck(const QString& assetId, const QString& commandId, bool success,
                              const QString& detail)
{
  auto it = inflight_.find(commandId);
  Telecommand tc;
  if (it != inflight_.end()) {
    tc = it.value();
    inflight_.erase(it);
  } else {
    tc.id = commandId;
    tc.assetId = assetId;
    tc.name = QStringLiteral("?");
    tc.createdUtc = QDateTime::currentDateTimeUtc();
  }

  CommandResult result;
  result.commandId = commandId;
  result.assetId = assetId;
  result.status = success ? CommandStatus::Acked : CommandStatus::Failed;
  result.detail = detail;
  result.updatedUtc = QDateTime::currentDateTimeUtc();

  QString error;
  db_->upsertCommand(tc, result, &error);
  emit commandUpdated(result);
}

}  // namespace mos
