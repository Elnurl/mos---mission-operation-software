#include "MainWindow.h"
#include "Session.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("MOS"));
  QApplication::setOrganizationName(QStringLiteral("MOS"));
  QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

  const QString dataDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dataDir);
  const QString dbPath = dataDir + QStringLiteral("/mos.sqlite");

  mos::Session session;
  QString error;
  if (!session.open(dbPath, &error)) {
    QMessageBox::critical(nullptr, QStringLiteral("MOS"),
                          QStringLiteral("Failed to open database:\n%1\n%2").arg(dbPath, error));
    return 1;
  }
  if (!session.startLink(&error)) {
    QMessageBox::critical(nullptr, QStringLiteral("MOS"),
                          QStringLiteral("Failed to start stub bridge:\n%1").arg(error));
    return 1;
  }

  mos::MainWindow window(&session);
  window.show();
  return app.exec();
}
