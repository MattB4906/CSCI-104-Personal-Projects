// Qt Test suite for the engine: probes against local sockets, the waker over
// loopback, and the Session state machine driven by fakes. Nothing leaves the
// machine: every server lives on 127.0.0.1 and no name lookup goes out.
#include <QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>
#include <QScopeGuard>
#include <QTemporaryDir>
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
#include "engine/LocalNetworks.h"
#include "engine/QtWaker.h"
#include "engine/ServiceProbe.h"
#include "engine/Session.h"
#include "engine/Tailscale.h"

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

class FakeLauncher final : public Launcher {
public:
    FakeLauncher(bool ok, QObject* parent, int delayMs = 0) : Launcher(parent), ok_(ok), delayMs_(delayMs) {}
    void start() override {
        QTimer::singleShot(delayMs_, this, [this] {
            emit finished(ok_, ok_ ? QStringLiteral("fake client launched") : QStringLiteral("fake client not found"));
        });
    }

private:
    bool ok_;
    int delayMs_;
};

struct Script {
    std::vector<ProbeStatus> statuses;  // one per check; the last one repeats
    int probesCreated = 0;
    int wakersCreated = 0;
    bool wakeOk = true;
    std::optional<bool> launchOk;  // when set, a FakeLauncher with this result replaces NoLauncher
    // Resend tests: when either is set, a ScriptedWaker replaces FakeWaker.
    std::vector<bool> wakeResults;  // one per wake request; the last one repeats
    int wakeDelayMs = 0;
    int resendDelayMs = -1;         // >= 0: how long the second and later wake requests take (else wakeDelayMs)
    int sendsDone = 0;              // wake requests that finished
    int readyAfterSends = -1;       // >= 0: every check is ready once this many wake requests finished
    int launchDelayMs = 0;          // how long the FakeLauncher takes
    int readyAfterMs = -1;          // >= 0: every check is ready once this much time has passed
    QElapsedTimer clock;            // started by fakeFactories
    int wakersInFlight = 0;
    int maxWakersInFlight = 0;
    // Tailscale: one status per read (the last one repeats); empty: the factory returns no reader.
    std::vector<TailscaleStatus> tailscale;
    int readersCreated = 0;
    int lateReadMs = 0;  // how long the second and later reads take
};

class FakeTailscaleReader final : public TailscaleReader {
public:
    FakeTailscaleReader(TailscaleStatus status, QObject* parent, int delayMs = 0)
        : TailscaleReader(parent), status_(std::move(status)), delayMs_(delayMs) {}
    void start() override {
        QTimer::singleShot(delayMs_, this, [this] { emit finished(status_); });
    }

private:
    TailscaleStatus status_;
    int delayMs_;
};

// Counts how many wake requests are in flight at once and answers after a delay.
class ScriptedWaker final : public Waker {
public:
    ScriptedWaker(std::shared_ptr<Script> script, bool ok, int delayMs, QObject* parent)
        : Waker(parent), script_(std::move(script)), ok_(ok), delayMs_(delayMs) {}
    ~ScriptedWaker() override {
        if (running_) --script_->wakersInFlight;
    }
    void start() override {
        running_ = true;
        ++script_->wakersInFlight;
        script_->maxWakersInFlight = std::max(script_->maxWakersInFlight, script_->wakersInFlight);
        QTimer::singleShot(delayMs_, this, [this] {
            running_ = false;
            --script_->wakersInFlight;
            ++script_->sendsDone;
            emit finished(ok_, ok_ ? QStringLiteral("10 packets to 192.0.2.255 (ports 9 x5, 7 x5)")
                                   : QStringLiteral("0 of 10 packets to 192.0.2.255; cannot open a UDP socket"));
        });
    }

private:
    std::shared_ptr<Script> script_;
    bool ok_;
    int delayMs_;
    bool running_ = false;
};

SessionFactories fakeFactories(const std::shared_ptr<Script>& script) {
    SessionFactories factories;
    factories.probe = [script](const ProbeTarget&, QObject* parent) -> Probe* {
        const std::size_t index = std::min(static_cast<std::size_t>(script->probesCreated), script->statuses.size() - 1);
        ++script->probesCreated;
        ProbeResult result;
        result.status = script->statuses[index];
        if (script->readyAfterMs >= 0 && script->clock.elapsed() >= script->readyAfterMs) result.status = ProbeStatus::Ready;
        if (script->readyAfterSends >= 0 && script->sendsDone >= script->readyAfterSends) result.status = ProbeStatus::Ready;
        result.detail = statusName(result.status);
        result.elapsedMs = 1;
        return new FakeProbe(result, parent);
    };
    script->clock.start();
    factories.waker = [script](const WakeConfig&, QObject* parent) -> Waker* {
        ++script->wakersCreated;
        if (!script->wakeResults.empty() || script->wakeDelayMs > 0 || script->resendDelayMs >= 0) {
            const std::size_t index = std::min(static_cast<std::size_t>(script->wakersCreated - 1),
                                               script->wakeResults.empty() ? 0 : script->wakeResults.size() - 1);
            const bool ok = script->wakeResults.empty() ? script->wakeOk : script->wakeResults[index];
            const int delay = script->wakersCreated > 1 && script->resendDelayMs >= 0 ? script->resendDelayMs : script->wakeDelayMs;
            return new ScriptedWaker(script, ok, delay, parent);
        }
        return new FakeWaker(script->wakeOk, parent);
    };
    if (script->launchOk.has_value()) {
        factories.launcher = [script](QObject* parent) -> Launcher* {
            return new FakeLauncher(*script->launchOk, parent, script->launchDelayMs);
        };
    }
    factories.tailscale = [script](QObject* parent) -> TailscaleReader* {
        ++script->readersCreated;
        if (script->tailscale.empty()) return nullptr;
        const std::size_t index = std::min(static_cast<std::size_t>(script->readersCreated - 1), script->tailscale.size() - 1);
        return new FakeTailscaleReader(script->tailscale[index], parent, script->readersCreated > 1 ? script->lateReadMs : 0);
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

// Sample answers of "tailscale status --json", with invented names and
// documentation addresses only.
const char* const kTailscaleRunning = R"({
  "Version": "1.90.0",
  "BackendState": "Running",
  "Self": {"HostName": "this-mac", "DNSName": "this-mac.example-tailnet.ts.net.", "TailscaleIPs": ["100.64.0.1"],
           "Online": true},
  "Peer": {
    "nodekey:aaaa": {"HostName": "LIVING-ROOM-PC", "DNSName": "living-room-pc.example-tailnet.ts.net.",
                     "TailscaleIPs": ["100.64.0.10", "fd7a:115c:a1e0::a"], "Online": true,
                     "LastSeen": "0001-01-01T00:00:00Z", "OS": "windows"},
    "nodekey:bbbb": {"HostName": "office-nas", "DNSName": "office-nas.example-tailnet.ts.net.",
                     "TailscaleIPs": ["100.64.0.11"], "Online": false,
                     "LastSeen": "2026-11-12T10:00:00.123456789Z", "OS": "linux"}
  }
})";

TailscaleStatus tailscaleState(const QString& state) {
    TailscaleStatus status;
    status.available = true;
    status.backendState = state;
    return status;
}

TailscaleStatus tailscaleWithPc(bool online) {
    TailscaleStatus status = tailscaleState(QStringLiteral("Running"));
    TailscalePeer peer;
    peer.hostName = QStringLiteral("LIVING-ROOM-PC");
    peer.dnsName = QStringLiteral("living-room-pc.example-tailnet.ts.net.");
    peer.addresses = {QStringLiteral("100.64.0.10")};
    peer.online = online;
    status.peers.push_back(peer);
    return status;
}

TailscaleStatus tailscaleUnavailable() {
    TailscaleStatus status;
    status.error = QStringLiteral("not found");
    return status;
}

SessionConfig tailscaleConfig() {
    SessionConfig config = quickConfig(true);
    config.check.host = QStringLiteral("100.64.0.10");
    return config;
}

// Runs a ProcessTailscaleReader against a fake tool given by the environment
// variable, and puts the variable back to a missing path afterwards.
std::optional<TailscaleStatus> readWithTool(const QString& tool, int limitMs = ProcessTailscaleReader::kDefaultLimitMs,
                                            int waitMs = 5000) {
    qputenv("WAKEDECK_TAILSCALE_CLI", tool.toLocal8Bit());
    const auto restore = qScopeGuard([] { qputenv("WAKEDECK_TAILSCALE_CLI", QByteArrayLiteral("/nonexistent/wakedeck-test-tailscale")); });
    std::optional<TailscaleStatus> out;
    ProcessTailscaleReader reader(nullptr, limitMs);
    QObject::connect(&reader, &TailscaleReader::finished, [&out](const TailscaleStatus& status) { out = status; });
    reader.start();
    if (out) return std::nullopt;  // must never finish inside start()
    [[maybe_unused]] const bool finished = QTest::qWaitFor([&out] { return out.has_value(); }, waitMs);
    return out;
}

QString writeScript(const QTemporaryDir& dir, const QString& name, const QByteArray& body) {
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write("#!/bin/sh\n" + body);
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
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
    void initTestCase() {
        // No test may run the real Tailscale tool: a missing path switches it off.
        qputenv("WAKEDECK_TAILSCALE_CLI", QByteArrayLiteral("/nonexistent/wakedeck-test-tailscale"));
    }

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
        QVERIFY2(result.detail.startsWith(QStringLiteral("state busy")), qPrintable(result.detail));
        QVERIFY(result.detail.contains(QStringLiteral("already active")));
    }

    void serverInfoWithoutStatusCodeIsAccepted() {
        // Sunshine always sends status_code; a reply without it is still read, on purpose.
        const ProbeResult result = SunshineProbe::interpretServerInfo(
            "<root><hostname>PC</hostname><state>SUNSHINE_SERVER_FREE</state></root>");
        QVERIFY(result.ready());
        QCOMPARE(result.detail, QStringLiteral("hostname PC, state free"));
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

    void sunshineProbeTimesOutOnSilentServer() {
        QTcpServer silent;  // accepts the connection and never answers
        QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
        ProbeTarget target;
        target.host = QStringLiteral("127.0.0.1");
        target.port = silent.serverPort();
        target.timeoutMs = 300;
        QObject parent;
        const ProbeResult result = runProbe(makeProbe(target, &parent));
        QCOMPARE(result.status, ProbeStatus::TimedOut);
        QVERIFY2(result.detail.contains(QStringLiteral("300 ms")), qPrintable(result.detail));
        QVERIFY2(result.elapsedMs >= 250, qPrintable(QString::number(result.elapsedMs)));
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

    void wakerRejectsEmptyPlan() {
        WakeTarget target;
        target.destination = "127.0.0.1";
        target.ports = {};
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
        QVERIFY2(summary.contains(QStringLiteral("nothing to send")), qPrintable(summary));
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

    // ---- Every local network (F2) ----

    void broadcastSelectionFollowsTheRules() {
        const auto entry = [](const char* ip, const char* broadcast) {
            return InterfaceDescription::Entry{QHostAddress(QString::fromLatin1(ip)),
                                               broadcast ? QHostAddress(QString::fromLatin1(broadcast)) : QHostAddress()};
        };
        const auto lan = [](const char* name) {
            InterfaceDescription d;
            d.name = QString::fromLatin1(name);
            d.isUp = d.isRunning = d.canBroadcast = true;
            return d;
        };
        std::vector<InterfaceDescription> interfaces;
        InterfaceDescription ethernet = lan("en0");
        ethernet.entries = {entry("192.168.1.20", "192.168.1.255"), entry("fe80::1", nullptr)};
        interfaces.push_back(ethernet);
        InterfaceDescription wifi = lan("en1");
        wifi.entries = {entry("10.0.1.5", "10.0.1.255"), entry("10.0.1.6", "10.0.1.255")};  // duplicate
        interfaces.push_back(wifi);
        InterfaceDescription bridge = lan("bridge0");
        bridge.entries = {entry("192.168.1.30", "192.168.1.255")};  // duplicate across interfaces
        interfaces.push_back(bridge);
        InterfaceDescription vpn = lan("utun3");  // a VPN tunnel: point-to-point
        vpn.isPointToPoint = true;
        vpn.entries = {entry("100.100.1.2", "100.100.1.255")};
        interfaces.push_back(vpn);
        InterfaceDescription loopback = lan("lo0");
        loopback.isLoopback = true;
        loopback.entries = {entry("127.0.0.1", "127.255.255.255")};
        interfaces.push_back(loopback);
        InterfaceDescription down = lan("en2");
        down.isUp = false;
        down.entries = {entry("172.16.0.2", "172.16.0.255")};
        interfaces.push_back(down);
        InterfaceDescription notRunning = lan("en3");
        notRunning.isRunning = false;
        notRunning.entries = {entry("172.16.1.2", "172.16.1.255")};
        interfaces.push_back(notRunning);
        InterfaceDescription noBroadcast = lan("en4");
        noBroadcast.canBroadcast = false;
        noBroadcast.entries = {entry("172.16.2.2", "172.16.2.255")};
        interfaces.push_back(noBroadcast);
        InterfaceDescription noBroadcastAddress = lan("en5");
        noBroadcastAddress.entries = {entry("172.16.3.2", nullptr)};
        interfaces.push_back(noBroadcastAddress);
        InterfaceDescription ipv6Only = lan("en6");
        ipv6Only.entries = {entry("2001:db8::5", "2001:db8::ff")};
        interfaces.push_back(ipv6Only);

        QCOMPARE(chooseBroadcastAddresses(interfaces), (QStringList{QStringLiteral("192.168.1.255"), QStringLiteral("10.0.1.255")}));
        QVERIFY(chooseBroadcastAddresses({}).isEmpty());
        QVERIFY(chooseBroadcastAddresses({vpn, loopback, down}).isEmpty());
    }

    void realInterfacesGiveOnlyIPv4() {
        // Reads this Mac's interface list (a local system call, no traffic) and
        // prints nothing about it.
        for (const InterfaceDescription& interface : readLocalInterfaces()) {
            for (const InterfaceDescription::Entry& entry : interface.entries) {
                QCOMPARE(entry.ip.protocol(), QAbstractSocket::IPv4Protocol);
            }
        }
        for (const QString& address : localBroadcastAddresses()) {
            QHostAddress parsed;
            QVERIFY(parsed.setAddress(address));
            QCOMPARE(parsed.protocol(), QAbstractSocket::IPv4Protocol);
            QVERIFY(!parsed.isLoopback());
        }
    }

    void wakerSendsThePlanToEveryNetwork() {
        QUdpSocket listener;
        QVERIFY(listener.bind(QHostAddress::LocalHost, 0));
        WakeTarget target;
        target.ports = {listener.localPort()};
        target.repeat = 3;
        target.interval = std::chrono::milliseconds(0);
        const WakeConfig config{*MacAddress::parse("00:00:5E:00:53:2A"), target, WakeDestination::LocalNetworks};
        int providerCalls = 0;
        const BroadcastAddressProvider provider = [&providerCalls] {
            ++providerCalls;
            return QStringList{QStringLiteral("127.0.0.1"), QStringLiteral("127.0.0.1")};
        };

        std::optional<bool> ok;
        QString summary;
        QObject parent;
        auto* waker = new QtWaker(config, provider, &parent);
        connect(waker, &Waker::finished, [&](bool allSent, const QString& text) {
            ok = allSent;
            summary = text;
        });
        waker->start();
        QCOMPARE(providerCalls, 0);  // read when the send begins, on the event loop
        QVERIFY(QTest::qWaitFor([&ok] { return ok.has_value(); }, 5000));
        QVERIFY2(*ok, qPrintable(summary));
        QCOMPARE(providerCalls, 1);
        QCOMPARE(summary, QStringLiteral("3 packets to each of 2 local networks (127.0.0.1, 127.0.0.1), ports %1 x3")
                              .arg(listener.localPort()));

        const MagicPacket packet(config.mac);
        int received = 0;
        while (received < 6 && QTest::qWaitFor([&listener] { return listener.hasPendingDatagrams(); }, 2000)) {
            const QNetworkDatagram datagram = listener.receiveDatagram();
            QCOMPARE(datagram.data().size(), 102);
            QVERIFY(std::equal(packet.bytes().begin(), packet.bytes().end(),
                               reinterpret_cast<const std::uint8_t*>(datagram.data().constData())));
            ++received;
        }
        QCOMPARE(received, 6);  // the plan twice, byte for byte
        QVERIFY(!QTest::qWaitFor([&listener] { return listener.hasPendingDatagrams(); }, 100));

        // A second send reads the list again, since networks change.
        std::optional<bool> again;
        auto* second = new QtWaker(config, provider, &parent);
        connect(second, &Waker::finished, [&again](bool allSent, const QString&) { again = allSent; });
        second->start();
        QVERIFY(QTest::qWaitFor([&again] { return again.has_value(); }, 5000));
        QCOMPARE(providerCalls, 2);
    }

    void wakerWithoutLocalNetworkFailsCleanly() {
        WakeTarget target;
        const WakeConfig config{*MacAddress::parse("00:00:5E:00:53:2A"), target, WakeDestination::LocalNetworks};
        std::optional<bool> ok;
        QString summary;
        QObject parent;
        auto* waker = new QtWaker(config, [] { return QStringList(); }, &parent);
        connect(waker, &Waker::finished, [&](bool allSent, const QString& text) {
            ok = allSent;
            summary = text;
        });
        waker->start();
        QVERIFY(QTest::qWaitFor([&ok] { return ok.has_value(); }, 5000));
        QVERIFY(!*ok);
        QVERIFY2(summary.contains(QStringLiteral("this Mac is not connected to a local network that supports broadcast")),
                 qPrintable(summary));
    }

    void localNetworksDescription() {
        WakeTarget target;
        const WakeConfig config{*MacAddress::parse("00:00:5E:00:53:2A"), target, WakeDestination::LocalNetworks};
        QCOMPARE(describe(config), QStringLiteral("10 packets to every local network of this Mac (ports 9 x5, 7 x5)"));
        QCOMPARE(destinationModeName(config.destinationMode), QStringLiteral("localNetworks"));
        QCOMPARE(destinationModeName(WakeDestination::Address), QStringLiteral("address"));
    }

    // ---- Resending the wake request while waiting (F1) ----

    void resendsAtTheIntervalUntilReady() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->readyAfterSends = 3;  // counted, not timed, so a loaded Mac cannot make it flaky
        script->wakeResults = {true};
        SessionConfig config = quickConfig(true);
        config.pollIntervalMs = 10;
        config.resendIntervalMs = 60;
        Session session(config, fakeFactories(script));
        std::vector<QDateTime> sends;
        connect(&session, &Session::timelineEvent, [&sends](const TimelineEvent& e) {
            if (e.kind == EventKind::WakeSent) sends.push_back(e.at);
        });
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyAfterWake);
        QVERIFY2(script->wakersCreated >= 3, qPrintable(QString::number(script->wakersCreated)));
        QVERIFY(run.hasEvent(QStringLiteral("Wake request sent: ")));
        QVERIFY(run.hasEvent(QStringLiteral("Wake request sent again (2nd time): 10 packets")));
        QVERIFY(run.hasEvent(QStringLiteral("Wake request sent again (3rd time)")));
        for (std::size_t i = 1; i < sends.size(); ++i) {
            QVERIFY2(sends[i - 1].msecsTo(sends[i]) >= 55, qPrintable(QString::number(sends[i - 1].msecsTo(sends[i]))));
        }
        QCOMPARE(session.observations().wakeSends, static_cast<int>(sends.size()));
        // Nothing more once the service is ready.
        const int created = script->wakersCreated;
        QTest::qWait(150);
        QCOMPARE(script->wakersCreated, created);
        QCOMPARE(script->maxWakersInFlight, 1);
    }

    void resendIntervalCountsFromTheEndOfTheSend() {
        // Each send takes 80 ms; the next one starts 50 ms after the previous one
        // ended, so finished sends are at least 130 ms apart (not 50 or 80).
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->readyAfterSends = 3;
        script->wakeResults = {true};
        script->wakeDelayMs = 80;
        SessionConfig config = quickConfig(true);
        config.pollIntervalMs = 10;
        config.resendIntervalMs = 50;
        config.overallTimeoutMs = 10000;
        Session session(config, fakeFactories(script));
        std::vector<QDateTime> sends;
        connect(&session, &Session::timelineEvent, [&sends](const TimelineEvent& e) {
            if (e.kind == EventKind::WakeSent) sends.push_back(e.at);
        });
        const Run run = runSession(session, 10000);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyAfterWake);
        QVERIFY(sends.size() >= 3);
        for (std::size_t i = 1; i < sends.size(); ++i) {
            QVERIFY2(sends[i - 1].msecsTo(sends[i]) >= 125, qPrintable(QString::number(sends[i - 1].msecsTo(sends[i]))));
        }
        QCOMPARE(script->maxWakersInFlight, 1);
    }

    void aResendStillInFlightAtReadyIsIgnored() {
        // The first request is quick; the resend takes 1.5 s and is still in
        // flight when the service answers (after 50 ms); the launch takes longer.
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->readyAfterMs = 50;
        script->wakeResults = {true};
        script->resendDelayMs = 1500;
        script->launchOk = true;
        script->launchDelayMs = 2000;
        SessionConfig config = quickConfig(true);
        config.pollIntervalMs = 5;
        config.resendIntervalMs = 1;
        config.overallTimeoutMs = 10000;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session, 10000);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyAfterWake);
        QCOMPARE(script->wakersCreated, 2);
        QCOMPARE(script->sendsDone, 2);  // the resend did finish, during the launch
        const auto ready = std::find(run.kinds.begin(), run.kinds.end(), EventKind::Ready);
        QVERIFY(ready != run.kinds.end());
        QVERIFY(std::find(ready, run.kinds.end(), EventKind::WakeSent) == run.kinds.end());
        QVERIFY(std::find(ready, run.kinds.end(), EventKind::Failure) == run.kinds.end());
        QCOMPARE(session.observations().wakeSends, 1);
        QVERIFY(!run.hasEvent(QStringLiteral("sent again")));
    }

    void resendsStopAtTheTimeLimit() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->wakeResults = {true};
        SessionConfig config = quickConfig(true);
        config.pollIntervalMs = 10;
        config.resendIntervalMs = 40;
        config.overallTimeoutMs = 200;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::TimedOut);
        QVERIFY2(script->wakersCreated >= 3, qPrintable(QString::number(script->wakersCreated)));
        const int sends = session.observations().wakeSends;
        QVERIFY2(run.summary.contains(QStringLiteral("Wake request sent %1 times between ").arg(sends)), qPrintable(run.summary));
        QVERIFY2(run.summary.contains(QStringLiteral("That confirms the packets left this computer")), qPrintable(run.summary));
        const int created = script->wakersCreated;
        QTest::qWait(150);
        QCOMPARE(script->wakersCreated, created);
    }

    void resendsStopWhenCancelled() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->wakeResults = {true};
        SessionConfig config = quickConfig(true);
        config.pollIntervalMs = 10;
        config.resendIntervalMs = 30;
        config.overallTimeoutMs = 10000;
        Session session(config, fakeFactories(script));
        QTimer::singleShot(120, &session, [&session] { session.cancel(); });
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::Cancelled);
        QVERIFY2(script->wakersCreated >= 2, qPrintable(QString::number(script->wakersCreated)));
        const int created = script->wakersCreated;
        QTest::qWait(150);
        QCOMPARE(script->wakersCreated, created);
    }

    void noResendWithZero() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        SessionConfig config = quickConfig(true);
        config.pollIntervalMs = 10;
        config.resendIntervalMs = 0;
        config.overallTimeoutMs = 200;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::TimedOut);
        QCOMPARE(script->wakersCreated, 1);
        QCOMPARE(session.observations().wakeSends, 1);
        QVERIFY(!run.hasEvent(QStringLiteral("sent again")));
        QVERIFY2(run.summary.contains(QStringLiteral("Wake request sent at")), qPrintable(run.summary));
    }

    void noResendWhenReadyAtOnce() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::Ready};
        SessionConfig config = quickConfig(true);
        config.resendIntervalMs = 20;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyWithoutWake);
        QTest::qWait(80);
        QCOMPARE(script->wakersCreated, 0);
    }

    void aFailedResendDoesNotEndTheRun() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->readyAfterMs = 250;
        script->wakeResults = {true, false, true};
        SessionConfig config = quickConfig(true);
        config.pollIntervalMs = 10;
        config.resendIntervalMs = 50;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyAfterWake);
        QVERIFY2(script->wakersCreated >= 3, qPrintable(QString::number(script->wakersCreated)));
        QVERIFY(run.hasEvent(QStringLiteral("Could not send the wake request again: 0 of 10 packets")));
        QVERIFY(run.hasKind(EventKind::Failure));
        // The failed one is not counted: the next success is the 2nd time it was sent.
        QVERIFY(run.hasEvent(QStringLiteral("Wake request sent again (2nd time)")));
    }

    void neverTwoWakersAtOnce() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->wakeResults = {true};
        script->wakeDelayMs = 40;  // each send takes longer than the interval
        SessionConfig config = quickConfig(true);
        config.pollIntervalMs = 5;
        config.resendIntervalMs = 5;
        config.overallTimeoutMs = 300;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::TimedOut);
        QVERIFY2(script->wakersCreated >= 3, qPrintable(QString::number(script->wakersCreated)));
        QCOMPARE(script->maxWakersInFlight, 1);
    }

    void explainSeveralSends() {
        Observations o;
        o.check.host = QStringLiteral("192.168.1.20");
        o.wakeConfigured = true;
        o.wakeSent = true;
        o.wakeSends = 8;
        o.wakeAt = QDateTime(QDate(2026, 1, 2), QTime(22, 41, 7));
        o.lastWakeAt = QDateTime(QDate(2026, 1, 2), QTime(22, 42, 52));
        o.wakeSummary = QStringLiteral("10 packets to 192.168.1.255 (ports 9 x5, 7 x5)");
        o.elapsedMs = 120000;
        const QString text = explainTimeout(o);
        QVERIFY2(text.contains(QStringLiteral("- Wake request sent 8 times between 22:41:07 and 22:42:52: 10 packets to "
                                              "192.168.1.255 (ports 9 x5, 7 x5). That confirms the packets left this "
                                              "computer, not that the PC received them.")),
                 qPrintable(text));
        o.wakeSends = 1;
        o.lastWakeAt = o.wakeAt;
        QVERIFY(explainTimeout(o).contains(QStringLiteral("- Wake request sent at 22:41:07: 10 packets")));
    }

    void ordinals() {
        QCOMPARE(ordinal(1), QStringLiteral("1st"));
        QCOMPARE(ordinal(2), QStringLiteral("2nd"));
        QCOMPARE(ordinal(3), QStringLiteral("3rd"));
        QCOMPARE(ordinal(4), QStringLiteral("4th"));
        QCOMPARE(ordinal(11), QStringLiteral("11th"));
        QCOMPARE(ordinal(12), QStringLiteral("12th"));
        QCOMPARE(ordinal(13), QStringLiteral("13th"));
        QCOMPARE(ordinal(21), QStringLiteral("21st"));
        QCOMPARE(ordinal(102), QStringLiteral("102nd"));
        QCOMPARE(ordinal(111), QStringLiteral("111th"));
    }

    void resendBounds() {
        QVERIFY(resendSecondsError(0).isEmpty());
        QVERIFY(resendSecondsError(5).isEmpty());
        QVERIFY(resendSecondsError(600).isEmpty());
        QVERIFY(!resendSecondsError(1).isEmpty());
        QVERIFY(!resendSecondsError(4).isEmpty());
        QVERIFY(!resendSecondsError(601).isEmpty());
        QVERIFY(!resendSecondsError(-1).isEmpty());
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

    void sessionLaunchFailure() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::Ready};
        script->launchOk = false;
        Session session(quickConfig(true), fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::LaunchFailed);
        QVERIFY(run.hasKind(EventKind::Ready));
        QVERIFY(run.hasKind(EventKind::Failure));
        QVERIFY2(run.summary.contains(QStringLiteral("fake client not found")), qPrintable(run.summary));
        QCOMPARE(session.state(), SessionState::Failed);
    }

    void sessionLaunchSuccessAfterWake() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut, ProbeStatus::Ready};
        script->launchOk = true;
        Session session(quickConfig(true), fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyAfterWake);
        QVERIFY(run.hasEvent(QStringLiteral("fake client launched")));
        QCOMPARE(session.state(), SessionState::Done);
    }

    void sessionKeepWaitingExtendsTheTimeout() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        SessionConfig config = quickConfig(false);
        config.overallTimeoutMs = 60;
        Session session(config, fakeFactories(script));
        QTimer::singleShot(10, &session, [&session] { session.extendTimeout(300); });
        QElapsedTimer clock;
        clock.start();
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::TimedOut);
        QVERIFY2(clock.elapsed() >= 250, qPrintable(QString::number(clock.elapsed())));
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

    void explainRefusedPointsAtTheService() {
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
        QVERIFY(text.contains(QStringLiteral("refused the connection")));
        QVERIFY(text.contains(QStringLiteral("not listening")));
        QVERIFY(!text.contains(QStringLiteral("The computer is up")));  // never claims on or off
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

    // ---- Tailscale: parsing and matching (pure) ----

    void tailscaleParsesARunningStatus() {
        const TailscaleStatus status = parseTailscaleStatus(kTailscaleRunning);
        QVERIFY(status.available);
        QVERIFY(status.running());
        QCOMPARE(status.backendState, QStringLiteral("Running"));
        QCOMPARE(status.peers.size(), std::size_t(2));  // Self is not a peer
        const TailscalePeer* pc = findTailscalePeer(status, QStringLiteral("100.64.0.10"));
        QVERIFY(pc != nullptr);
        QVERIFY(pc->online);
        QVERIFY(!pc->lastSeen.isValid());  // the zero time means unknown
        QCOMPARE(pc->displayName(), QStringLiteral("living-room-pc"));
        QCOMPARE(findTailscalePeer(status, QStringLiteral("FD7A:115C:A1E0::A")), pc);
        QCOMPARE(findTailscalePeer(status, QStringLiteral("living-room-pc.example-tailnet.ts.net")), pc);
        QCOMPARE(findTailscalePeer(status, QStringLiteral("Living-Room-PC.Example-Tailnet.TS.NET.")), pc);
        QCOMPARE(findTailscalePeer(status, QStringLiteral("living-room-pc")), pc);
        const TailscalePeer* nas = findTailscalePeer(status, QStringLiteral("office-nas"));
        QVERIFY(nas != nullptr);
        QVERIFY(!nas->online);
        QCOMPARE(nas->lastSeen, QDateTime(QDate(2026, 11, 12), QTime(10, 0), QTimeZone::UTC));
        QVERIFY(findTailscalePeer(status, QStringLiteral("100.64.0.99")) == nullptr);
        QVERIFY(findTailscalePeer(status, QStringLiteral("other-pc.example-tailnet.ts.net")) == nullptr);
        QVERIFY(findTailscalePeer(status, QStringLiteral("living-room-pc.other.ts.net")) == nullptr);  // dots: whole name only
        QVERIFY(findTailscalePeer(status, QStringLiteral("this-mac")) == nullptr);
    }

    void tailscaleDescribesWhatItShows() {
        const TailscaleStatus status = parseTailscaleStatus(kTailscaleRunning);
        const QDateTime now(QDate(2026, 11, 12), QTime(13, 5), QTimeZone::UTC);
        QCOMPARE(describeTailscale(status, QStringLiteral("100.64.0.10"), now),
                 QStringLiteral("Tailscale is connected; it shows living-room-pc online"));
        QCOMPARE(describeTailscale(status, QStringLiteral("100.64.0.11"), now),
                 QStringLiteral("Tailscale is connected; it shows office-nas offline, last seen 3 h ago"));
        QCOMPARE(describeTailscale(status, QStringLiteral("100.64.0.99"), now),
                 QStringLiteral("Tailscale is connected; 100.64.0.99 is not one of its devices"));
        QCOMPARE(formatAgo(5), QStringLiteral("less than a minute ago"));
        QCOMPARE(formatAgo(12 * 60 + 5), QStringLiteral("12 min ago"));
        QCOMPARE(formatAgo(3 * 3600), QStringLiteral("3 h ago"));
        QCOMPARE(formatAgo(4 * 86400), QStringLiteral("4 days ago"));
    }

    void tailscaleStoppedAndNeedsLogin() {
        for (const char* state : {"Stopped", "NeedsLogin"}) {
            const QByteArray json = QByteArray("{\"BackendState\": \"") + state + "\", \"Peer\": null}";
            const TailscaleStatus status = parseTailscaleStatus(json);
            QVERIFY(status.available);
            QVERIFY(!status.running());
            QVERIFY(status.peers.empty());
            QCOMPARE(describeTailscale(status, QStringLiteral("100.64.0.10")),
                     QStringLiteral("Tailscale is not connected on this Mac (state: %1)").arg(QLatin1String(state)));
        }
    }

    void tailscaleMissingFieldsKeepDefaults() {
        const TailscaleStatus status = parseTailscaleStatus(
            "{\"BackendState\": \"Running\", \"Peer\": {\"a\": {}, \"b\": 7, "
            "\"c\": {\"HostName\": \"game-box\", \"TailscaleIPs\": [\"100.64.0.12\", 5], \"LastSeen\": \"yesterday\"}}}");
        QVERIFY(status.available);
        QCOMPARE(status.peers.size(), std::size_t(2));  // the entry that is not an object is skipped
        const TailscalePeer* box = findTailscalePeer(status, QStringLiteral("game-box"));
        QVERIFY(box != nullptr);
        QCOMPARE(box->addresses, QStringList{QStringLiteral("100.64.0.12")});
        QVERIFY(!box->online);
        QVERIFY(!box->lastSeen.isValid());
        QCOMPARE(box->displayName(), QStringLiteral("game-box"));
        QCOMPARE(describeTailscale(status, QStringLiteral("100.64.0.12")),
                 QStringLiteral("Tailscale is connected; it shows game-box offline"));
    }

    void tailscaleGarbageIsUnavailable() {
        for (const QByteArray& garbage : {QByteArray("not json"), QByteArray("[]"), QByteArray("{}"),
                                          QByteArray("{\"BackendState\": 3}"), QByteArray("{\"BackendState\": \"\"}"),
                                          QByteArray()}) {
            const TailscaleStatus status = parseTailscaleStatus(garbage);
            QVERIFY2(!status.available, garbage.constData());
            QVERIFY(!status.error.isEmpty());
            QVERIFY(describeTailscale(status, QStringLiteral("100.64.0.10")).isEmpty());
        }
    }

    // ---- Tailscale: the reader against fake tools ----

    void tailscaleCandidates() {
        QCOMPARE(tailscaleCliCandidates(), QStringList{QStringLiteral("/nonexistent/wakedeck-test-tailscale")});
        QVERIFY(findTailscaleCli().isEmpty());  // the variable is the only candidate, even when missing
        qunsetenv("WAKEDECK_TAILSCALE_CLI");
        const auto restore = qScopeGuard([] { qputenv("WAKEDECK_TAILSCALE_CLI", QByteArrayLiteral("/nonexistent/wakedeck-test-tailscale")); });
        QCOMPARE(tailscaleCliCandidates(),
                 (QStringList{QStringLiteral("/Applications/Tailscale.app/Contents/MacOS/Tailscale"),
                              QStringLiteral("/usr/local/bin/tailscale"), QStringLiteral("/opt/homebrew/bin/tailscale")}));
        QCOMPARE(ProcessTailscaleReader::kDefaultLimitMs, 3000);
    }

    void tailscaleReaderReadsTheAnswer() {
        QTemporaryDir dir;
        const QString tool = writeScript(dir, QStringLiteral("tailscale"),
                                         QByteArray("[ \"$1 $2\" = \"status --json\" ] || exit 9\ncat <<'END'\n") +
                                             kTailscaleRunning + "\nEND\n");
        const auto status = readWithTool(tool);
        QVERIFY(status.has_value());
        QVERIFY2(status->available, qPrintable(status->error));
        QCOMPARE(status->backendState, QStringLiteral("Running"));
        QCOMPARE(status->peers.size(), std::size_t(2));
    }

    void tailscaleReaderStopsASlowTool() {
        QTemporaryDir dir;
        const QString tool = writeScript(dir, QStringLiteral("slow"), "exec sleep 30\n");
        QElapsedTimer clock;
        clock.start();
        const auto status = readWithTool(tool, 300, 10000);
        QVERIFY(status.has_value());
        QVERIFY(!status->available);
        QVERIFY2(status->error.contains(QStringLiteral("did not answer within 300 ms")), qPrintable(status->error));
        QVERIFY(clock.elapsed() < 10000);  // ended by the limit, not by the tool
    }

    void tailscaleReaderReportsAFailingTool() {
        QTemporaryDir dir;
        const QString tool = writeScript(dir, QStringLiteral("failing"), "echo 'something went wrong' >&2\nexit 1\n");
        const auto status = readWithTool(tool);
        QVERIFY(status.has_value());
        QVERIFY(!status->available);
        QVERIFY2(status->error.contains(QStringLiteral("exited with code 1")), qPrintable(status->error));
    }

    void tailscaleReaderWithoutTheTool() {
        const auto status = readWithTool(QStringLiteral("/nonexistent/wakedeck-test-tailscale"));
        QVERIFY(status.has_value());
        QVERIFY(!status->available);
        QVERIFY2(status->error.contains(QStringLiteral("not found")), qPrintable(status->error));
    }

    void tailscaleRealToolWhenAsked() {
        // Optional: runs Tailscale's real tool only with WAKEDECK_TEST_REAL_TAILSCALE=1,
        // read-only, and prints nothing it returns (no names, no addresses).
        const QString real = QStringLiteral("/Applications/Tailscale.app/Contents/MacOS/Tailscale");
        if (qgetenv("WAKEDECK_TEST_REAL_TAILSCALE") != "1" || !QFileInfo(real).isExecutable()) {
            qInfo("not run: needs WAKEDECK_TEST_REAL_TAILSCALE=1 and Tailscale installed");
            return;
        }
        const auto status = readWithTool(real);
        QVERIFY(status.has_value());  // an answer or "unavailable", always within the limit
        if (status->available) QVERIFY(!status->backendState.isEmpty());
    }

    // ---- Tailscale in the session ----

    void sessionStopsAtOnceWhenTailscaleIsOff() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::Ready};
        script->tailscale = {tailscaleState(QStringLiteral("Stopped"))};
        Session session(tailscaleConfig(), fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::TailscaleOff);
        QCOMPARE(run.summary, QStringLiteral("Tailscale is not connected on this Mac (state: Stopped), so 100.64.0.10 cannot "
                                             "be reached. Turn Tailscale on and try again."));
        QCOMPARE(script->readersCreated, 1);
        QCOMPARE(script->probesCreated, 0);
        QCOMPARE(script->wakersCreated, 0);
        QCOMPARE(run.events, QStringList{QStringLiteral("Tailscale is not connected on this Mac (state: Stopped)")});
        QCOMPARE(run.kinds.front(), EventKind::Failure);
        QCOMPARE(session.state(), SessionState::Failed);
        QCOMPARE(outcomeName(Outcome::TailscaleOff), QStringLiteral("Tailscale not connected"));
    }

    void sessionGoesOnWhenTailscaleShowsTheDeviceOffline() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut, ProbeStatus::Ready};
        script->tailscale = {tailscaleWithPc(false)};
        Session session(tailscaleConfig(), fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyAfterWake);
        QCOMPARE(run.events.value(0), QStringLiteral("Tailscale is connected; it shows living-room-pc offline"));
        QVERIFY(run.events.value(1).startsWith(QStringLiteral("Checking ")));
        QCOMPARE(script->wakersCreated, 1);
        QCOMPARE(script->readersCreated, 1);  // read once more only at a timeout
    }

    void sessionReadsTailscaleOnceMoreAtATimeout() {
        struct Case {
            TailscaleStatus atEnd;
            QString expected;
        };
        const std::vector<Case> cases = {
            {tailscaleWithPc(true), QStringLiteral("Tailscale shows the computer online. If that is current (Tailscale can "
                                                   "take a while to notice that a computer went to sleep), the computer is "
                                                   "awake and the service on port 47989 is what did not answer: check that "
                                                   "it is running.")},
            {tailscaleWithPc(false), QStringLiteral("Tailscale still shows the computer offline, so it most likely did not "
                                                    "wake up, or woke up without Tailscale.")},
            {tailscaleState(QStringLiteral("Stopped")), QStringLiteral("Tailscale is no longer connected on this Mac")},
        };
        for (const Case& c : cases) {
            auto script = std::make_shared<Script>();
            script->statuses = {ProbeStatus::TimedOut};
            script->tailscale = {tailscaleWithPc(false), c.atEnd};
            SessionConfig config = tailscaleConfig();
            config.overallTimeoutMs = 60;
            Session session(config, fakeFactories(script));
            const Run run = runSession(session);
            QVERIFY(run.outcome.has_value());
            QCOMPARE(*run.outcome, Outcome::TimedOut);
            QVERIFY2(run.summary.contains(c.expected), qPrintable(run.summary));
            QCOMPARE(script->readersCreated, 2);
        }
    }

    void keepWaitingIsRefusedOnceTimedOut() {
        // The second Tailscale read answers late; meanwhile the run has timed out
        // and stays in Waiting, but the wait cannot be extended any more.
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->tailscale = {tailscaleWithPc(false)};
        script->lateReadMs = 400;
        SessionConfig config = tailscaleConfig();
        config.overallTimeoutMs = 60;
        Session session(config, fakeFactories(script));
        std::optional<bool> concludingAtTimeout;
        std::optional<bool> extended;
        connect(&session, &Session::timelineEvent, &session, [&](const TimelineEvent& e) {
            if (!e.text.startsWith(QStringLiteral("Timed out after"))) return;
            concludingAtTimeout = session.concluding();
            extended = session.extendTimeout(60000);
        });
        QVERIFY(session.extendTimeout(0) == false);  // nothing to add
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::TimedOut);
        QCOMPARE(concludingAtTimeout, std::optional<bool>(true));
        QCOMPARE(extended, std::optional<bool>(false));
        QCOMPARE(script->readersCreated, 2);
        QVERIFY(!session.extendTimeout(60000));  // finished
    }

    void sessionWithoutATailscaleAnswerIsUnchanged() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->tailscale = {tailscaleUnavailable()};
        SessionConfig config = tailscaleConfig();
        config.overallTimeoutMs = 60;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::TimedOut);
        QCOMPARE(script->readersCreated, 1);  // no second read without a first answer
        QVERIFY(run.events.value(0).startsWith(QStringLiteral("Checking ")));
        QVERIFY(!run.hasEvent(QStringLiteral("Tailscale is")));
        QVERIFY(!run.summary.contains(QStringLiteral("Tailscale shows")));
        QVERIFY(!run.summary.contains(QStringLiteral("no longer connected")));

        // A factory that returns no reader at all: the same.
        auto none = std::make_shared<Script>();
        none->statuses = {ProbeStatus::Ready};
        Session quiet(tailscaleConfig(), fakeFactories(none));
        const Run ready = runSession(quiet);
        QVERIFY(ready.outcome.has_value());
        QCOMPARE(*ready.outcome, Outcome::ReadyWithoutWake);
        QCOMPARE(none->readersCreated, 1);
    }

    void sessionNeverAsksTailscaleForOtherHosts() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->tailscale = {tailscaleState(QStringLiteral("Stopped"))};
        SessionConfig config = quickConfig(true);  // 192.0.2.10
        config.overallTimeoutMs = 60;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::TimedOut);
        QCOMPARE(script->readersCreated, 0);
        QVERIFY(!run.hasEvent(QStringLiteral("Tailscale")));
    }

    void explainAddsWhatTailscaleShows() {
        Observations o;
        o.check.host = QStringLiteral("100.64.0.10");
        o.check.port = 47989;
        o.wakeConfigured = true;
        o.elapsedMs = 60000;
        QVERIFY(!explainTimeout(o).contains(QStringLiteral("Tailscale shows")));
        o.tailscale = TailscaleAtTimeout::DeviceOnline;
        QVERIFY(explainTimeout(o).contains(QStringLiteral("Tailscale shows the computer online")));
        o.tailscale = TailscaleAtTimeout::DeviceOffline;
        QVERIFY(explainTimeout(o).contains(QStringLiteral("Tailscale still shows the computer offline")));
        o.tailscale = TailscaleAtTimeout::NotRunning;
        QVERIFY(explainTimeout(o).contains(QStringLiteral("Tailscale is no longer connected on this Mac, so 100.64.0.10")));
    }

    // ---- Learned timing in the session ----

    void sessionSaysWhatIsUsualAndWhenItTakesLonger() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut};
        script->readyAfterMs = 150;
        SessionConfig config = quickConfig(true);
        config.pollIntervalMs = 10;
        LearnedTiming learned;
        learned.entries = 5;
        learned.medianOf = 5;
        learned.typicalMs = 30000;
        learned.lastMs = 26000;
        learned.longerAfterMs = 30;  // milliseconds here; the real rule is tested with the history
        config.learned = learned;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyAfterWake);
        int sent = -1;
        for (int i = 0; i < run.events.size() && sent < 0; ++i) {
            if (run.events[i].startsWith(QStringLiteral("Wake request sent: "))) sent = i;
        }
        QVERIFY(sent >= 0);
        QCOMPARE(run.events.value(sent + 1), QStringLiteral("Usually ready about 0:30 after the wake request (last 5 runs)"));
        QCOMPARE(run.events.filter(QStringLiteral("Taking longer than usual")).size(), 1);
        QVERIFY(session.wakeToReadyMs().has_value());
        QVERIFY(*session.wakeToReadyMs() >= 100);
    }

    void sessionWithFewRunsSaysLastTime() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::TimedOut, ProbeStatus::Ready};
        SessionConfig config = quickConfig(true);
        LearnedTiming learned;
        learned.entries = 2;
        learned.medianOf = 2;
        learned.typicalMs = 30000;
        learned.lastMs = 26000;
        learned.longerAfterMs = 60000;
        config.learned = learned;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QVERIFY(run.hasEvent(QStringLiteral("Last time: 0:26")));
        QVERIFY(!run.hasEvent(QStringLiteral("Taking longer")));
    }

    void sessionWithoutWakeHasNoWakeToReadyTime() {
        auto script = std::make_shared<Script>();
        script->statuses = {ProbeStatus::Ready};
        SessionConfig config = quickConfig(true);
        LearnedTiming learned;
        learned.entries = 4;
        learned.medianOf = 4;
        learned.typicalMs = 30000;
        learned.lastMs = 30000;
        learned.longerAfterMs = 5;
        config.learned = learned;
        Session session(config, fakeFactories(script));
        const Run run = runSession(session);
        QVERIFY(run.outcome.has_value());
        QCOMPARE(*run.outcome, Outcome::ReadyWithoutWake);
        QVERIFY(!session.wakeToReadyMs().has_value());
        QVERIFY(!run.hasEvent(QStringLiteral("Usually ready")));

        auto failed = std::make_shared<Script>();
        failed->statuses = {ProbeStatus::TimedOut};
        SessionConfig timing = quickConfig(true);
        timing.overallTimeoutMs = 60;
        Session timedOut(timing, fakeFactories(failed));
        const Run late = runSession(timedOut);
        QVERIFY(late.outcome.has_value());
        QCOMPARE(*late.outcome, Outcome::TimedOut);
        QVERIFY(!timedOut.wakeToReadyMs().has_value());
    }
};

QTEST_GUILESS_MAIN(EngineTest)
#include "test_engine.moc"
