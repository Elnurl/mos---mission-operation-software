#pragma once

#include <QDateTime>
#include <QString>
#include <QVariant>
#include <QVector>
#include <cstdint>

namespace mos {

/** First-class spacecraft identity (SYS-020 / multi-sat extension point). */
struct Asset {
  QString id;          // e.g. "SC-1"
  QString name;
  QString className;   // bus/class — configuration, not a fork
  bool active = true;
};

/** Normalized live/history parameter sample from TM decode. */
struct Parameter {
  QString assetId;
  QString name;
  QVariant value;
  QString unit;
  QDateTime timestampUtc;
  quint64 sequence = 0;
};

/** Opaque southbound frame with enough metadata for adapters. */
struct RawFrame {
  QString assetId;
  QByteArray payload;
  QDateTime receivedUtc;
  QString source;  // "sim" | "cfs-bridge" | …
};

enum class CommandStatus {
  Queued,
  Sent,
  Acked,
  Failed,
  Timeout
};

inline QString commandStatusToString(CommandStatus s)
{
  switch (s) {
  case CommandStatus::Queued: return QStringLiteral("Queued");
  case CommandStatus::Sent: return QStringLiteral("Sent");
  case CommandStatus::Acked: return QStringLiteral("Acked");
  case CommandStatus::Failed: return QStringLiteral("Failed");
  case CommandStatus::Timeout: return QStringLiteral("Timeout");
  }
  return QStringLiteral("Unknown");
}

struct Telecommand {
  QString id;       // local UUID / sequence
  QString assetId;
  QString name;     // mnemonic, e.g. NOOP
  QString argsJson; // free-form stub args
  QDateTime createdUtc;
};

struct CommandResult {
  QString commandId;
  QString assetId;
  CommandStatus status = CommandStatus::Queued;
  QString detail;
  QDateTime updatedUtc;
};

enum class LinkState {
  Disconnected,
  Connecting,
  Connected,
  Degraded
};

inline QString linkStateToString(LinkState s)
{
  switch (s) {
  case LinkState::Disconnected: return QStringLiteral("Disconnected");
  case LinkState::Connecting: return QStringLiteral("Connecting");
  case LinkState::Connected: return QStringLiteral("Connected");
  case LinkState::Degraded: return QStringLiteral("Degraded");
  }
  return QStringLiteral("Unknown");
}

}  // namespace mos
