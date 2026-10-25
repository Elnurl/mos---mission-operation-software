#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace mos {

/**
 * Southbound cFS I/O boundary (I-CFS).
 *
 * Implementations:
 *   - StubCfsBridge + SimTmGenerator — offline desktop demo (default)
 *   - UdpCfsBridge — UDP path aligned with cFS ci_lab/to_lab ports (1234/1235)
 *
 * Select at runtime: MOS_BRIDGE=stub|udp or mos-desktop --bridge=stub|udp.
 */
class ICfsBridge : public QObject {
  Q_OBJECT
 public:
  explicit ICfsBridge(QObject* parent = nullptr) : QObject(parent) {}
  ~ICfsBridge() override = default;

  virtual bool start(QString* error = nullptr) = 0;
  virtual void stop() = 0;
  virtual bool isRunning() const = 0;

  /** Send an encoded telecommand frame toward cFS. */
  virtual bool sendTelecommand(const QString& assetId, const QByteArray& encoded,
                               QString* error = nullptr) = 0;

 signals:
  /** Raw TM bytes from the link (sim or real). */
  void telemetryReceived(const QString& assetId, const QByteArray& payload);

  /** Command ack / status from the link. */
  void commandAck(const QString& assetId, const QString& commandId, bool success,
                  const QString& detail);

  void linkStateChanged(const QString& assetId, bool connected);
};

}  // namespace mos
