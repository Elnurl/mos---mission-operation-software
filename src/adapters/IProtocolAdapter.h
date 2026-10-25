#pragma once

#include "Models.h"

#include <QByteArray>
#include <QObject>
#include <QVector>

namespace mos {

/**
 * Protocol pack boundary (I-ADAPTER).
 * Slice A: one cFS-oriented stub pack. Future CSP/SOL packs implement this.
 */
class IProtocolAdapter : public QObject {
  Q_OBJECT
 public:
  explicit IProtocolAdapter(QObject* parent = nullptr) : QObject(parent) {}
  ~IProtocolAdapter() override = default;

  virtual QString packId() const = 0;
  virtual QString packVersion() const = 0;

  virtual bool decodeTelemetry(const RawFrame& frame, QVector<Parameter>* out,
                               QString* error = nullptr) = 0;

  virtual bool encodeTelecommand(const Telecommand& tc, QByteArray* out,
                                 QString* error = nullptr) = 0;
};

}  // namespace mos
