#pragma once

#include "ICfsBridge.h"

#include <QHostAddress>
#include <QUdpSocket>

namespace mos {

/**
 * UDP southbound bridge aimed at NASA cFS lab ground ports (ci_lab / to_lab style):
 *   - MOS listens for TM (and MOSACK) on tmListenPort (default 1235)
 *   - MOS sends TC datagrams to tcHost:tcPort (default 127.0.0.1:1234)
 *
 * Default wire payload remains MOST TM / stub TC (harness + translator).
 * Set MOS_CFS_WIRE=ccsds to translate stub TC → CCSDS on send, accept raw
 * CCSDS TM, auto-send TO_LAB_ENABLE (unless MOS_CFS_AUTO_ENABLE=0), and
 * synthesize MOSACK (live cFS has no MOSACK). Preferred live path: keep MOST
 * wire and run scripts/most-ccsds-translator.py (see scripts/cfs-sim-notes.md).
 *
 * Env overrides (read at start()):
 *   MOS_CFS_TM_PORT, MOS_CFS_TC_HOST, MOS_CFS_TC_PORT, MOS_CFS_TM_BIND
 *   MOS_CFS_WIRE, MOS_CFS_AUTO_ENABLE, MOS_TO_DEST_IP
 */
class UdpCfsBridge : public ICfsBridge {
  Q_OBJECT
 public:
  explicit UdpCfsBridge(QString assetId, QObject* parent = nullptr);

  void setEndpoints(const QHostAddress& tmBind, quint16 tmListenPort,
                    const QHostAddress& tcHost, quint16 tcPort);

  bool start(QString* error = nullptr) override;
  void stop() override;
  bool isRunning() const override;
  bool sendTelecommand(const QString& assetId, const QByteArray& encoded,
                       QString* error = nullptr) override;

  quint16 tmListenPort() const { return tmListenPort_; }
  quint16 tcPort() const { return tcPort_; }
  QHostAddress tcHost() const { return tcHost_; }

  /** Parse MOSACK|cmdId|0|detail or MOSACK|cmdId|1|detail. */
  static bool tryParseAck(const QByteArray& datagram, QString* commandId, bool* success,
                          QString* detail);

 private slots:
  void onReadyRead();

 private:
  void applyEnvOverrides();

  QString assetId_;
  QHostAddress tmBind_{QHostAddress::AnyIPv4};
  quint16 tmListenPort_ = 1235;
  QHostAddress tcHost_{QHostAddress::LocalHost};
  quint16 tcPort_ = 1234;
  QUdpSocket socket_;
  bool running_ = false;
  quint16 cmdSeq_ = 0;
};

}  // namespace mos
