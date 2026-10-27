#pragma once

#include "IProtocolAdapter.h"

namespace mos {

/**
 * Protocol pack: MOST frames (stub/harness/translator) plus raw CCSDS lab TM
 * (ci_lab/to_lab). TC encode stays stub `cmdId|mnemonic|args` for ack correlation;
 * set MOS_CFS_WIRE=ccsds on UdpCfsBridge to emit CCSDS on the wire.
 */
class CfsAdapter : public IProtocolAdapter {
  Q_OBJECT
 public:
  explicit CfsAdapter(QObject* parent = nullptr);

  QString packId() const override { return QStringLiteral("cfs-sim-v1"); }
  QString packVersion() const override { return QStringLiteral("0.1.0"); }

  bool decodeTelemetry(const RawFrame& frame, QVector<Parameter>* out,
                       QString* error = nullptr) override;
  bool encodeTelecommand(const Telecommand& tc, QByteArray* out,
                         QString* error = nullptr) override;
};

}  // namespace mos
