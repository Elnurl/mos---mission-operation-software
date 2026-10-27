#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

namespace mos {
namespace ccsds_lab {

/** True if datagram looks like CCSDS version-0 primary header. */
bool looksLikeCcsds(const QByteArray& datagram);

struct PrimaryHeader {
  quint16 streamId = 0;
  quint8 version = 0;
  quint8 packetType = 0;  // 0=TM, 1=TC
  quint16 seqField = 0;
  quint16 length = 0;
  quint16 apid() const { return streamId & 0x07FFu; }
  quint16 seqCount() const { return seqField & 0x3FFFu; }
};

std::optional<PrimaryHeader> parsePrimaryHeader(const QByteArray& datagram);

/** Classic sample-mission command MIDs / function codes (cpu1). */
constexpr quint16 kToLabCmdMid = 0x1880;
constexpr quint16 kSampleAppCmdMid = 0x1882;
constexpr quint8 kToLabEnableCc = 6;
constexpr quint8 kNoopCc = 0;

/** Pack a cFE-style CCSDS telecommand (checksummed). */
QByteArray packCommand(quint16 streamId, quint8 cmdCode, const QByteArray& payload = {},
                       quint16 seq = 0);

QByteArray packToLabEnable(const QString& destIp = QStringLiteral("127.0.0.1"),
                           quint16 seq = 0, int destIpWidth = 16);

QByteArray packSampleNoop(quint16 seq = 0);
QByteArray packToLabNoop(quint16 seq = 0);

/**
 * Map MOS mnemonic → CCSDS lab command. Empty QByteArray if unknown.
 * Recognized: NOOP, TO_ENABLE / ENABLE_TLM, TO_NOOP, CI_NOOP.
 */
QByteArray encodeMnemonic(const QString& mnemonic, const QString& argsJson = QStringLiteral("{}"),
                          quint16 seq = 0, const QString& destIp = QStringLiteral("127.0.0.1"));

}  // namespace ccsds_lab
}  // namespace mos
