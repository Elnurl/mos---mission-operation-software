#include "CcsdsLab.h"

#include <QByteArray>

namespace mos {
namespace ccsds_lab {
namespace {

quint8 cmdChecksum(const QByteArray& packet)
{
  QByteArray data = packet;
  if (data.size() < 8) {
    return 0;
  }
  data[7] = static_cast<char>(0);
  quint8 x = 0;
  for (unsigned char b : data) {
    x ^= b;
  }
  return static_cast<quint8>(x ^ 0xFFu);
}

void appendBe16(QByteArray* out, quint16 v)
{
  out->append(static_cast<char>((v >> 8) & 0xFF));
  out->append(static_cast<char>(v & 0xFF));
}

}  // namespace

bool looksLikeCcsds(const QByteArray& datagram)
{
  if (datagram.size() < 6) {
    return false;
  }
  const auto version = static_cast<quint8>((static_cast<quint8>(datagram.at(0)) >> 5) & 0x7);
  return version == 0;
}

std::optional<PrimaryHeader> parsePrimaryHeader(const QByteArray& datagram)
{
  if (!looksLikeCcsds(datagram)) {
    return std::nullopt;
  }
  PrimaryHeader h;
  const auto* p = reinterpret_cast<const uchar*>(datagram.constData());
  h.streamId = static_cast<quint16>((p[0] << 8) | p[1]);
  h.seqField = static_cast<quint16>((p[2] << 8) | p[3]);
  h.length = static_cast<quint16>((p[4] << 8) | p[5]);
  h.version = static_cast<quint8>((h.streamId >> 13) & 0x7);
  h.packetType = static_cast<quint8>((h.streamId >> 12) & 0x1);
  return h;
}

QByteArray packCommand(quint16 streamId, quint8 cmdCode, const QByteArray& payload, quint16 seq)
{
  const quint16 seqField = static_cast<quint16>(0xC000u | (seq & 0x3FFFu));
  const int total = 6 + 2 + payload.size();
  const quint16 length = static_cast<quint16>(total - 7);
  QByteArray pkt;
  pkt.reserve(total);
  appendBe16(&pkt, streamId);
  appendBe16(&pkt, seqField);
  appendBe16(&pkt, length);
  pkt.append(static_cast<char>(cmdCode & 0x7F));
  pkt.append(static_cast<char>(0));
  pkt.append(payload);
  pkt[7] = static_cast<char>(cmdChecksum(pkt));
  return pkt;
}

QByteArray packToLabEnable(const QString& destIp, quint16 seq, int destIpWidth)
{
  if (destIpWidth != 16 && destIpWidth != 18) {
    destIpWidth = 16;
  }
  QByteArray raw = destIp.toLatin1();
  if (raw.size() >= destIpWidth) {
    raw = raw.left(destIpWidth - 1);
  }
  while (raw.size() < destIpWidth) {
    raw.append('\0');
  }
  return packCommand(kToLabCmdMid, kToLabEnableCc, raw, seq);
}

QByteArray packSampleNoop(quint16 seq)
{
  return packCommand(kSampleAppCmdMid, kNoopCc, {}, seq);
}

QByteArray packToLabNoop(quint16 seq)
{
  return packCommand(kToLabCmdMid, kNoopCc, {}, seq);
}

QByteArray encodeMnemonic(const QString& mnemonic, const QString& /*argsJson*/, quint16 seq,
                          const QString& destIp)
{
  const QString m = mnemonic.trimmed().toUpper();
  if (m == QLatin1String("NOOP") || m == QLatin1String("SAMPLE_NOOP") ||
      m == QLatin1String("SAMPLE_APP_NOOP")) {
    return packSampleNoop(seq);
  }
  if (m == QLatin1String("TO_NOOP") || m == QLatin1String("TO_LAB_NOOP")) {
    return packToLabNoop(seq);
  }
  if (m == QLatin1String("TO_ENABLE") || m == QLatin1String("TO_LAB_ENABLE") ||
      m == QLatin1String("ENABLE_TLM") || m == QLatin1String("ENABLE_TELEMETRY")) {
    return packToLabEnable(destIp, seq, 16);
  }
  if (m == QLatin1String("CI_NOOP") || m == QLatin1String("CI_LAB_NOOP")) {
    return packCommand(0x1884, kNoopCc, {}, seq);
  }
  return {};
}

}  // namespace ccsds_lab
}  // namespace mos
