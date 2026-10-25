#include "Session.h"

namespace mos {

Session::Session(QObject* parent) : QObject(parent) {}

Session::~Session()
{
  close();
}

bool Session::open(const QString& dbPath, QString* error)
{
  close();

  db_ = std::make_unique<persist::Database>(QStringLiteral("mos_session"));
  if (!db_->open(dbPath, error)) {
    return false;
  }

  assets_ = std::make_unique<AssetRegistry>(db_.get(), this);
  if (!assets_->bootstrapDefaultSc1(error)) {
    return false;
  }

  const QString assetId = primaryAssetId();
  adapter_ = std::make_unique<CfsAdapter>(this);
  bridge_ = std::make_unique<StubCfsBridge>(assetId, this);
  tm_ = std::make_unique<TmPipeline>(db_.get(), adapter_.get(), this);
  tc_ = std::make_unique<TcPipeline>(db_.get(), adapter_.get(), bridge_.get(), this);

  connect(bridge_.get(), &ICfsBridge::telemetryReceived, tm_.get(), &TmPipeline::onRawTelemetry);
  connect(bridge_.get(), &ICfsBridge::commandAck, tc_.get(), &TcPipeline::onCommandAck);
  connect(bridge_.get(), &ICfsBridge::linkStateChanged, this,
          [this](const QString& /*assetId*/, bool connected) {
            linkState_ = connected ? LinkState::Connected : LinkState::Disconnected;
            emit linkStateChanged(linkState_);
            emit opsLog(connected ? QStringLiteral("Link UP (stub/sim)")
                                  : QStringLiteral("Link DOWN"));
          });
  connect(tm_.get(), &TmPipeline::ingestError, this,
          [this](const QString& msg) { emit opsLog(QStringLiteral("TM error: %1").arg(msg)); });
  connect(tc_.get(), &TcPipeline::commandError, this,
          [this](const QString& msg) { emit opsLog(QStringLiteral("TC error: %1").arg(msg)); });

  emit opsLog(QStringLiteral("Session open — asset %1, pack %2")
                  .arg(assetId, adapter_->packId()));
  return true;
}

void Session::close()
{
  stopLink();
  tc_.reset();
  tm_.reset();
  bridge_.reset();
  adapter_.reset();
  assets_.reset();
  if (db_) {
    db_->close();
    db_.reset();
  }
}

bool Session::startLink(QString* error)
{
  if (!bridge_) {
    if (error) {
      *error = QStringLiteral("session not open");
    }
    return false;
  }
  linkState_ = LinkState::Connecting;
  emit linkStateChanged(linkState_);
  if (!bridge_->start(error)) {
    linkState_ = LinkState::Disconnected;
    emit linkStateChanged(linkState_);
    return false;
  }
  return true;
}

void Session::stopLink()
{
  if (bridge_) {
    bridge_->stop();
  }
}

QString Session::primaryAssetId() const
{
  if (!assets_) {
    return QStringLiteral("SC-1");
  }
  if (auto a = assets_->primary()) {
    return a->id;
  }
  return QStringLiteral("SC-1");
}

}  // namespace mos
