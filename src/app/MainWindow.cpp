#include "MainWindow.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace mos {

MainWindow::MainWindow(Session* session, QWidget* parent)
    : QMainWindow(parent), session_(session)
{
  setWindowTitle(QStringLiteral("MOS — Mission Operations Software"));
  resize(960, 640);
  buildUi();

  connect(session_, &Session::linkStateChanged, this, &MainWindow::onLinkStateChanged);
  connect(session_, &Session::opsLog, this, &MainWindow::onOpsLog);
  connect(session_->tm(), &TmPipeline::parametersUpdated, this, &MainWindow::onParametersUpdated);
  connect(session_->tc(), &TcPipeline::commandUpdated, this, &MainWindow::onCommandUpdated);

  onLinkStateChanged(session_->linkState());
  onOpsLog(QStringLiteral("UI ready — bridge=%1").arg(bridgeModeToString(session_->bridgeMode())));
}

void MainWindow::buildUi()
{
  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);

  auto* header = new QHBoxLayout();
  assetLabel_ = new QLabel(this);
  assetLabel_->setText(QStringLiteral("Asset: %1").arg(session_->primaryAssetId()));
  QFont assetFont = assetLabel_->font();
  assetFont.setPointSize(assetFont.pointSize() + 2);
  assetFont.setBold(true);
  assetLabel_->setFont(assetFont);

  linkLabel_ = new QLabel(this);
  header->addWidget(assetLabel_);
  header->addStretch();
  header->addWidget(linkLabel_);
  root->addLayout(header);

  auto* body = new QHBoxLayout();

  auto* tmBox = new QGroupBox(QStringLiteral("Telemetry (live)"), this);
  auto* tmLayout = new QVBoxLayout(tmBox);
  tmTable_ = new QTableWidget(0, 4, this);
  tmTable_->setHorizontalHeaderLabels(
      {QStringLiteral("Parameter"), QStringLiteral("Value"), QStringLiteral("Unit"),
       QStringLiteral("Updated (UTC)")});
  tmTable_->horizontalHeader()->setStretchLastSection(true);
  tmTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tmTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tmLayout->addWidget(tmTable_);
  body->addWidget(tmBox, 2);

  auto* right = new QVBoxLayout();
  auto* tcBox = new QGroupBox(QStringLiteral("Telecommand"), this);
  auto* tcForm = new QFormLayout(tcBox);
  tcNameEdit_ = new QLineEdit(QStringLiteral("NOOP"), this);
  tcArgsEdit_ = new QLineEdit(QStringLiteral("{}"), this);
  sendButton_ = new QPushButton(QStringLiteral("Send"), this);
  connect(sendButton_, &QPushButton::clicked, this, &MainWindow::sendCommand);
  tcForm->addRow(QStringLiteral("Mnemonic"), tcNameEdit_);
  tcForm->addRow(QStringLiteral("Args (JSON)"), tcArgsEdit_);
  tcForm->addRow(QString(), sendButton_);
  right->addWidget(tcBox);

  auto* histBox = new QGroupBox(QStringLiteral("Command history / status"), this);
  auto* histLayout = new QVBoxLayout(histBox);
  tcTable_ = new QTableWidget(0, 4, this);
  tcTable_->setHorizontalHeaderLabels(
      {QStringLiteral("ID"), QStringLiteral("Status"), QStringLiteral("Detail"),
       QStringLiteral("Updated (UTC)")});
  tcTable_->horizontalHeader()->setStretchLastSection(true);
  tcTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  histLayout->addWidget(tcTable_);
  right->addWidget(histBox, 1);

  body->addLayout(right, 1);
  root->addLayout(body, 1);

  auto* logBox = new QGroupBox(QStringLiteral("Ops log"), this);
  auto* logLayout = new QVBoxLayout(logBox);
  logView_ = new QPlainTextEdit(this);
  logView_->setReadOnly(true);
  logView_->setMaximumBlockCount(500);
  logLayout->addWidget(logView_);
  root->addWidget(logBox);

  setCentralWidget(central);
  statusBar()->showMessage(QStringLiteral("MOS Phase 2 Slice A"));
}

void MainWindow::onParametersUpdated(const QString& /*assetId*/,
                                     const QVector<Parameter>& params)
{
  for (const auto& p : params) {
    latest_.insert(p.name, p);
  }
  refreshTmTable();
}

void MainWindow::refreshTmTable()
{
  const QStringList order = {QStringLiteral("SEQ"), QStringLiteral("MODE"),
                             QStringLiteral("BUS_V"), QStringLiteral("BATT_SOC"),
                             QStringLiteral("TEMP_BRD")};
  QStringList names = order;
  for (auto it = latest_.cbegin(); it != latest_.cend(); ++it) {
    if (!names.contains(it.key())) {
      names.push_back(it.key());
    }
  }

  tmTable_->setRowCount(names.size());
  for (int row = 0; row < names.size(); ++row) {
    const auto& p = latest_.value(names[row]);
    tmTable_->setItem(row, 0, new QTableWidgetItem(p.name));
    tmTable_->setItem(row, 1, new QTableWidgetItem(p.value.toString()));
    tmTable_->setItem(row, 2, new QTableWidgetItem(p.unit));
    tmTable_->setItem(
        row, 3,
        new QTableWidgetItem(p.timestampUtc.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))));
  }
}

void MainWindow::onCommandUpdated(const CommandResult& result)
{
  appendTcRow(result, QString());
  onOpsLog(QStringLiteral("TC %1 → %2 (%3)")
               .arg(result.commandId.left(8), commandStatusToString(result.status),
                    result.detail));
}

void MainWindow::appendTcRow(const CommandResult& result, const QString& /*nameHint*/)
{
  // Update existing row if present.
  for (int row = 0; row < tcTable_->rowCount(); ++row) {
    if (tcTable_->item(row, 0) && tcTable_->item(row, 0)->data(Qt::UserRole).toString() ==
                                      result.commandId) {
      tcTable_->item(row, 1)->setText(commandStatusToString(result.status));
      tcTable_->item(row, 2)->setText(result.detail);
      tcTable_->item(row, 3)->setText(
          result.updatedUtc.toUTC().toString(QStringLiteral("HH:mm:ss.zzz")));
      return;
    }
  }

  const int row = tcTable_->rowCount();
  tcTable_->insertRow(row);
  auto* idItem = new QTableWidgetItem(result.commandId.left(8));
  idItem->setData(Qt::UserRole, result.commandId);
  tcTable_->setItem(row, 0, idItem);
  tcTable_->setItem(row, 1, new QTableWidgetItem(commandStatusToString(result.status)));
  tcTable_->setItem(row, 2, new QTableWidgetItem(result.detail));
  tcTable_->setItem(
      row, 3,
      new QTableWidgetItem(result.updatedUtc.toUTC().toString(QStringLiteral("HH:mm:ss.zzz"))));
}

void MainWindow::onLinkStateChanged(LinkState state)
{
  linkLabel_->setText(QStringLiteral("Link: %1").arg(linkStateToString(state)));
  statusBar()->showMessage(QStringLiteral("Link %1").arg(linkStateToString(state)));
}

void MainWindow::onOpsLog(const QString& line)
{
  const QString stamp =
      QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss"));
  logView_->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, line));
}

void MainWindow::sendCommand()
{
  QString error;
  if (!session_->tc()->send(session_->primaryAssetId(), tcNameEdit_->text(),
                            tcArgsEdit_->text(), &error)) {
    QMessageBox::warning(this, QStringLiteral("Send failed"), error);
  }
}

}  // namespace mos
