#pragma once

#include "IProtocolAdapter.h"

namespace mos {

/** First protocol pack: decodes stub/sim MOST frames; encodes stub TC for bridge. */
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
