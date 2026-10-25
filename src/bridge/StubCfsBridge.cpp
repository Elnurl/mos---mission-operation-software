#include "StubCfsBridge.h"

#include <QUuid>

namespace mos {

StubCfsBridge::StubCfsBridge(QString assetId, QObject* parent)
    : ICfsBridge(parent), assetId_(std::move(assetId)), sim_(assetId_)
{
  connect(&sim_, &SimTmGenerator::frameReady, this,
          [this](const QString& assetId, const QByteArray& payload) {
            emit telemetryReceived(assetId, payload);
          });
}

bool StubCfsBridge::start(QString* /*error*/)
{
  if (running_) {
    return true;
  }
  running_ = true;
  sim_.start(1000);
  emit linkStateChanged(assetId_, true);
  return true;
}

void StubCfsBridge::stop()
{
  sim_.stop();
  for (auto* t : pendingAcks_) {
    t->stop();
    t->deleteLater();
  }
  pendingAcks_.clear();
  if (running_) {
    running_ = false;
    emit linkStateChanged(assetId_, false);
  }
}

bool StubCfsBridge::isRunning() const
{
  return running_;
}

QByteArray StubCfsBridge::encodeStubCommand(const QString& commandId, const QString& mnemonic,
                                            const QString& argsJson)
{
  // Compact stub wire format — real adapter will replace this.
  const QString line = QStringLiteral("%1|%2|%3").arg(commandId, mnemonic, argsJson);
  return line.toUtf8();
}

bool StubCfsBridge::sendTelecommand(const QString& assetId, const QByteArray& encoded,
                                    QString* error)
{
  if (!running_) {
    if (error) {
      *error = QStringLiteral("bridge not running");
    }
    return false;
  }
  if (assetId != assetId_) {
    if (error) {
      *error = QStringLiteral("unknown asset for stub bridge");
    }
    return false;
  }

  const QString text = QString::fromUtf8(encoded);
  const QString commandId = text.section(QLatin1Char('|'), 0, 0);
  if (commandId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("missing command id in stub encoding");
    }
    return false;
  }

  // Simulated ground→FSW latency then success ack.
  auto* timer = new QTimer(this);
  timer->setSingleShot(true);
  pendingAcks_.insert(commandId, timer);
  connect(timer, &QTimer::timeout, this, [this, assetId, commandId, timer]() {
    pendingAcks_.remove(commandId);
    timer->deleteLater();
    emit commandAck(assetId, commandId, true, QStringLiteral("stub ACK"));
  });
  timer->start(350);
  return true;
}

}  // namespace mos
