#include "CfsAdapter.h"

#include "CcsdsLab.h"
#include "StubCfsBridge.h"

#include <QDataStream>
#include <QVariant>

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

bool decodeMost(const RawFrame& frame, QVector<Parameter>* out, QString* error)
{
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

bool decodeCcsds(const RawFrame& frame, QVector<Parameter>* out, QString* error)
{
  const auto hdr = ccsds_lab::parsePrimaryHeader(frame.payload);
  if (!hdr) {
    if (error) {
      *error = QStringLiteral("invalid CCSDS primary header");
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
    p.sequence = hdr->seqCount();
    out->push_back(p);
  };

  add(QStringLiteral("SEQ"), QVariant::fromValue(hdr->seqCount()), QStringLiteral(""));
  add(QStringLiteral("CCSDS_MID"),
      QStringLiteral("0x%1").arg(hdr->streamId, 4, 16, QLatin1Char('0')).toUpper(),
      QStringLiteral(""));
  add(QStringLiteral("CCSDS_APID"), QVariant::fromValue(hdr->apid()), QStringLiteral(""));
  add(QStringLiteral("CCSDS_LEN"), QVariant::fromValue(hdr->length), QStringLiteral(""));
  add(QStringLiteral("CCSDS_TYPE"),
      hdr->packetType == 0 ? QStringLiteral("TM") : QStringLiteral("TC"), QStringLiteral(""));
  add(QStringLiteral("RAW_BYTES"), QVariant::fromValue(frame.payload.size()), QStringLiteral("B"));
  // Familiar columns when talking raw lab CCSDS (translator prefers MOST).
  add(QStringLiteral("BUS_V"), QVariant::fromValue(static_cast<double>(hdr->apid())),
      QStringLiteral("APID"));
  add(QStringLiteral("BATT_SOC"), QVariant::fromValue(static_cast<double>(hdr->length)),
      QStringLiteral(""));
  add(QStringLiteral("TEMP_BRD"),
      QVariant::fromValue(static_cast<double>(qMax(0, frame.payload.size() - 6))),
      QStringLiteral("B"));
  add(QStringLiteral("MODE"),
      QStringLiteral("MID_0x%1").arg(hdr->streamId, 4, 16, QLatin1Char('0')).toUpper(),
      QStringLiteral(""));
  return true;
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

  if (frame.payload.size() >= 4) {
    const auto* p = reinterpret_cast<const uchar*>(frame.payload.constData());
    const quint32 magic = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) |
                          quint32(p[3]);
    if (magic == kMagic) {
      return decodeMost(frame, out, error);
    }
  }
  if (ccsds_lab::looksLikeCcsds(frame.payload)) {
    return decodeCcsds(frame, out, error);
  }
  if (error) {
    *error = QStringLiteral("unrecognized TM (not MOST or CCSDS)");
  }
  return false;
}

bool CfsAdapter::encodeTelecommand(const Telecommand& tc, QByteArray* out, QString* error)
{
  if (!out) {
    if (error) {
      *error = QStringLiteral("null out");
    }
    return false;
  }
  // Always stub-encode (cmdId|mnemonic|args) so bridges can correlate MOSACK.
  // UdpCfsBridge with MOS_CFS_WIRE=ccsds translates mnemonic → CCSDS on the wire.
  *out = StubCfsBridge::encodeStubCommand(tc.id, tc.name, tc.argsJson);
  return true;
}

}  // namespace mos
