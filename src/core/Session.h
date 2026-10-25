#pragma once

#include "AssetRegistry.h"
#include "CfsAdapter.h"
#include "Database.h"
#include "ICfsBridge.h"
#include "TcPipeline.h"
#include "TmPipeline.h"

#include <QObject>
#include <memory>
#include <optional>

namespace mos {

enum class BridgeMode {
  Stub,
  Udp
};

inline QString bridgeModeToString(BridgeMode m)
{
  switch (m) {
  case BridgeMode::Stub: return QStringLiteral("stub");
  case BridgeMode::Udp: return QStringLiteral("udp");
  }
  return QStringLiteral("unknown");
}

/** Parse "stub" / "udp" (case-insensitive). Empty → nullopt. */
inline std::optional<BridgeMode> parseBridgeMode(const QString& text)
{
  const QString t = text.trimmed().toLower();
  if (t.isEmpty()) {
    return std::nullopt;
  }
  if (t == QLatin1String("stub") || t == QLatin1String("sim")) {
    return BridgeMode::Stub;
  }
  if (t == QLatin1String("udp") || t == QLatin1String("cfs") || t == QLatin1String("real")) {
    return BridgeMode::Udp;
  }
  return std::nullopt;
}

/**
 * Phase 2 collapsed desktop+core session.
 * Owns persist, adapter, bridge (stub or UDP), and TM/TC pipelines.
 */
class Session : public QObject {
  Q_OBJECT
 public:
  explicit Session(QObject* parent = nullptr);
  ~Session() override;

  bool open(const QString& dbPath, BridgeMode bridgeMode = BridgeMode::Stub,
            QString* error = nullptr);
  void close();

  bool startLink(QString* error = nullptr);
  void stopLink();

  persist::Database* database() { return db_.get(); }
  AssetRegistry* assets() { return assets_.get(); }
  TmPipeline* tm() { return tm_.get(); }
  TcPipeline* tc() { return tc_.get(); }
  ICfsBridge* bridge() { return bridge_.get(); }
  CfsAdapter* adapter() { return adapter_.get(); }

  BridgeMode bridgeMode() const { return bridgeMode_; }
  QString primaryAssetId() const;
  LinkState linkState() const { return linkState_; }

 signals:
  void linkStateChanged(LinkState state);
  void opsLog(const QString& line);

 private:
  std::unique_ptr<ICfsBridge> createBridge(const QString& assetId);

  std::unique_ptr<persist::Database> db_;
  std::unique_ptr<AssetRegistry> assets_;
  std::unique_ptr<CfsAdapter> adapter_;
  std::unique_ptr<ICfsBridge> bridge_;
  std::unique_ptr<TmPipeline> tm_;
  std::unique_ptr<TcPipeline> tc_;
  BridgeMode bridgeMode_ = BridgeMode::Stub;
  LinkState linkState_ = LinkState::Disconnected;
};

}  // namespace mos
