#include "AssetRegistry.h"

#include "Database.h"

namespace mos {

AssetRegistry::AssetRegistry(persist::Database* db, QObject* parent)
    : QObject(parent), db_(db)
{
}

bool AssetRegistry::bootstrapDefaultSc1(QString* error)
{
  Asset sc1;
  sc1.id = QStringLiteral("SC-1");
  sc1.name = QStringLiteral("Spacecraft 1");
  sc1.className = QStringLiteral("generic-cubesat");
  sc1.active = true;
  if (!db_->ensureDefaultAsset(sc1, error)) {
    return false;
  }
  return reload(error);
}

std::optional<Asset> AssetRegistry::primary() const
{
  for (const auto& a : assets_) {
    if (a.active) {
      return a;
    }
  }
  if (!assets_.isEmpty()) {
    return assets_.front();
  }
  return std::nullopt;
}

bool AssetRegistry::reload(QString* error)
{
  QString localError;
  assets_ = db_->listAssets(error ? error : &localError);
  emit assetsChanged();
  if (error && !error->isEmpty()) {
    return false;
  }
  return true;
}

}  // namespace mos
