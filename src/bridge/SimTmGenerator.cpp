#include "SimTmGenerator.h"

#include <QDataStream>
#include <QIODevice>
#include <cmath>

namespace mos {

SimTmGenerator::SimTmGenerator(QString assetId, QObject* parent)
    : QObject(parent), assetId_(std::move(assetId))
{
  connect(&timer_, &QTimer::timeout, this, &SimTmGenerator::tick);
}

void SimTmGenerator::start(int intervalMs)
{
  sequence_ = 0;
  timer_.start(intervalMs);
  tick();
}

void SimTmGenerator::stop()
{
  timer_.stop();
}

void SimTmGenerator::tick()
{
  ++sequence_;
  const double t = static_cast<double>(sequence_);
  // Simple housekeeping-ish values for the TM table.
  const float busVoltage = static_cast<float>(28.0 + 0.5 * std::sin(t / 5.0));
  const float batterySoc = static_cast<float>(75.0 + 5.0 * std::sin(t / 20.0));
  const float tempBoard = static_cast<float>(22.0 + 3.0 * std::sin(t / 12.0));
  const quint32 mode = (sequence_ / 30) % 3;  // 0 IDLE, 1 NOMINAL, 2 SAFE

  QByteArray payload;
  QDataStream out(&payload, QIODevice::WriteOnly);
  out.setByteOrder(QDataStream::BigEndian);
  out.setFloatingPointPrecision(QDataStream::SinglePrecision);
  // Magic + version so a future real bridge can coexist.
  out << quint32(0x4D4F5354);  // 'MOST'
  out << quint16(1);           // version
  out << sequence_;
  out << busVoltage << batterySoc << tempBoard << mode;

  emit frameReady(assetId_, payload);
}

}  // namespace mos
