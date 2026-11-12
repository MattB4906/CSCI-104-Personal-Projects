#include "TimingHistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <limits>
#include <utility>

#include "Explain.h"

namespace wakedeck {
namespace {

constexpr int kFileVersion = 1;
constexpr qint64 kLongerMarginMs = 20000;

int clampToInt(qint64 ms) {
    return static_cast<int>(std::clamp<qint64>(ms, 0, std::numeric_limits<int>::max()));
}

}  // namespace

QString describeLearnedTiming(const LearnedTiming& learned) {
    if (learned.entries < 3) return QStringLiteral("Last time: %1").arg(formatDuration(clampToInt(learned.lastMs)));
    return QStringLiteral("Usually ready about %1 after the wake request (last %2 runs)")
        .arg(formatDuration(clampToInt(learned.typicalMs)))
        .arg(learned.medianOf);
}

qint64 longerThanUsualMs(qint64 typicalMs) {
    return std::max(2 * typicalMs, typicalMs + kLongerMarginMs);
}

qint64 medianMs(std::vector<qint64> values) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 1) return values[middle];
    return (values[middle - 1] + values[middle]) / 2;
}

QString TimingHistory::pathBeside(const QString& storePath) {
    const QFileInfo info(storePath);
    if (info.fileName() == QLatin1String("profiles.json")) return info.dir().filePath(QStringLiteral("history.json"));
    return info.dir().filePath(info.completeBaseName() + QStringLiteral(".history.json"));
}

QString TimingHistory::key(const QString& name) {
    return name.trimmed().toLower();
}

TimingHistory::TimingHistory(QString path) : path_(std::move(path)) {}

TimingHistory::Table TimingHistory::read() const {
    Table table;
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) return table;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return table;  // unreadable: treated as empty, replaced on the next record
    const QJsonObject computers = document.object().value(QLatin1String("computers")).toObject();
    for (auto it = computers.begin(); it != computers.end(); ++it) {
        std::vector<Entry> entries;
        for (const QJsonValue& value : it.value().toArray()) {
            const QJsonObject object = value.toObject();
            const QJsonValue ms = object.value(QLatin1String("ms"));
            const QDateTime at = QDateTime::fromString(object.value(QLatin1String("at")).toString(), Qt::ISODate);
            if (!ms.isDouble() || ms.toDouble() < 0 || !at.isValid()) continue;
            entries.push_back(Entry{at, static_cast<qint64>(ms.toDouble())});
        }
        if (entries.size() > static_cast<std::size_t>(kMaxEntries)) {
            entries.erase(entries.begin(), entries.end() - kMaxEntries);
        }
        if (!entries.empty()) table[key(it.key())] = std::move(entries);
    }
    return table;
}

bool TimingHistory::write(const Table& table, QString* error) const {
    QJsonObject computers;
    for (const auto& [name, entries] : table) {
        QJsonArray list;
        for (const Entry& entry : entries) {
            QJsonObject object;
            object[QStringLiteral("at")] = entry.at.toUTC().toString(Qt::ISODate);
            object[QStringLiteral("ms")] = static_cast<double>(entry.ms);
            list.append(object);
        }
        computers[name] = list;
    }
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = kFileVersion;
    root[QStringLiteral("computers")] = computers;

    const QFileInfo info(path_);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = QStringLiteral("cannot create the folder %1").arg(info.absolutePath());
        return false;
    }
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path_, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = QStringLiteral("cannot finish writing %1: %2").arg(path_, file.errorString());
        return false;
    }
    return true;
}

std::vector<TimingHistory::Entry> TimingHistory::entries(const QString& computer) const {
    const Table table = read();
    const auto it = table.find(key(computer));
    return it == table.end() ? std::vector<Entry>{} : it->second;
}

std::optional<LearnedTiming> TimingHistory::learned(const QString& computer) const {
    const std::vector<Entry> all = entries(computer);
    if (all.empty()) return std::nullopt;
    LearnedTiming out;
    out.entries = static_cast<int>(all.size());
    out.medianOf = std::min(out.entries, static_cast<int>(kMedianOf));
    std::vector<qint64> latest;
    for (auto it = all.end() - out.medianOf; it != all.end(); ++it) latest.push_back(it->ms);
    out.typicalMs = medianMs(latest);
    out.lastMs = all.back().ms;
    out.longerAfterMs = longerThanUsualMs(out.typicalMs);
    return out;
}

bool TimingHistory::record(const QString& computer, qint64 ms, const QDateTime& at, QString* error) const {
    Table table = read();
    std::vector<Entry>& entries = table[key(computer)];
    entries.push_back(Entry{at.toUTC(), std::max<qint64>(ms, 0)});
    if (entries.size() > static_cast<std::size_t>(kMaxEntries)) {
        entries.erase(entries.begin(), entries.end() - kMaxEntries);
    }
    return write(table, error);
}

bool TimingHistory::remove(const QString& computer, QString* error) const {
    Table table = read();
    if (table.erase(key(computer)) == 0) return true;  // nothing on record: leave the file as it is
    return write(table, error);
}

bool TimingHistory::rename(const QString& from, const QString& to, QString* error) const {
    if (key(from) == key(to)) return true;
    Table table = read();
    const auto old = table.find(key(from));
    const bool hadTarget = table.erase(key(to)) > 0;
    if (old == table.end()) return hadTarget ? write(table, error) : true;
    table[key(to)] = std::move(old->second);
    table.erase(key(from));
    return write(table, error);
}

}  // namespace wakedeck
