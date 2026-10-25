#include "MainWindow.h"
#include "Session.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

namespace {

mos::BridgeMode resolveBridgeMode(const QCommandLineParser& parser, QString* error)
{
  if (parser.isSet(QStringLiteral("bridge"))) {
    const QString cli = parser.value(QStringLiteral("bridge"));
    auto m = mos::parseBridgeMode(cli);
    if (!m) {
      if (error) {
        *error = QStringLiteral("unknown --bridge=%1 (use stub|udp)").arg(cli);
      }
      return mos::BridgeMode::Stub;
    }
    return *m;
  }
  const QByteArray env = qgetenv("MOS_BRIDGE");
  if (!env.isEmpty()) {
    auto m = mos::parseBridgeMode(QString::fromUtf8(env));
    if (!m) {
      if (error) {
        *error = QStringLiteral("unknown MOS_BRIDGE=%1 (use stub|udp)")
                     .arg(QString::fromUtf8(env));
      }
      return mos::BridgeMode::Stub;
    }
    return *m;
  }
  return mos::BridgeMode::Stub;
}

}  // namespace

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("MOS"));
  QApplication::setOrganizationName(QStringLiteral("MOS"));
  QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("MOS desktop — Slice A TM/TC (stub or UDP cFS bridge)"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption(
      {{QStringLiteral("b"), QStringLiteral("bridge")},
       QStringLiteral("Southbound bridge: stub (default) or udp"),
       QStringLiteral("mode")});
  parser.process(app);

  QString modeError;
  const mos::BridgeMode mode = resolveBridgeMode(parser, &modeError);
  if (!modeError.isEmpty()) {
    QMessageBox::critical(nullptr, QStringLiteral("MOS"), modeError);
    return 2;
  }

  const QString dataDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dataDir);
  const QString dbPath = dataDir + QStringLiteral("/mos.sqlite");

  mos::Session session;
  QString error;
  if (!session.open(dbPath, mode, &error)) {
    QMessageBox::critical(nullptr, QStringLiteral("MOS"),
                          QStringLiteral("Failed to open database:\n%1\n%2").arg(dbPath, error));
    return 1;
  }
  if (!session.startLink(&error)) {
    QMessageBox::critical(
        nullptr, QStringLiteral("MOS"),
        QStringLiteral("Failed to start %1 bridge:\n%2")
            .arg(mos::bridgeModeToString(mode), error));
    return 1;
  }

  mos::MainWindow window(&session);
  window.show();
  return app.exec();
}
