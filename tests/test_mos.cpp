#include "CfsAdapter.h"
#include "Database.h"
#include "Models.h"
#include "Schema.h"
#include "SimTmGenerator.h"
#include "StubCfsBridge.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestMos : public QObject {
  Q_OBJECT
 private slots:
  void schemaAndDefaultAsset();
  void tmAndTcHistory();
  void insertUnitlessTmSample();
  void adapterDecodesSimFrame();
  void stubBridgeEmitsTmAndAck();
};

void TestMos::schemaAndDefaultAsset()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + QStringLiteral("/t.sqlite");

  mos::persist::Database db(QStringLiteral("test_schema"));
  QString error;
  QVERIFY2(db.open(path, &error), qPrintable(error));
  QCOMPARE(mos::persist::schemaVersion(), QStringLiteral("1"));

  mos::Asset sc1{QStringLiteral("SC-1"), QStringLiteral("Spacecraft 1"),
                 QStringLiteral("generic-cubesat"), true};
  QVERIFY2(db.ensureDefaultAsset(sc1, &error), qPrintable(error));
  auto loaded = db.loadAsset(QStringLiteral("SC-1"), &error);
  QVERIFY(loaded.has_value());
  QCOMPARE(loaded->name, QStringLiteral("Spacecraft 1"));
}

void TestMos::tmAndTcHistory()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  mos::persist::Database db(QStringLiteral("test_hist"));
  QString error;
  QVERIFY(db.open(dir.path() + QStringLiteral("/h.sqlite"), &error));

  mos::Asset sc1{QStringLiteral("SC-1"), QStringLiteral("Spacecraft 1"),
                 QStringLiteral("x"), true};
  QVERIFY(db.ensureDefaultAsset(sc1, &error));

  mos::Parameter p;
  p.assetId = QStringLiteral("SC-1");
  p.name = QStringLiteral("BUS_V");
  p.value = 28.1;
  p.unit = QStringLiteral("V");
  p.sequence = 1;
  p.timestampUtc = QDateTime::currentDateTimeUtc();
  QVERIFY(db.insertTmSample(p, &error));
  QCOMPARE(db.recentTm(QStringLiteral("SC-1"), 10, &error).size(), 1);

  mos::Telecommand tc;
  tc.id = QStringLiteral("cmd-1");
  tc.assetId = QStringLiteral("SC-1");
  tc.name = QStringLiteral("NOOP");
  tc.argsJson = QStringLiteral("{}");
  tc.createdUtc = QDateTime::currentDateTimeUtc();
  mos::CommandResult r;
  r.commandId = tc.id;
  r.assetId = tc.assetId;
  r.status = mos::CommandStatus::Acked;
  r.detail = QStringLiteral("ok");
  r.updatedUtc = tc.createdUtc;
  QVERIFY(db.upsertCommand(tc, r, &error));
  QCOMPARE(db.recentCommands(QStringLiteral("SC-1"), 10, &error).size(), 1);
}

void TestMos::insertUnitlessTmSample()
{
  // Default-constructed QString is null; Qt SQL would bind SQL NULL and trip
  // NOT NULL on tm_samples.unit unless insertTmSample coalesces to "".
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  mos::persist::Database db(QStringLiteral("test_unitless"));
  QString error;
  QVERIFY2(db.open(dir.path() + QStringLiteral("/u.sqlite"), &error), qPrintable(error));

  mos::Asset sc1{QStringLiteral("SC-1"), QStringLiteral("Spacecraft 1"),
                 QStringLiteral("x"), true};
  QVERIFY(db.ensureDefaultAsset(sc1, &error));

  mos::Parameter seq;
  seq.assetId = QStringLiteral("SC-1");
  seq.name = QStringLiteral("SEQ");
  seq.value = 42;
  // deliberately leave unit default-null
  seq.sequence = 42;
  seq.timestampUtc = QDateTime::currentDateTimeUtc();
  QVERIFY2(db.insertTmSample(seq, &error), qPrintable(error));

  mos::Parameter mode;
  mode.assetId = QStringLiteral("SC-1");
  mode.name = QStringLiteral("MODE");
  mode.value = QStringLiteral("IDLE");
  mode.unit = QString();  // null
  mode.sequence = 42;
  mode.timestampUtc = QDateTime::currentDateTimeUtc();
  QVERIFY2(db.insertTmSample(mode, &error), qPrintable(error));

  mos::CfsAdapter adapter;
  mos::SimTmGenerator gen(QStringLiteral("SC-1"));
  QSignalSpy spy(&gen, &mos::SimTmGenerator::frameReady);
  gen.start(100000);
  QVERIFY(spy.count() >= 1);
  mos::RawFrame frame;
  frame.assetId = spy.first().at(0).toString();
  frame.payload = spy.first().at(1).toByteArray();
  frame.receivedUtc = QDateTime::currentDateTimeUtc();
  QVector<mos::Parameter> params;
  QVERIFY2(adapter.decodeTelemetry(frame, &params, &error), qPrintable(error));
  for (const auto& p : params) {
    QVERIFY2(db.insertTmSample(p, &error),
             qPrintable(QStringLiteral("%1: %2").arg(p.name, error)));
  }
}

void TestMos::adapterDecodesSimFrame()
{
  mos::SimTmGenerator gen(QStringLiteral("SC-1"));
  QSignalSpy spy(&gen, &mos::SimTmGenerator::frameReady);
  gen.start(100000);
  QVERIFY(spy.count() >= 1);

  const auto args = spy.takeFirst();
  mos::RawFrame frame;
  frame.assetId = args.at(0).toString();
  frame.payload = args.at(1).toByteArray();
  frame.receivedUtc = QDateTime::currentDateTimeUtc();
  frame.source = QStringLiteral("sim");

  mos::CfsAdapter adapter;
  QVector<mos::Parameter> params;
  QString error;
  QVERIFY2(adapter.decodeTelemetry(frame, &params, &error), qPrintable(error));
  QVERIFY(params.size() >= 4);
  QCOMPARE(params.first().assetId, QStringLiteral("SC-1"));
}

void TestMos::stubBridgeEmitsTmAndAck()
{
  mos::StubCfsBridge bridge(QStringLiteral("SC-1"));
  QSignalSpy tmSpy(&bridge, &mos::ICfsBridge::telemetryReceived);
  QSignalSpy ackSpy(&bridge, &mos::ICfsBridge::commandAck);
  QString error;
  QVERIFY(bridge.start(&error));
  QTRY_VERIFY_WITH_TIMEOUT(tmSpy.count() >= 1, 2000);

  const QByteArray enc = mos::StubCfsBridge::encodeStubCommand(
      QStringLiteral("abc"), QStringLiteral("NOOP"), QStringLiteral("{}"));
  QVERIFY(bridge.sendTelecommand(QStringLiteral("SC-1"), enc, &error));
  QTRY_COMPARE_WITH_TIMEOUT(ackSpy.count(), 1, 2000);
  QCOMPARE(ackSpy.first().at(2).toBool(), true);
  bridge.stop();
}

QTEST_MAIN(TestMos)
#include "test_mos.moc"
