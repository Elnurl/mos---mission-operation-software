#include "UdpCfsBridge.h"

#include <QByteArray>
#include <QNetworkDatagram>

namespace mos {

namespace {
bool looksLikeMostTm(const QByteArray& d)
{
  if (d.size() < 4) {
    return false;
  }
  const auto* p = reinterpret_cast<const uchar*>(d.constData());
  const quint32 magic = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) |
                        quint32(p[3]);
  return magic == 0x4D4F5354u;  // 'MOST'
}
}  // namespace

UdpCfsBridge::UdpCfsBridge(QString assetId, QObject* parent)
    : ICfsBridge(parent), assetId_(std::move(assetId))
{
  connect(&socket_, &QUdpSocket::readyRead, this, &UdpCfsBridge::onReadyRead);
}

void UdpCfsBridge::setEndpoints(const QHostAddress& tmBind, quint16 tmListenPort,
                                const QHostAddress& tcHost, quint16 tcPort)
{
  tmBind_ = tmBind;
  tmListenPort_ = tmListenPort;
  tcHost_ = tcHost;
  tcPort_ = tcPort;
}

void UdpCfsBridge::applyEnvOverrides()
{
  if (const QByteArray v = qgetenv("MOS_CFS_TM_PORT"); !v.isEmpty()) {
    bool ok = false;
    const int p = QString::fromUtf8(v).toInt(&ok);
    if (ok && p > 0 && p < 65536) {
      tmListenPort_ = static_cast<quint16>(p);
    }
  }
  if (const QByteArray v = qgetenv("MOS_CFS_TC_PORT"); !v.isEmpty()) {
    bool ok = false;
    const int p = QString::fromUtf8(v).toInt(&ok);
    if (ok && p > 0 && p < 65536) {
      tcPort_ = static_cast<quint16>(p);
    }
  }
  if (const QByteArray v = qgetenv("MOS_CFS_TC_HOST"); !v.isEmpty()) {
    const QHostAddress addr(QString::fromUtf8(v));
    if (!addr.isNull()) {
      tcHost_ = addr;
    }
  }
  if (const QByteArray v = qgetenv("MOS_CFS_TM_BIND"); !v.isEmpty()) {
    const QString s = QString::fromUtf8(v);
    if (s == QLatin1String("0.0.0.0") || s == QLatin1String("*")) {
      tmBind_ = QHostAddress::AnyIPv4;
    } else {
      const QHostAddress addr(s);
      if (!addr.isNull()) {
        tmBind_ = addr;
      }
    }
  }
}

bool UdpCfsBridge::start(QString* error)
{
  if (running_) {
    return true;
  }
  applyEnvOverrides();

  if (!socket_.bind(tmBind_, tmListenPort_,
                    QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
    if (error) {
      *error = QStringLiteral("UDP bind failed on %1:%2 — %3")
                   .arg(tmBind_.toString())
                   .arg(tmListenPort_)
                   .arg(socket_.errorString());
    }
    return false;
  }

  running_ = true;
  emit linkStateChanged(assetId_, true);
  return true;
}

void UdpCfsBridge::stop()
{
  if (socket_.state() != QAbstractSocket::UnconnectedState) {
    socket_.close();
  }
  if (running_) {
    running_ = false;
    emit linkStateChanged(assetId_, false);
  }
}

bool UdpCfsBridge::isRunning() const
{
  return running_;
}

bool UdpCfsBridge::sendTelecommand(const QString& assetId, const QByteArray& encoded,
                                   QString* error)
{
  if (!running_) {
    if (error) {
      *error = QStringLiteral("UDP bridge not running");
    }
    return false;
  }
  if (assetId != assetId_) {
    if (error) {
      *error = QStringLiteral("unknown asset for UDP bridge");
    }
    return false;
  }
  const qint64 n = socket_.writeDatagram(encoded, tcHost_, tcPort_);
  if (n != encoded.size()) {
    if (error) {
      *error = QStringLiteral("UDP TC send failed: %1").arg(socket_.errorString());
    }
    return false;
  }
  return true;
}

bool UdpCfsBridge::tryParseAck(const QByteArray& datagram, QString* commandId, bool* success,
                               QString* detail)
{
  const QString text = QString::fromUtf8(datagram).trimmed();
  if (!text.startsWith(QLatin1String("MOSACK|"))) {
    return false;
  }
  const QStringList parts = text.split(QLatin1Char('|'));
  // MOSACK|cmdId|0or1|detail...
  if (parts.size() < 3) {
    return false;
  }
  if (commandId) {
    *commandId = parts.at(1);
  }
  if (success) {
    *success = (parts.at(2) == QLatin1String("1") ||
                parts.at(2).compare(QLatin1String("ok"), Qt::CaseInsensitive) == 0 ||
                parts.at(2).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
  }
  if (detail) {
    *detail = parts.size() >= 4 ? parts.mid(3).join(QLatin1Char('|'))
                                : QStringLiteral("udp ack");
  }
  return !commandId || !commandId->isEmpty();
}

void UdpCfsBridge::onReadyRead()
{
  while (socket_.hasPendingDatagrams()) {
    const QNetworkDatagram dg = socket_.receiveDatagram();
    const QByteArray data = dg.data();
    if (data.isEmpty()) {
      continue;
    }

    QString commandId;
    bool ok = false;
    QString detail;
    if (tryParseAck(data, &commandId, &ok, &detail)) {
      emit commandAck(assetId_, commandId, ok, detail);
      continue;
    }

    if (looksLikeMostTm(data)) {
      emit telemetryReceived(assetId_, data);
      continue;
    }

    // Unknown datagram — ignore (keeps bridge robust against stray UDP).
  }
}

}  // namespace mos
