#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace mos {

/**
 * Southbound cFS I/O boundary (I-CFS).
 *
 * Slice A ships StubCfsBridge + SimTmGenerator so the UI path works without a
 * live cFS. A real bridge implementation should speak the chosen cFS ground
 * interface (CI/TO/custom) behind this same API — no UI changes required.
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
