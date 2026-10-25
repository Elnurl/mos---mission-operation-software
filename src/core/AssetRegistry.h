#pragma once

#include "Models.h"

#include <QObject>
#include <QVector>
#include <optional>

namespace mos::persist {
class Database;
}

namespace mos {

class AssetRegistry : public QObject {
  Q_OBJECT
 public:
  explicit AssetRegistry(persist::Database* db, QObject* parent = nullptr);

  bool bootstrapDefaultSc1(QString* error = nullptr);
  std::optional<Asset> primary() const;
  QVector<Asset> assets() const { return assets_; }
  bool reload(QString* error = nullptr);

 signals:
  void assetsChanged();

 private:
  persist::Database* db_ = nullptr;
  QVector<Asset> assets_;
};

}  // namespace mos
