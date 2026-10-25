#pragma once

#include "AssetRegistry.h"
#include "CfsAdapter.h"
#include "Database.h"
#include "StubCfsBridge.h"
#include "TcPipeline.h"
#include "TmPipeline.h"

#include <QObject>
#include <memory>

namespace mos {

/**
 * Phase 2 collapsed desktop+core session.
 * Owns persist, adapter, stub bridge, and TM/TC pipelines.
 */
class Session : public QObject {
  Q_OBJECT
 public:
  explicit Session(QObject* parent = nullptr);
  ~Session() override;

  bool open(const QString& dbPath, QString* error = nullptr);
  void close();

  bool startLink(QString* error = nullptr);
  void stopLink();

  persist::Database* database() { return db_.get(); }
  AssetRegistry* assets() { return assets_.get(); }
  TmPipeline* tm() { return tm_.get(); }
  TcPipeline* tc() { return tc_.get(); }
  ICfsBridge* bridge() { return bridge_.get(); }
  CfsAdapter* adapter() { return adapter_.get(); }

  QString primaryAssetId() const;
  LinkState linkState() const { return linkState_; }

 signals:
  void linkStateChanged(LinkState state);
  void opsLog(const QString& line);

 private:
  std::unique_ptr<persist::Database> db_;
  std::unique_ptr<AssetRegistry> assets_;
  std::unique_ptr<CfsAdapter> adapter_;
  std::unique_ptr<StubCfsBridge> bridge_;
  std::unique_ptr<TmPipeline> tm_;
  std::unique_ptr<TcPipeline> tc_;
  LinkState linkState_ = LinkState::Disconnected;
};

}  // namespace mos
