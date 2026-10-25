#pragma once

#include <QObject>
#include <QTimer>
#include <cstdint>

namespace mos {

/**
 * Lightweight simulated TM publisher for Slice A UI unblock.
 * Emits compact binary frames that CfsAdapter can decode.
 */
class SimTmGenerator : public QObject {
  Q_OBJECT
 public:
  explicit SimTmGenerator(QString assetId, QObject* parent = nullptr);

  void start(int intervalMs = 1000);
  void stop();
  bool isRunning() const { return timer_.isActive(); }

 signals:
  void frameReady(const QString& assetId, const QByteArray& payload);

 private:
  void tick();

  QString assetId_;
  QTimer timer_;
  quint32 sequence_ = 0;
};

}  // namespace mos
