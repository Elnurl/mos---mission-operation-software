#include "CfsAdapter.h"

#include "StubCfsBridge.h"

#include <QDataStream>

namespace mos {

namespace {
constexpr quint32 kMagic = 0x4D4F5354;  // MOST

QString modeName(quint32 mode)
{
  switch (mode) {
  case 0: return QStringLiteral("IDLE");
  case 1: return QStringLiteral("NOMINAL");
  case 2: return QStringLiteral("SAFE");
  default: return QStringLiteral("UNKNOWN");
  }
}
}  // namespace

CfsAdapter::CfsAdapter(QObject* parent) : IProtocolAdapter(parent) {}

bool CfsAdapter::decodeTelemetry(const RawFrame& frame, QVector<Parameter>* out,
                                 QString* error)
{
  if (!out) {
    if (error) {
      *error = QStringLiteral("null out");
    }
    return false;
  }
  out->clear();

  QDataStream in(frame.payload);
  in.setByteOrder(QDataStream::BigEndian);
  in.setFloatingPointPrecision(QDataStream::SinglePrecision);

  quint32 magic = 0;
  quint16 version = 0;
  quint32 sequence = 0;
  float busVoltage = 0;
  float batterySoc = 0;
  float tempBoard = 0;
  quint32 mode = 0;

  in >> magic >> version >> sequence >> busVoltage >> batterySoc >> tempBoard >> mode;
  if (in.status() != QDataStream::Ok || magic != kMagic || version != 1) {
    if (error) {
      *error = QStringLiteral("invalid MOST TM frame");
    }
    return false;
  }

  auto add = [&](const QString& name, const QVariant& value, const QString& unit) {
    Parameter p;
    p.assetId = frame.assetId;
    p.name = name;
    p.value = value;
    p.unit = unit;
    p.timestampUtc = frame.receivedUtc;
    p.sequence = sequence;
    out->push_back(p);
  };

  add(QStringLiteral("SEQ"), QVariant::fromValue(sequence), QStringLiteral(""));
  add(QStringLiteral("BUS_V"), busVoltage, QStringLiteral("V"));
  add(QStringLiteral("BATT_SOC"), batterySoc, QStringLiteral("%"));
  add(QStringLiteral("TEMP_BRD"), tempBoard, QStringLiteral("C"));
  add(QStringLiteral("MODE"), modeName(mode), QStringLiteral(""));
  return true;
}

bool CfsAdapter::encodeTelecommand(const Telecommand& tc, QByteArray* out, QString* error)
{
  if (!out) {
    if (error) {
      *error = QStringLiteral("null out");
    }
    return false;
  }
  *out = StubCfsBridge::encodeStubCommand(tc.id, tc.name, tc.argsJson);
  return true;
}

}  // namespace mos
