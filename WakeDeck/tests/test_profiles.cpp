// Qt Test suite for profiles: the JSON form and its validation, the store with
// its schema version and recovery paths, import and export. Everything happens
// in a temporary folder; the real profiles file is never touched.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <chrono>

#include "engine/Profile.h"
#include "engine/ProfileStore.h"

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
        QTest::newRow("newer schema") << QByteArray(R"({"schemaVersion": 2, "name": "X", "check": {"host": "h"}})") << QStringLiteral("newer");
        QTest::newRow("schema zero") << QByteArray(R"({"schemaVersion": 0, "name": "X", "check": {"host": "h"}})") << QStringLiteral("schemaVersion");
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
        QVERIFY(QFile::exists(path));
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
};

QTEST_GUILESS_MAIN(ProfilesTest)
#include "test_profiles.moc"
