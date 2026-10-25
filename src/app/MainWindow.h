#pragma once

#include "Session.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace mos {

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(Session* session, QWidget* parent = nullptr);

 private slots:
  void onParametersUpdated(const QString& assetId, const QVector<Parameter>& params);
  void onCommandUpdated(const CommandResult& result);
  void onLinkStateChanged(LinkState state);
  void onOpsLog(const QString& line);
  void sendCommand();

 private:
  void buildUi();
  void refreshTmTable();
  void appendTcRow(const CommandResult& result, const QString& nameHint);

  QPointer<Session> session_;
  QLabel* assetLabel_ = nullptr;
  QLabel* linkLabel_ = nullptr;
  QTableWidget* tmTable_ = nullptr;
  QTableWidget* tcTable_ = nullptr;
  QLineEdit* tcNameEdit_ = nullptr;
  QLineEdit* tcArgsEdit_ = nullptr;
  QPushButton* sendButton_ = nullptr;
  QPlainTextEdit* logView_ = nullptr;
  QHash<QString, Parameter> latest_;
};

}  // namespace mos
