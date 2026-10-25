#pragma once

#include "ICfsBridge.h"
#include "SimTmGenerator.h"

#include <QHash>
#include <QTimer>

namespace mos {

/**
 * Stub cFS bridge: feeds UI from SimTmGenerator and fakes TC acks.
 * Replace with a real socket/file CI-TO client implementing ICfsBridge.
 */
class StubCfsBridge : public ICfsBridge {
  Q_OBJECT
 public:
  explicit StubCfsBridge(QString assetId, QObject* parent = nullptr);

  bool start(QString* error = nullptr) override;
  void stop() override;
  bool isRunning() const override;
  bool sendTelecommand(const QString& assetId, const QByteArray& encoded,
                       QString* error = nullptr) override;

  /** Injected into encoded TC for ack correlation (UTF-8 command id prefix). */
  static QByteArray encodeStubCommand(const QString& commandId, const QString& mnemonic,
                                      const QString& argsJson);

 private:
  QString assetId_;
  SimTmGenerator sim_;
  bool running_ = false;
  QHash<QString, QTimer*> pendingAcks_;
};

}  // namespace mos
