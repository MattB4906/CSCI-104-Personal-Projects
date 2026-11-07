// Qt Test suite for the engine: probes against local sockets, the waker over
// loopback, and the Session state machine driven by fakes. Nothing leaves the
// machine: every server lives on 127.0.0.1 and no name lookup goes out.
#include <QtTest>

#include <QCoreApplication>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>

#include "engine/Explain.h"
#include "engine/QtWaker.h"
#include "engine/ServiceProbe.h"
#include "engine/Session.h"

using namespace wakedeck;

namespace {

const char* const kServerInfo =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<root status_code=\"200\"><hostname>LIVING-ROOM-PC</hostname><appversion>7.1.431.-1</appversion>"
    "<PairStatus>0</PairStatus><currentgame>0</currentgame><state>SUNSHINE_SERVER_FREE</state></root>";

// Answers every HTTP request on 127.0.0.1 with one fixed response, then closes.
class FakeHttpServer {
public:
    FakeHttpServer(int status, const QByteArray& body) {
        server_.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server_, &QTcpServer::newConnection, [this, status, body] {
            while (QTcpSocket* socket = server_.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, status, body] {
                    if (!socket->peek(64 * 1024).contains("\r\n\r\n")) return;  // wait for the whole request head
                    socket->readAll();
                    ++requests;
                    const QByteArray reason = status == 200 ? "OK" : "Not Found";
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " " + reason +
                                  "\r\nContent-Type: text/xml\r\nContent-Length: " + QByteArray::number(body.size()) +
                                  "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    quint16 port() const { return server_.serverPort(); }
    int requests = 0;

private:
    QTcpServer server_;
};

class FakeProbe final : public Probe {
public:
    FakeProbe(ProbeResult result, QObject* parent) : Probe(parent), result_(std::move(result)) {}
    void start() override {
        QTimer::singleShot(0, this, [this] { emit finished(result_); });
    }

private:
    ProbeResult result_;
};

class FakeWaker final : public Waker {
public:
    FakeWaker(bool ok, QObject* parent) : Waker(parent), ok_(ok) {}
    void start() override {
        QTimer::singleShot(0, this, [this] {
            emit finished(ok_, QStringLiteral("10 packets to 192.0.2.255 (ports 9 x5, 7 x5)"));
        });
    }

private:
    bool ok_;
};

struct Script {
    std::vector<ProbeStatus> statuses;  // one per check; the last one repeats
    int probesCreated = 0;
    int wakersCreated = 0;
    bool wakeOk = true;
};

SessionFactories fakeFactories(const std::shared_ptr<Script>& script) {
    SessionFactories factories;
    factories.probe = [script](const ProbeTarget&, QObject* parent) -> Probe* {
        const std::size_t index = std::min(static_cast<std::size_t>(script->probesCreated), script->statuses.size() - 1);
        ++script->probesCreated;
        ProbeResult result;
        result.status = script->statuses[index];
        result.detail = statusName(result.status);
        result.elapsedMs = 1;
        return new FakeProbe(result, parent);
    };
    factories.waker = [script](const WakeConfig&, QObject* parent) -> Waker* {
        ++script->wakersCreated;
        return new FakeWaker(script->wakeOk, parent);
    };
    return factories;
}

WakeConfig exampleWake() {
    WakeTarget target;
    target.destination = "192.0.2.255";
    return WakeConfig{*MacAddress::parse("00:00:5E:00:53:2A"), target};
}

SessionConfig quickConfig(bool withWake) {
    SessionConfig config;
    config.check.host = QStringLiteral("192.0.2.10");
    config.check.port = 47989;
    config.pollIntervalMs = 5;
    config.overallTimeoutMs = 2000;
    if (withWake) config.wake = exampleWake();
    return config;
}

struct Run {
    std::optional<Outcome> outcome;
    QString summary;
    QStringList events;
    std::vector<EventKind> kinds;

    bool hasEvent(const QString& needle) const {
        return std::any_of(events.begin(), events.end(),
                           [&](const QString& e) { return e.contains(needle, Qt::CaseInsensitive); });
    }
    bool hasKind(EventKind kind) const { return std::find(kinds.begin(), kinds.end(), kind) != kinds.end(); }
};

Run runSession(Session& session, int timeoutMs = 5000) {
    Run run;
    QObject::connect(&session, &Session::timelineEvent, [&run](const TimelineEvent& e) {
        run.events << e.text;
        run.kinds.push_back(e.kind);
    });
    QObject::connect(&session, &Session::finished, [&run](Outcome outcome, const QString& summary) {
        run.outcome = outcome;
        run.summary = summary;
    });
    session.start();
    [[maybe_unused]] const bool finished = QTest::qWaitFor([&run] { return run.outcome.has_value(); }, timeoutMs);
    return run;  // callers check run.outcome, which stays empty on a timeout
}

ProbeResult runProbe(Probe* probe, int timeoutMs = 5000) {
    std::optional<ProbeResult> out;
    QObject::connect(probe, &Probe::finished, [&out](const ProbeResult& result) { out = result; });
    probe->start();
    [[maybe_unused]] const bool finished = QTest::qWaitFor([&out] { return out.has_value(); }, timeoutMs);
    ProbeResult fallback;
    fallback.status = ProbeStatus::Error;
    fallback.detail = QStringLiteral("probe never finished");
    return out.value_or(fallback);
}

quint16 closedLocalPort() {
    QTcpServer server;
    server.listen(QHostAddress::LocalHost, 0);
    return server.serverPort();  // the server closes when it goes out of scope
}

}  // namespace

class EngineTest : public QObject {
    Q_OBJECT

private slots:
    // ---- Sunshine reply parsing (pure) ----

    void serverInfoReady() {
        const ProbeResult result = SunshineProbe::interpretServerInfo(kServerInfo);
        QVERIFY(result.ready());
        QCOMPARE(result.detail, QStringLiteral("hostname LIVING-ROOM-PC, state free"));
    }

    void serverInfoBusyWithoutHostname() {
        const ProbeResult result = SunshineProbe::interpretServerInfo(
            "<root status_code=\"200\"><state>SUNSHINE_SERVER_BUSY</state></root>");
        QVERIFY(result.ready());
        QCOMPARE(result.detail, QStringLiteral("state busy"));
    }

    void serverInfoRejectsOtherReplies() {
        QCOMPARE(SunshineProbe::interpretServerInfo("<html><body>router login</body></html>").status, ProbeStatus::NotReady);
        QCOMPARE(SunshineProbe::interpretServerInfo("not xml at all").status, ProbeStatus::NotReady);
        QCOMPARE(SunshineProbe::interpretServerInfo("").status, ProbeStatus::NotReady);
        QCOMPARE(SunshineProbe::interpretServerInfo("<root status_code=\"200\"><hostname>PC</hostname></root>").status,
                 ProbeStatus::NotReady);
        const ProbeResult bad = SunshineProbe::interpretServerInfo(
            "<root status_code=\"503\"><state>SUNSHINE_SERVER_FREE</state></root>");
        QCOMPARE(bad.status, ProbeStatus::NotReady);
        QVERIFY(bad.detail.contains(QStringLiteral("503")));
    }

    // ---- Real probes against local sockets ----

    void tcpProbeReady() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        ProbeTarget target;
        target.host = QStringLiteral("127.0.0.1");
        target.port = server.serverPort();
        target.kind = ProbeKind::Tcp;
        QObject parent;
        const ProbeResult result = runProbe(makeProbe(target, &parent));
        QCOMPARE(result.status, ProbeStatus::Ready);
        QVERIFY(result.detail.contains(QStringLiteral("accepted")));
    }

    void tcpProbeRefused() {
        ProbeTarget target;
        target.host = QStringLiteral("127.0.0.1");
        target.port = closedLocalPort();
        target.kind = ProbeKind::Tcp;
        QObject parent;
        QCOMPARE(runProbe(makeProbe(target, &parent)).status, ProbeStatus::Refused);
    }

    void sunshineProbeReady() {
        FakeHttpServer server(200, kServerInfo);
        ProbeTarget target;
        target.host = QStringLiteral("127.0.0.1");
        target.port = server.port();
        QObject parent;
        const ProbeResult result = runProbe(makeProbe(target, &parent));
        QCOMPARE(result.status, ProbeStatus::Ready);
        QVERIFY(result.detail.contains(QStringLiteral("LIVING-ROOM-PC")));
        QCOMPARE(server.requests, 1);
    }

    void sunshineProbeWrongService() {
        FakeHttpServer server(200, "<html><body>not sunshine</body></html>");
        ProbeTarget target;
        target.host = QStringLiteral("127.0.0.1");
        target.port = server.port();
        QObject parent;
        QCOMPARE(runProbe(makeProbe(target, &parent)).status, ProbeStatus::NotReady);
    }

    void sunshineProbeHttpError() {
        FakeHttpServer server(404, "nothing here");
        ProbeTarget target;
        target.host = QStringLiteral("127.0.0.1");
        target.port = server.port();
        QObject parent;
        const ProbeResult result = runProbe(makeProbe(target, &parent));
        QCOMPARE(result.status, ProbeStatus::NotReady);
        QVERIFY(result.detail.contains(QStringLiteral("404")));
    }

    void sunshineProbeRefused() {
        ProbeTarget target;
        target.host = QStringLiteral("127.0.0.1");
        target.port = closedLocalPort();
        QObject parent;
        QCOMPARE(runProbe(makeProbe(target, &parent)).status, ProbeStatus::Refused);
    }

    void probesDescribeThemselves() {
        ProbeTarget target;
        target.host = QStringLiteral("pc.example.net");
        QCOMPARE(describe(target), QStringLiteral("Sunshine at pc.example.net:47989"));
        target.kind = ProbeKind::Tcp;
        target.port = 3389;
        QCOMPARE(describe(target), QStringLiteral("TCP port 3389 on pc.example.net"));
        QCOMPARE(describe(exampleWake()), QStringLiteral("10 packets to 192.0.2.255 (ports 9 x5, 7 x5)"));
    }

    // ---- Waker over loopback ----

    void wakerLoopback() {
        QUdpSocket listener;
        QVERIFY(listener.bind(QHostAddress::LocalHost, 0));

        WakeTarget target;
        target.destination = "127.0.0.1";
        target.ports = {listener.localPort()};
        target.repeat = 3;
        target.interval = std::chrono::milliseconds(0);
        const WakeConfig config{*MacAddress::parse("00:00:5E:00:53:2A"), target};

        std::optional<bool> ok;
        QString summary;
        QObject parent;
        Waker* waker = makeWaker(config, &parent);
        connect(waker, &Waker::finished, [&](bool allSent, const QString& text) {
            ok = allSent;
            summary = text;
        });
        waker->start();
        QVERIFY(QTest::qWaitFor([&ok] { return ok.has_value(); }, 5000));
        QVERIFY(*ok);
        QVERIFY2(summary.contains(QStringLiteral("3 packets to 127.0.0.1")), qPrintable(summary));

        const MagicPacket packet(config.mac);
        int received = 0;
        while (received < 3 && QTest::qWaitFor([&listener] { return listener.hasPendingDatagrams(); }, 2000)) {
            const QNetworkDatagram datagram = listener.receiveDatagram();
            QCOMPARE(datagram.data().size(), 102);
            QVERIFY(std::equal(packet.bytes().begin(), packet.bytes().end(),
                               reinterpret_cast<const std::uint8_t*>(datagram.data().constData())));
            ++received;
        }
        QCOMPARE(received, 3);
    }

    void wakerReportsResolutionFailure() {
        WakeTarget target;
        target.destination = "";  // nothing to resolve, so no lookup leaves the machine
        const WakeConfig config{*MacAddress::parse("00:00:5E:00:53:2A"), target};
        std::optional<bool> ok;
        QString summary;
        QObject parent;
        Waker* waker = makeWaker(config, &parent);
        connect(waker, &Waker::finished, [&](bool allSent, const QString& text) {
            ok = allSent;
            summary = text;
        });
        waker->start();
        QVERIFY(QTest::qWaitFor([&ok] { return ok.has_value(); }, 5000));
        QVERIFY(!*ok);
        QVERIFY2(summary.contains(QStringLiteral("cannot resolve")), qPrintable(summary));
    }

    // ---- Session state machine with fakes ----

    void sessionReadyImmediately() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::Ready};
        Session session(quickConfig(true), fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyWithoutWake);
        QCOMPARE(script->probesCreated, 1);
        QCOMPARE(script->wakersCreated, 0);
        QVERIFY(run.hasKind(EventKind::Ready));
        QVERIFY(!run.hasEvent(QStringLiteral("Wake request sent")));
        QCOMPARE(session.state(), SessionState::Done);
    }

    void sessionWakeThenReady() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut, ProbeStatus::TimedOut, ProbeStatus::Ready};
        Session session(quickConfig(true), fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyAfterWake);
        QCOMPARE(script->wakersCreated, 1);
        QCOMPARE(script->probesCreated, 3);
        QVERIFY(run.hasKind(EventKind::WakeSent));
        QVERIFY(run.hasEvent(QStringLiteral("Wake request sent")));
        QVERIFY(run.hasEvent(QStringLiteral("Check 2")));
        QVERIFY(run.hasKind(EventKind::Ready));
        QVERIFY(session.observations().wakeSent);
        QCOMPARE(session.state(), SessionState::Done);
    }

    void sessionTimesOutWithExplanation() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        SessionConfig config = quickConfig(true);
        config.overallTimeoutMs = 60;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::TimedOut);
        QVERIFY(script->probesCreated >= 2);
        QVERIFY(run.hasKind(EventKind::Failure));
        QVERIFY2(run.summary.contains(QStringLiteral("What happened")), qPrintable(run.summary));
        QVERIFY2(run.summary.contains(QStringLiteral("timed out")), qPrintable(run.summary));
        QVERIFY2(run.summary.contains(QStringLiteral("Wake request sent at")), qPrintable(run.summary));
        QCOMPARE(session.state(), SessionState::Failed);
    }

    void sessionWakeFailure() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->wakeOk = false;
        Session session(quickConfig(true), fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::WakeFailed);
        QCOMPARE(script->probesCreated, 1);
        QVERIFY(run.summary.contains(QStringLiteral("could not be sent")));
        QCOMPARE(session.state(), SessionState::Failed);
    }

    void sessionWithoutWakeOnlyWaits() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::Refused, ProbeStatus::Ready};
        Session session(quickConfig(false), fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyWithoutWake);
        QCOMPARE(script->wakersCreated, 0);
        QCOMPARE(script->probesCreated, 2);
        QVERIFY(run.hasEvent(QStringLiteral("No wake request configured")));
        QVERIFY(!run.hasKind(EventKind::WakeSent));
    }

    void sessionCancel() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        SessionConfig config = quickConfig(true);
        config.overallTimeoutMs = 10000;
        Session session(config, fakeFactories(script));
        QTimer::singleShot(20, &session, [&session] { session.cancel(); });
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::Cancelled);
        QCOMPARE(session.state(), SessionState::Failed);
    }

    // ---- Explanation text and helpers (pure) ----

    void explainSilentChecksMentionTailscale() {
        Observations o;
        o.check.host = QStringLiteral("gaming-pc.tail1234.ts.net");
        o.wakeConfigured = true;
        o.wakeSent = true;
        o.wakeAt = QDateTime::currentDateTime();
        o.wakeSummary = QStringLiteral("10 packets to 192.168.1.255 (ports 9 x5, 7 x5)");
        o.elapsedMs = 120000;
        for (int i = 0; i < 40; ++i) {
            ProbeResult r;
            r.status = ProbeStatus::TimedOut;
            r.detail = QStringLiteral("no answer within 2000 ms");
            o.checks.push_back(Observation{QDateTime::currentDateTime(), r});
        }
        const QString text = explainTimeout(o);
        QVERIFY2(text.contains(QStringLiteral("40 checks, 40 timed out")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("2:00")));
        QVERIFY(text.contains(QStringLiteral("Tailscale")));
        QVERIFY(text.contains(QStringLiteral("Wake request sent at")));
        QVERIFY(text.contains(QStringLiteral("never answered")));
        QVERIFY(!text.contains(QStringLiteral("refused")));
    }

    void explainRefusedMeansTheComputerIsUp() {
        Observations o;
        o.check.host = QStringLiteral("192.168.1.20");
        o.check.port = 47989;
        o.wakeConfigured = false;
        o.elapsedMs = 30000;
        for (int i = 0; i < 5; ++i) {
            ProbeResult r;
            r.status = i < 3 ? ProbeStatus::TimedOut : ProbeStatus::Refused;
            r.detail = statusName(r.status);
            o.checks.push_back(Observation{QDateTime::currentDateTime(), r});
        }
        const QString text = explainTimeout(o);
        QVERIFY2(text.contains(QStringLiteral("3 timed out, 2 refused")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("not listening")));
        QVERIFY(text.contains(QStringLiteral("No wake request is configured")));
        QVERIFY(!text.contains(QStringLiteral("Tailscale")));
        QVERIFY(!text.contains(QStringLiteral("never answered")));
    }

    void durationsFormat() {
        QCOMPARE(formatDuration(0), QStringLiteral("0:00"));
        QCOMPARE(formatDuration(999), QStringLiteral("0:00"));
        QCOMPARE(formatDuration(5000), QStringLiteral("0:05"));
        QCOMPARE(formatDuration(120000), QStringLiteral("2:00"));
        QCOMPARE(formatDuration(754000), QStringLiteral("12:34"));
        QCOMPARE(formatDuration(-5), QStringLiteral("0:00"));
    }

    void tailscaleDetection() {
        QVERIFY(looksLikeTailscale(QStringLiteral("pc.tail1234.ts.net")));
        QVERIFY(looksLikeTailscale(QStringLiteral("PC.TAIL1234.TS.NET")));
        QVERIFY(looksLikeTailscale(QStringLiteral("100.101.102.103")));
        QVERIFY(looksLikeTailscale(QStringLiteral("100.64.0.1")));
        QVERIFY(looksLikeTailscale(QStringLiteral("100.127.255.254")));
        QVERIFY(!looksLikeTailscale(QStringLiteral("100.63.255.255")));
        QVERIFY(!looksLikeTailscale(QStringLiteral("100.128.0.1")));
        QVERIFY(!looksLikeTailscale(QStringLiteral("192.168.1.5")));
        QVERIFY(!looksLikeTailscale(QStringLiteral("pc.example.net")));
    }
};

QTEST_GUILESS_MAIN(EngineTest)
#include "test_engine.moc"
