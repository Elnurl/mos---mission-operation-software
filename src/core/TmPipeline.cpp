#include "TmPipeline.h"

#include "Database.h"
#include "IProtocolAdapter.h"

namespace mos {

TmPipeline::TmPipeline(persist::Database* db, IProtocolAdapter* adapter, QObject* parent)
    : QObject(parent), db_(db), adapter_(adapter)
{
}

void TmPipeline::onRawTelemetry(const QString& assetId, const QByteArray& payload)
{
  RawFrame frame;
  frame.assetId = assetId;
  frame.payload = payload;
  frame.receivedUtc = QDateTime::currentDateTimeUtc();
  frame.source = QStringLiteral("bridge");

  QVector<Parameter> params;
  QString error;
  if (!adapter_->decodeTelemetry(frame, &params, &error)) {
    emit ingestError(error);
    return;
  }

  for (const auto& p : params) {
    latest_.insert(p.name, p);
    if (!db_->insertTmSample(p, &error)) {
      emit ingestError(error);
    }
  }
  emit parametersUpdated(assetId, params);
}

}  // namespace mos
