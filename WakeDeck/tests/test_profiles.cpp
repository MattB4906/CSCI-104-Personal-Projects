// Qt Test suite for profiles: the JSON form and its validation, the store with
// its schema version and recovery paths, import and export. Everything happens
// in a temporary folder; the real profiles file is never touched.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <chrono>

#include "engine/Profile.h"
#include "engine/ProfileStore.h"
#include "engine/TimingHistory.h"

using namespace wakedeck;

namespace {

Profile fullProfile() {
    Profile profile;
    profile.name = QStringLiteral("Living Room PC");
    profile.notes = QStringLiteral("test notes");
    profile.check.host = QStringLiteral("living-room-pc.example.net");
    profile.check.port = 47989;
    profile.check.kind = ProbeKind::Sunshine;
    profile.check.timeoutMs = 2500;
    profile.pollIntervalMs = 4000;
    profile.overallTimeoutMs = 90000;
    WakeTarget target;
    target.destination = "192.168.1.255";
    target.ports = {9, 7, 12};
    target.repeat = 3;
    target.interval = std::chrono::milliseconds(50);
    profile.wake = WakeConfig{*MacAddress::parse("00:00:5E:00:53:2A"), target};
    profile.client.preset = ClientConfig::Preset::Moonlight;
    profile.client.launchMode = QStringLiteral("stream");
    profile.client.appName = QStringLiteral("Desktop");
    return profile;
}

QJsonObject parse(const char* text) {
    return QJsonDocument::fromJson(text).object();
}

bool writeText(const QString& path, const QByteArray& text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(text);
    return true;
}

QStringList filesStartingWith(const QString& dir, const QString& prefix) {
    return QDir(dir).entryList({prefix + QStringLiteral("*")}, QDir::Files);
}

}  // namespace

class ProfilesTest : public QObject {
    Q_OBJECT

private slots:
    // ---- JSON form ----

    void roundTripKeepsEveryField() {
        const Profile original = fullProfile();
        QString error;
        const auto restored = profileFromJson(toJson(original), &error);
        QVERIFY2(restored.has_value(), qPrintable(error));
        QCOMPARE(toJson(*restored), toJson(original));
        QCOMPARE(restored->name, original.name);
        QCOMPARE(restored->check.host, original.check.host);
        QCOMPARE(restored->check.timeoutMs, 2500);
        QCOMPARE(restored->pollIntervalMs, 4000);
        QCOMPARE(restored->overallTimeoutMs, 90000);
        QVERIFY(restored->wake.has_value());
        QCOMPARE(QString::fromStdString(restored->wake->mac.toString()), QStringLiteral("00:00:5E:00:53:2A"));
        QCOMPARE(restored->wake->target.destination, std::string("192.168.1.255"));
        QCOMPARE(restored->wake->target.ports, (std::vector<std::uint16_t>{9, 7, 12}));
        QCOMPARE(restored->wake->target.repeat, 3u);
        QCOMPARE(restored->wake->target.interval.count(), 50);
        QCOMPARE(restored->client.preset, ClientConfig::Preset::Moonlight);
        QCOMPARE(restored->client.appName, QStringLiteral("Desktop"));
    }

    void minimalJsonGetsDefaults() {
        QString error;
        const auto profile = profileFromJson(parse(R"({"name": "Minimal", "check": {"host": "pc.example.net"}})"), &error);
        QVERIFY2(profile.has_value(), qPrintable(error));
        QCOMPARE(profile->check.port, quint16(47989));
        QCOMPARE(profile->check.kind, ProbeKind::Sunshine);
        QCOMPARE(profile->check.timeoutMs, 2000);
        QCOMPARE(profile->pollIntervalMs, 3000);
        QCOMPARE(profile->overallTimeoutMs, 120000);
        QVERIFY(!profile->wake.has_value());
        QCOMPARE(profile->client.preset, ClientConfig::Preset::None);
        QCOMPARE(profile->client.launchMode, QStringLiteral("open"));
        QVERIFY(profile->notes.isEmpty());
    }

    void explicitNullWakeMeansNoWake() {
        QString error;
        const auto profile = profileFromJson(parse(R"({"name": "X", "check": {"host": "h"}, "wake": null})"), &error);
        QVERIFY2(profile.has_value(), qPrintable(error));
        QVERIFY(!profile->wake.has_value());
    }

    void wakeDefaultsMatchTheScript() {
        QString error;
        const auto profile = profileFromJson(
            parse(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00005E00532A", "destination": "192.168.1.255"}})"),
            &error);
        QVERIFY2(profile.has_value(), qPrintable(error));
        QVERIFY(profile->wake.has_value());
        QCOMPARE(profile->wake->target.ports, (std::vector<std::uint16_t>{9, 7}));
        QCOMPARE(profile->wake->target.repeat, 5u);
        QCOMPARE(profile->wake->target.interval.count(), 100);
    }

    void version1ProfileGetsTheNewDefaults() {
        QString error;
        const auto profile = profileFromJson(
            parse(R"({"schemaVersion": 1, "name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destination": "192.168.1.255"}})"),
            &error);
        QVERIFY2(profile.has_value(), qPrintable(error));
        QCOMPARE(profile->wake->destinationMode, WakeDestination::Address);
        QCOMPARE(profile->wake->resendSeconds, 15);
        QCOMPARE(profile->toSessionConfig().resendIntervalMs, 15000);
        // Written back as version 2, with both new fields spelled out.
        const QJsonObject json = toJson(*profile);
        QCOMPARE(json.value(QStringLiteral("schemaVersion")).toInt(), 2);
        QCOMPARE(json.value(QStringLiteral("wake")).toObject().value(QStringLiteral("destinationMode")).toString(), QStringLiteral("address"));
        QCOMPARE(json.value(QStringLiteral("wake")).toObject().value(QStringLiteral("resendSeconds")).toInt(), 15);
    }

    void localNetworksAndResendRoundTrip() {
        Profile original = fullProfile();
        original.wake->destinationMode = WakeDestination::LocalNetworks;
        original.wake->target.destination.clear();
        original.wake->resendSeconds = 0;
        QString error;
        const auto restored = profileFromJson(toJson(original), &error);
        QVERIFY2(restored.has_value(), qPrintable(error));
        QCOMPARE(toJson(*restored), toJson(original));
        QCOMPARE(restored->wake->destinationMode, WakeDestination::LocalNetworks);
        QVERIFY(restored->wake->target.destination.empty());
        QCOMPARE(restored->wake->resendSeconds, 0);
        QCOMPARE(restored->toSessionConfig().resendIntervalMs, 0);
        QCOMPARE(toJson(original).value(QStringLiteral("wake")).toObject().value(QStringLiteral("destination")).toString(), QString());

        // The bounds: 0, 5 and 600 are accepted.
        for (int seconds : {0, 5, 600}) {
            QJsonObject json = toJson(fullProfile());
            QJsonObject wake = json.value(QStringLiteral("wake")).toObject();
            wake[QStringLiteral("resendSeconds")] = seconds;
            json[QStringLiteral("wake")] = wake;
            const auto bounded = profileFromJson(json, &error);
            QVERIFY2(bounded.has_value(), qPrintable(error));
            QCOMPARE(bounded->wake->resendSeconds, seconds);
        }
        QCOMPARE(describe(*restored->wake), QStringLiteral("9 packets to every local network of this Mac (ports 9 x3, 7 x3, 12 x3)"));
        QCOMPARE(describeResend(*restored->wake), QStringLiteral("sent once"));
        QCOMPARE(describeResend(*fullProfile().wake), QStringLiteral("again every 15 s while waiting"));

        // A hand edit that changes only the mode keeps the old destination: it is
        // ignored and written back empty (review finding D; was rejected before).
        QJsonObject edited = toJson(fullProfile());
        QJsonObject wake = edited.value(QStringLiteral("wake")).toObject();
        QVERIFY(!wake.value(QStringLiteral("destination")).toString().isEmpty());
        wake[QStringLiteral("destinationMode")] = QStringLiteral("localNetworks");
        edited[QStringLiteral("wake")] = wake;
        const auto handEdited = profileFromJson(edited, &error);
        QVERIFY2(handEdited.has_value(), qPrintable(error));
        QCOMPARE(handEdited->wake->destinationMode, WakeDestination::LocalNetworks);
        QVERIFY(handEdited->wake->target.destination.empty());
        QCOMPARE(toJson(*handEdited).value(QStringLiteral("wake")).toObject().value(QStringLiteral("destination")).toString(), QString());
    }

    void invalidJsonIsRejectedWithAReason_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("expectedWords");
        QTest::newRow("no name") << QByteArray(R"({"check": {"host": "h"}})") << QStringLiteral("name");
        QTest::newRow("blank name") << QByteArray(R"({"name": "  ", "check": {"host": "h"}})") << QStringLiteral("name");
        QTest::newRow("no check") << QByteArray(R"({"name": "X"})") << QStringLiteral("check");
        QTest::newRow("blank host") << QByteArray(R"({"name": "X", "check": {"host": ""}})") << QStringLiteral("host");
        QTest::newRow("bad kind") << QByteArray(R"({"name": "X", "check": {"host": "h", "kind": "ssh"}})") << QStringLiteral("kind");
        QTest::newRow("port zero") << QByteArray(R"({"name": "X", "check": {"host": "h", "port": 0}})") << QStringLiteral("port");
        QTest::newRow("port text") << QByteArray(R"({"name": "X", "check": {"host": "h", "port": "47989"}})") << QStringLiteral("port");
        QTest::newRow("fractional poll") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "pollIntervalMs": 2.5})") << QStringLiteral("pollIntervalMs");
        QTest::newRow("bad mac") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "nope", "destination": "d"}})") << QStringLiteral("mac");
        QTest::newRow("no destination") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A"}})") << QStringLiteral("destination");
        QTest::newRow("empty ports") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destination": "d", "ports": []}})") << QStringLiteral("ports");
        QTest::newRow("port too big") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destination": "d", "ports": [70000]}})") << QStringLiteral("ports");
        QTest::newRow("repeat zero") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destination": "d", "repeat": 0}})") << QStringLiteral("repeat");
        QTest::newRow("wake not object") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": 5})") << QStringLiteral("wake");
        QTest::newRow("unknown method") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"method": "smartPlug", "mac": "00:00:5E:00:53:2A", "destination": "d"}})") << QStringLiteral("method");
        QTest::newRow("bad preset") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"preset": "steam"}})") << QStringLiteral("preset");
        QTest::newRow("bad launch mode") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"launchMode": "fly"}})") << QStringLiteral("launchMode");
        QTest::newRow("arguments not list") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"arguments": "x"}})") << QStringLiteral("arguments");
        QTest::newRow("newer schema") << QByteArray(R"({"schemaVersion": 3, "name": "X", "check": {"host": "h"}})") << QStringLiteral("newer");
        QTest::newRow("schema zero") << QByteArray(R"({"schemaVersion": 0, "name": "X", "check": {"host": "h"}})") << QStringLiteral("schemaVersion");
        // Schema version 2: the destination mode and the resend interval.
        QTest::newRow("bad destination mode") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destination": "d", "destinationMode": "everywhere"}})") << QStringLiteral("destinationMode");
        QTest::newRow("address mode without destination") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destinationMode": "address"}})") << QStringLiteral("destination");
        QTest::newRow("resend below 5") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destination": "d", "resendSeconds": 4}})") << QStringLiteral("resendSeconds");
        QTest::newRow("resend above 600") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destination": "d", "resendSeconds": 601}})") << QStringLiteral("resendSeconds");
        QTest::newRow("resend negative") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destination": "d", "resendSeconds": -1}})") << QStringLiteral("resendSeconds");
        QTest::newRow("resend as text") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destination": "d", "resendSeconds": "15"}})") << QStringLiteral("resendSeconds");
        QTest::newRow("unknown wake field in version 2") << QByteArray(R"({"schemaVersion": 2, "name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destination": "d", "resendSecs": 15}})") << QStringLiteral("\"resendSecs\"");
        // Client rules, identical to the command line's.
        QTest::newRow("custom without path") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"preset": "custom"}})") << QStringLiteral("path");
        QTest::newRow("stream without app") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"preset": "moonlight", "launchMode": "stream"}})") << QStringLiteral("app");
        QTest::newRow("app without stream") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"preset": "moonlight", "appName": "Desktop"}})") << QStringLiteral("stream");
        QTest::newRow("none with path") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"path": "/x"}})") << QStringLiteral("client");
        QTest::newRow("none with stream") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"launchMode": "stream", "appName": "Desktop"}})") << QStringLiteral("moonlight");
        QTest::newRow("custom with stream") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"preset": "custom", "path": "/x", "launchMode": "stream", "appName": "D"}})") << QStringLiteral("moonlight");
        QTest::newRow("moonlight with arguments") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"preset": "moonlight", "arguments": ["a"]}})") << QStringLiteral("custom");
        QTest::newRow("relative client path") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"preset": "custom", "path": "bin/tool"}})") << QStringLiteral("absolute");
        // Mistyped field names are reported instead of silently replaced by defaults.
        QTest::newRow("unknown top-level field") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "pollIntervalMS": 1})") << QStringLiteral("pollIntervalMS");
        QTest::newRow("unknown check field") << QByteArray(R"({"name": "X", "check": {"host": "h", "timeoutMS": 9}})") << QStringLiteral("timeoutMS");
        QTest::newRow("unknown wake field") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "wake": {"mac": "00:00:5E:00:53:2A", "destination": "d", "port": 9}})") << QStringLiteral("\"port\"");
        QTest::newRow("unknown client field") << QByteArray(R"({"name": "X", "check": {"host": "h"}, "client": {"preset": "moonlight", "app": "Desktop"}})") << QStringLiteral("\"app\"");
    }

    void consistentClientsAreAccepted() {
        const char* const moonlightWithPath =
            R"({"name": "X", "check": {"host": "h"}, "client": {"preset": "moonlight", "path": "/Applications/Moonlight.app"}})";
        const char* const moonlightStreaming =
            R"({"name": "X", "check": {"host": "h"}, "client": {"preset": "moonlight", "launchMode": "stream", "appName": "Desktop"}})";
        const char* const customWithArguments =
            R"({"name": "X", "check": {"host": "h"}, "client": {"preset": "custom", "path": "/usr/bin/open", "arguments": ["rdp://{host}"]}})";
        for (const char* json : {moonlightWithPath, moonlightStreaming, customWithArguments}) {
            QString error;
            const bool accepted = profileFromJson(parse(json), &error).has_value();
            QVERIFY2(accepted, qPrintable(error));
        }
        ClientConfig none;
        QVERIFY(clientConfigError(none).isEmpty());
    }

    void invalidJsonIsRejectedWithAReason() {
        QFETCH(QByteArray, json);
        QFETCH(QString, expectedWords);
        QString error;
        const auto profile = profileFromJson(QJsonDocument::fromJson(json).object(), &error);
        QVERIFY(!profile.has_value());
        QVERIFY2(error.contains(expectedWords), qPrintable(error));
    }

    void nameHelpers() {
        std::vector<Profile> profiles;
        Profile a = fullProfile();
        a.name = QStringLiteral("Milan PC");
        upsertProfile(profiles, a);
        QCOMPARE(profiles.size(), std::size_t(1));
        QVERIFY(findProfile(profiles, QStringLiteral("milan pc")) != nullptr);
        QVERIFY(findProfile(profiles, QStringLiteral("  MILAN PC ")) != nullptr);
        QVERIFY(findProfile(profiles, QStringLiteral("Other")) == nullptr);

        Profile replacement = a;
        replacement.notes = QStringLiteral("replaced");
        upsertProfile(profiles, replacement);
        QCOMPARE(profiles.size(), std::size_t(1));
        QCOMPARE(profiles.front().notes, QStringLiteral("replaced"));

        QVERIFY(!removeProfile(profiles, QStringLiteral("Other")));
        QVERIFY(removeProfile(profiles, QStringLiteral("MILAN PC")));
        QVERIFY(profiles.empty());
    }

    void clientDescriptions() {
        ClientConfig client;
        QCOMPARE(describe(client), QStringLiteral("none"));
        client.preset = ClientConfig::Preset::Moonlight;
        QCOMPARE(describe(client), QStringLiteral("Moonlight, open"));
        client.launchMode = QStringLiteral("stream");
        client.appName = QStringLiteral("Desktop");
        QCOMPARE(describe(client), QStringLiteral("Moonlight, stream \"Desktop\""));
        client.preset = ClientConfig::Preset::Custom;
        client.path = QStringLiteral("/usr/bin/open");
        client.arguments = {QStringLiteral("rdp://{host}")};
        QCOMPARE(describe(client), QStringLiteral("custom: /usr/bin/open rdp://{host}"));
    }

    // ---- The example shipped in the repository ----

    void exampleProfileLoads() {
        QString error;
        const auto profile = ProfileStore::readProfileFile(QStringLiteral(WAKEDECK_EXAMPLES_DIR "/example-profile.json"), &error);
        QVERIFY2(profile.has_value(), qPrintable(error));
        QCOMPARE(profile->name, QStringLiteral("Living Room PC (example)"));
        QVERIFY(profile->wake.has_value());
        QCOMPARE(QString::fromStdString(profile->wake->mac.toString()), QStringLiteral("00:00:5E:00:53:2A"));
        QCOMPARE(profile->check.host, QStringLiteral("living-room-pc.example.net"));
        QCOMPARE(profile->client.preset, ClientConfig::Preset::Moonlight);
        QCOMPARE(profile->client.appName, QStringLiteral("Desktop"));
        QVERIFY(profile->notes.contains(QStringLiteral("Fictional")));
        QCOMPARE(profile->wake->destinationMode, WakeDestination::Address);
        QCOMPARE(profile->wake->resendSeconds, 15);
        // The shipped file is version 2 and spells out both new fields.
        QFile file(QStringLiteral(WAKEDECK_EXAMPLES_DIR "/example-profile.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(json.value(QStringLiteral("schemaVersion")).toInt(), 2);
        QVERIFY(json.value(QStringLiteral("wake")).toObject().contains(QStringLiteral("destinationMode")));
        QVERIFY(json.value(QStringLiteral("wake")).toObject().contains(QStringLiteral("resendSeconds")));
    }

    // ---- The store ----

    void storeSavesAndLoads() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("nested/folder/profiles.json"));  // folders get created
        const ProfileStore store(path);
        std::vector<Profile> profiles;
        profiles.push_back(fullProfile());
        Profile second;
        second.name = QStringLiteral("Office PC");
        second.check.host = QStringLiteral("office-pc.example.net");
        second.check.kind = ProbeKind::Tcp;
        second.check.port = 3389;
        profiles.push_back(second);

        QString error;
        QVERIFY2(store.save(profiles, &error), qPrintable(error));
        QVERIFY(QFile::exists(path));
        QCOMPARE(QDir(dir.filePath(QStringLiteral("nested/folder"))).entryList(QDir::Files).size(), 1);  // no temp files left

        const ProfileStore::LoadResult loaded = store.load();
        QVERIFY2(loaded.ok, qPrintable(loaded.error));
        QVERIFY(loaded.warning.isEmpty());
        QCOMPARE(loaded.profiles.size(), std::size_t(2));
        QCOMPARE(loaded.profiles[0].name, QStringLiteral("Living Room PC"));
        QCOMPARE(loaded.profiles[1].check.port, quint16(3389));
        QCOMPARE(loaded.profiles[1].check.kind, ProbeKind::Tcp);
        QVERIFY(!loaded.profiles[1].wake.has_value());

        // The file carries the schema version at the top level.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), Profile::kSchemaVersion);
        QCOMPARE(root.value(QStringLiteral("profiles")).toArray().size(), 2);
    }

    void missingStoreIsEmptyAndFine() {
        QTemporaryDir dir;
        const ProfileStore store(dir.filePath(QStringLiteral("profiles.json")));
        const ProfileStore::LoadResult loaded = store.load();
        QVERIFY(loaded.ok);
        QVERIFY(loaded.profiles.empty());
        QVERIFY(loaded.warning.isEmpty());
    }

    void corruptStoreIsMovedAside() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        QVERIFY(writeText(path, "this is not json {"));
        const ProfileStore store(path);
        const ProfileStore::LoadResult loaded = store.load();
        QVERIFY2(loaded.ok, qPrintable(loaded.error));
        QVERIFY(loaded.profiles.empty());
        QVERIFY2(loaded.warning.contains(QStringLiteral("moved")), qPrintable(loaded.warning));
        QVERIFY(!QFile::exists(path));
        QCOMPARE(filesStartingWith(dir.path(), QStringLiteral("profiles.json.corrupt-")).size(), 1);
    }

    void corruptStoreTwiceWithinASecondIsStillRecovered() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        const ProfileStore store(path);
        for (int round = 0; round < 2; ++round) {
            QVERIFY(writeText(path, "still not json"));
            const ProfileStore::LoadResult loaded = store.load();
            QVERIFY2(loaded.ok, qPrintable(loaded.error));
            QVERIFY2(loaded.warning.contains(QStringLiteral("It was moved")), qPrintable(loaded.warning));
        }
        QCOMPARE(filesStartingWith(dir.path(), QStringLiteral("profiles.json.corrupt-")).size(), 2);
    }

    void invalidTopLevelSchemaVersionIsCorrupt() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        QVERIFY(writeText(path, R"({"schemaVersion": 0, "profiles": []})"));
        const ProfileStore::LoadResult loaded = ProfileStore(path).load();
        QVERIFY(loaded.ok);
        QVERIFY(loaded.profiles.empty());
        QVERIFY2(loaded.warning.contains(QStringLiteral("schemaVersion")), qPrintable(loaded.warning));
        QCOMPARE(filesStartingWith(dir.path(), QStringLiteral("profiles.json.corrupt-")).size(), 1);
    }

    void singleProfileFileIsNotAStore() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("one.json"));
        QString error;
        QVERIFY(ProfileStore::writeProfileFile(fullProfile(), path, &error));
        QFile before(path);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray content = before.readAll();
        before.close();

        const ProfileStore::LoadResult loaded = ProfileStore(path).load();
        QVERIFY(!loaded.ok);
        QVERIFY2(loaded.error.contains(QStringLiteral("single profile")), qPrintable(loaded.error));
        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), content);
        QCOMPARE(filesStartingWith(dir.path(), QStringLiteral("one.json.")).size(), 0);
    }

    void storeWithoutProfilesListIsCorrupt() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        QVERIFY(writeText(path, R"({"schemaVersion": 1, "hello": "world"})"));
        const ProfileStore::LoadResult loaded = ProfileStore(path).load();
        QVERIFY(loaded.ok);
        QVERIFY(loaded.profiles.empty());
        QVERIFY(loaded.warning.contains(QStringLiteral("profiles")));
        QCOMPARE(filesStartingWith(dir.path(), QStringLiteral("profiles.json.corrupt-")).size(), 1);
    }

    void newerSchemaIsRefusedAndLeftUntouched() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        const QByteArray content = R"({"schemaVersion": 99, "profiles": []})";
        QVERIFY(writeText(path, content));
        const ProfileStore::LoadResult loaded = ProfileStore(path).load();
        QVERIFY(!loaded.ok);
        QVERIFY2(loaded.error.contains(QStringLiteral("newer")), qPrintable(loaded.error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
        QCOMPARE(filesStartingWith(dir.path(), QStringLiteral("profiles.json.")).size(), 0);
    }

    void version3StoreIsRefusedAndLeftUntouched() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        const QByteArray content = R"({"schemaVersion": 3, "profiles": [{"name": "Future", "check": {"host": "h"}}]})";
        QVERIFY(writeText(path, content));
        const ProfileStore store(path);
        const ProfileStore::LoadResult loaded = store.load();
        QVERIFY(!loaded.ok);
        QVERIFY2(loaded.error.contains(QStringLiteral("newer")), qPrintable(loaded.error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
        QCOMPARE(filesStartingWith(dir.path(), QStringLiteral("profiles.")).size(), 1);  // no backup, no copy
    }

    void saveNeverReplacesAFileLoadRefused() {
        // Review finding A: what load() refuses and leaves in place, save() must
        // not replace either, whoever calls it.
        QTemporaryDir dir;
        struct Case {
            QString file;
            QByteArray content;
            QString reason;
        };
        const std::vector<Case> cases = {
            {QStringLiteral("newer.json"), R"({"schemaVersion": 3, "profiles": []})", QStringLiteral("newer WakeDeck")},
            {QStringLiteral("single.json"), R"({"schemaVersion": 2, "name": "X", "check": {"host": "h"}})",
             QStringLiteral("single profile")},
        };
        for (const Case& c : cases) {
            const QString path = dir.filePath(c.file);
            QVERIFY(writeText(path, c.content));
            const ProfileStore store(path);
            QVERIFY(!store.load().ok);
            QString error;
            QVERIFY(!store.save({fullProfile()}, &error));
            QVERIFY2(error.contains(c.reason), qPrintable(error));
            QVERIFY2(error.contains(QStringLiteral("left untouched")), qPrintable(error));
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), c.content);
            QVERIFY(!QFile::exists(store.v1BackupPath()));
        }
        // A file that cannot be read is not replaced either.
        const QString locked = dir.filePath(QStringLiteral("locked.json"));
        QVERIFY(writeText(locked, R"({"schemaVersion": 2, "profiles": []})"));
        QVERIFY(QFile::setPermissions(locked, QFileDevice::WriteOwner));
        QString error;
        const bool saved = ProfileStore(locked).save({fullProfile()}, &error);
        QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QVERIFY(!saved);
        QVERIFY2(error.contains(QStringLiteral("was not replaced")), qPrintable(error));
        // A current store is still saved as usual.
        QVERIFY(ProfileStore(locked).save({fullProfile()}, &error));
    }

    void version1StoreLoadsWithDefaults() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        QVERIFY(writeText(path, R"({"schemaVersion": 1, "profiles": [
            {"schemaVersion": 1, "name": "Old PC", "check": {"host": "old-pc.example.net"},
             "wake": {"method": "magicPacket", "mac": "00:00:5E:00:53:2A", "destination": "192.168.1.255", "ports": [9, 7], "repeat": 5, "intervalMs": 100}}
        ]})"));
        const ProfileStore::LoadResult loaded = ProfileStore(path).load();
        QVERIFY2(loaded.ok, qPrintable(loaded.error));
        QVERIFY2(loaded.warning.isEmpty(), qPrintable(loaded.warning));
        QCOMPARE(loaded.profiles.size(), std::size_t(1));
        QCOMPARE(loaded.profiles[0].wake->destinationMode, WakeDestination::Address);
        QCOMPARE(loaded.profiles[0].wake->target.destination, std::string("192.168.1.255"));
        QCOMPARE(loaded.profiles[0].wake->resendSeconds, 15);
        QVERIFY(!QFile::exists(ProfileStore(path).v1BackupPath()));  // reading alone never writes
    }

    void version2RoundTripThroughTheStore() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        const ProfileStore store(path);
        Profile local = fullProfile();
        local.name = QStringLiteral("Local PC");
        local.wake->destinationMode = WakeDestination::LocalNetworks;
        local.wake->target.destination.clear();
        local.wake->resendSeconds = 30;
        QString error;
        QVERIFY2(store.save({fullProfile(), local}, &error), qPrintable(error));
        const ProfileStore::LoadResult loaded = store.load();
        QVERIFY2(loaded.ok, qPrintable(loaded.error));
        QCOMPARE(loaded.profiles.size(), std::size_t(2));
        QCOMPARE(toJson(loaded.profiles[0]), toJson(fullProfile()));
        QCOMPARE(toJson(loaded.profiles[1]), toJson(local));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 2);
        for (const QJsonValue& entry : root.value(QStringLiteral("profiles")).toArray()) {
            QCOMPARE(entry.toObject().value(QStringLiteral("schemaVersion")).toInt(), 2);
        }
        QVERIFY(!QFile::exists(store.v1BackupPath()));  // a new store needs no backup
    }

    void version1StoreIsBackedUpOnceAndNeverOverwritten() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        const QByteArray original = R"({"schemaVersion": 1, "profiles": [{"name": "Old PC", "check": {"host": "old-pc.example.net"}}]})";
        QVERIFY(writeText(path, original));
        const ProfileStore store(path);
        QCOMPARE(store.v1BackupPath(), dir.filePath(QStringLiteral("profiles.v1-backup.json")));

        const ProfileStore::LoadResult loaded = store.load();
        QVERIFY(loaded.ok);
        QString error;
        QVERIFY2(store.save(loaded.profiles, &error), qPrintable(error));
        QFile backup(store.v1BackupPath());
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), original);  // the version 1 file, byte for byte
        backup.close();

        // Later saves replace a version 2 file: the backup stays as it was.
        std::vector<Profile> more = loaded.profiles;
        more.push_back(fullProfile());
        QVERIFY2(store.save(more, &error), qPrintable(error));
        // Even another version 1 file in its place never overwrites the first backup.
        QVERIFY(writeText(path, R"({"schemaVersion": 1, "profiles": []})"));
        QVERIFY2(store.save(more, &error), qPrintable(error));
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), original);
        QCOMPARE(filesStartingWith(dir.path(), QStringLiteral("profiles.")).size(), 2);  // the store and one backup
    }

    void storeWithoutSchemaVersionCountsAsVersion1() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        QVERIFY(writeText(path, R"({"profiles": []})"));
        const ProfileStore store(path);
        QString error;
        QVERIFY2(store.save({fullProfile()}, &error), qPrintable(error));
        QVERIFY(QFile::exists(store.v1BackupPath()));
    }

    void invalidEntriesAreSkippedWithACopyKept() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        QVERIFY(writeText(path, R"({"schemaVersion": 1, "profiles": [
            {"name": "Good", "check": {"host": "good.example.net"}},
            {"name": "Broken", "check": {"host": "broken.example.net", "port": 0}},
            "not even an object"
        ]})"));
        const ProfileStore::LoadResult loaded = ProfileStore(path).load();
        QVERIFY2(loaded.ok, qPrintable(loaded.error));
        QCOMPARE(loaded.profiles.size(), std::size_t(1));
        QCOMPARE(loaded.profiles.front().name, QStringLiteral("Good"));
        QVERIFY2(loaded.warning.contains(QStringLiteral("Skipped 2")), qPrintable(loaded.warning));
        QVERIFY(loaded.warning.contains(QStringLiteral("Broken")));
        QVERIFY(loaded.warning.contains(QStringLiteral("is kept as")));
        QVERIFY(QFile::exists(path));
        QCOMPARE(filesStartingWith(dir.path(), QStringLiteral("profiles.json.invalid-")).size(), 1);

        // Reading the same file again keeps the one copy instead of adding another.
        const ProfileStore::LoadResult again = ProfileStore(path).load();
        QVERIFY(again.ok);
        QCOMPARE(filesStartingWith(dir.path(), QStringLiteral("profiles.json.invalid-")).size(), 1);
    }

    void importAndExportRoundTrip() {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("exported.json"));
        QString error;
        QVERIFY2(ProfileStore::writeProfileFile(fullProfile(), file, &error), qPrintable(error));
        const auto imported = ProfileStore::readProfileFile(file, &error);
        QVERIFY2(imported.has_value(), qPrintable(error));
        QCOMPARE(toJson(*imported), toJson(fullProfile()));
    }

    void importRejectsWholeStoreFiles() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("profiles.json"));
        QVERIFY(ProfileStore(path).save({fullProfile()}, nullptr));
        QString error;
        QVERIFY(!ProfileStore::readProfileFile(path, &error).has_value());
        QVERIFY2(error.contains(QStringLiteral("--store")), qPrintable(error));
        QVERIFY(!ProfileStore::readProfileFile(dir.filePath(QStringLiteral("missing.json")), &error).has_value());
        QVERIFY(error.contains(QStringLiteral("cannot read")));
    }

    void defaultPathIsInApplicationSupport() {
        const QString path = ProfileStore::defaultPath();
        QVERIFY2(path.endsWith(QStringLiteral("/WakeDeck/profiles.json")), qPrintable(path));
        QVERIFY2(path.contains(QStringLiteral("Application Support")), qPrintable(path));
    }
    // ---- Learned timing: the history beside the store ----

    void historyMedian() {
        QCOMPARE(medianMs({}), qint64(0));
        QCOMPARE(medianMs({30000}), qint64(30000));
        QCOMPARE(medianMs({40000, 10000, 30000}), qint64(30000));
        QCOMPARE(medianMs({10000, 40000, 20000, 30000}), qint64(25000));
        QCOMPARE(longerThanUsualMs(10000), qint64(30000));   // typical + 20 s is larger
        QCOMPARE(longerThanUsualMs(60000), qint64(120000));  // twice the typical time is larger
    }

    void historyIsNamedAfterTheStore() {
        QCOMPARE(TimingHistory::pathBeside(QStringLiteral("/x/WakeDeck/profiles.json")), QStringLiteral("/x/WakeDeck/history.json"));
        QCOMPARE(TimingHistory::pathBeside(QStringLiteral("/x/work.json")), QStringLiteral("/x/work.history.json"));
        QCOMPARE(TimingHistory::pathBeside(QStringLiteral("/x/home.profiles.json")), QStringLiteral("/x/home.profiles.history.json"));
        QCOMPARE(TimingHistory::pathBeside(ProfileStore::defaultPath()),
                 QFileInfo(ProfileStore::defaultPath()).dir().filePath(QStringLiteral("history.json")));
    }

    void historyRecordsAndPersists() {
        QTemporaryDir dir;
        const QString path = TimingHistory::pathBeside(dir.filePath(QStringLiteral("profiles.json")));
        const TimingHistory history(path);
        QVERIFY(!history.learned(QStringLiteral("Gaming PC")).has_value());
        QVERIFY(!QFile::exists(path));
        QString error;
        QVERIFY2(history.record(QStringLiteral("Gaming PC"), 26000, QDateTime::currentDateTimeUtc(), &error), qPrintable(error));
        QVERIFY(QFile::exists(path));
        auto learned = TimingHistory(path).learned(QStringLiteral("gaming pc"));  // read back from the file
        QVERIFY(learned.has_value());
        QCOMPARE(learned->entries, 1);
        QCOMPARE(learned->lastMs, qint64(26000));
        QCOMPARE(describeLearnedTiming(*learned), QStringLiteral("Last time: 0:26"));
        QVERIFY(history.record(QStringLiteral("Gaming PC"), 34000));
        QVERIFY(history.record(QStringLiteral("Gaming PC"), 30000));
        learned = history.learned(QStringLiteral("Gaming PC"));
        QCOMPARE(learned->entries, 3);
        QCOMPARE(learned->typicalMs, qint64(30000));
        QCOMPARE(learned->longerAfterMs, qint64(60000));  // twice 0:30
        QCOMPARE(describeLearnedTiming(*learned), QStringLiteral("Usually ready about 0:30 after the wake request (last 3 runs)"));
        // Names and times only.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("computers")).toObject().keys(), QStringList{QStringLiteral("gaming pc")});
        const QJsonObject entry = root.value(QStringLiteral("computers")).toObject().value(QStringLiteral("gaming pc")).toArray().first().toObject();
        QCOMPARE(entry.keys(), (QStringList{QStringLiteral("at"), QStringLiteral("ms")}));
    }

    void historyKeepsTwentyAndTakesTheMedianOfTen() {
        QTemporaryDir dir;
        const TimingHistory history(dir.filePath(QStringLiteral("history.json")));
        for (int i = 1; i <= 25; ++i) QVERIFY(history.record(QStringLiteral("PC"), i * 1000));
        const auto entries = history.entries(QStringLiteral("PC"));
        QCOMPARE(entries.size(), std::size_t(20));
        QCOMPARE(entries.front().ms, qint64(6000));  // the oldest five are gone
        QCOMPARE(entries.back().ms, qint64(25000));
        const auto learned = history.learned(QStringLiteral("PC"));
        QCOMPARE(learned->entries, 20);
        QCOMPARE(learned->medianOf, 10);
        QCOMPARE(learned->typicalMs, qint64(20500));  // median of 16..25 s
        QCOMPARE(describeLearnedTiming(*learned), QStringLiteral("Usually ready about 0:20 after the wake request (last 10 runs)"));
    }

    void historyNormalizesNames() {
        QCOMPARE(TimingHistory::key(QStringLiteral("  Living Room PC ")), QStringLiteral("living room pc"));
        QTemporaryDir dir;
        const TimingHistory history(dir.filePath(QStringLiteral("history.json")));
        QVERIFY(history.record(QStringLiteral("  Living Room PC "), 1000));
        QVERIFY(history.record(QStringLiteral("LIVING ROOM PC"), 3000));
        QCOMPARE(history.entries(QStringLiteral("living room pc")).size(), std::size_t(2));
    }

    void historyUnreadableFileIsEmptyAndReplaced() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("history.json"));
        for (const QByteArray& garbage : {QByteArray("this is not json"), QByteArray("[1, 2]"),
                                          QByteArray("{\"computers\": {\"pc\": [{\"at\": \"never\", \"ms\": 5}, {\"ms\": -1}]}}")}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(garbage);
            file.close();
            const TimingHistory history(path);
            QVERIFY(!history.learned(QStringLiteral("pc")).has_value());
            QVERIFY(history.record(QStringLiteral("PC"), 12000));
            QCOMPARE(history.entries(QStringLiteral("pc")).size(), std::size_t(1));
            QCOMPARE(TimingHistory(path).learned(QStringLiteral("PC"))->lastMs, qint64(12000));
        }
    }

    void historyRenameAndRemove() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("history.json"));
        const TimingHistory history(path);
        QVERIFY(history.record(QStringLiteral("Old Name"), 20000));
        QVERIFY(history.record(QStringLiteral("Other"), 40000));
        QVERIFY(history.rename(QStringLiteral("old name"), QStringLiteral("New Name")));
        QVERIFY(history.entries(QStringLiteral("Old Name")).empty());
        QCOMPARE(history.entries(QStringLiteral("new name")).size(), std::size_t(1));
        // Renaming onto another computer replaces that one's history.
        QVERIFY(history.rename(QStringLiteral("New Name"), QStringLiteral("Other")));
        QCOMPARE(history.learned(QStringLiteral("Other"))->lastMs, qint64(20000));
        QVERIFY(history.entries(QStringLiteral("New Name")).empty());
        QVERIFY(history.rename(QStringLiteral("Other"), QStringLiteral("OTHER")));  // same key: nothing moves
        QCOMPARE(history.entries(QStringLiteral("other")).size(), std::size_t(1));
        QVERIFY(history.remove(QStringLiteral("other")));
        QVERIFY(!history.learned(QStringLiteral("Other")).has_value());
        QVERIFY(history.remove(QStringLiteral("Nobody")));
        // Removing from a history that does not exist creates no file.
        const QString absent = dir.filePath(QStringLiteral("work.history.json"));
        QVERIFY(TimingHistory(absent).remove(QStringLiteral("PC")));
        QVERIFY(!QFile::exists(absent));
    }
};

QTEST_GUILESS_MAIN(ProfilesTest)
#include "test_profiles.moc"
