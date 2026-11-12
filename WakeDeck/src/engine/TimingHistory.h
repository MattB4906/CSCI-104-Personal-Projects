// TimingHistory: how long each computer took from the first wake request to
// the service being ready, so WakeDeck can say what is usual. The file lives
// beside the profiles store and holds computer names and times only. It is
// disposable: an unreadable file counts as empty and is replaced on the next
// record, without asking anyone.
#pragma once

#include <QDateTime>
#include <QString>
#include <map>
#include <optional>
#include <vector>

namespace wakedeck {

// What WakeDeck has learned about one computer.
struct LearnedTiming {
    int entries = 0;         // runs on record (up to TimingHistory::kMaxEntries)
    int medianOf = 0;        // how many of the latest runs the typical time is the median of
    qint64 typicalMs = 0;    // median of the latest up to kMedianOf runs
    qint64 lastMs = 0;       // the most recent run
    qint64 longerAfterMs = 0;  // "Taking longer than usual" after this long (longerThanUsualMs); 0: never
};

// "Usually ready about 0:30 after the wake request (last 5 runs)", or with
// fewer than three runs "Last time: 0:26".
QString describeLearnedTiming(const LearnedTiming& learned);

// When the wait has gone on longer than this since the wake request, the
// timeline says "Taking longer than usual": the larger of twice the typical
// time and the typical time plus 20 seconds.
qint64 longerThanUsualMs(qint64 typicalMs);

// The median of the values (the mean of the middle two for an even count); 0 when empty.
qint64 medianMs(std::vector<qint64> values);

class TimingHistory {
public:
    static constexpr int kMaxEntries = 20;
    static constexpr int kMedianOf = 10;

    struct Entry {
        QDateTime at;  // when the service became ready (UTC)
        qint64 ms = 0; // first wake request to ready
    };

    // "history.json" beside the default "profiles.json"; "<name>.history.json"
    // beside a store called "<name>.json" (any other name), so two stores in one
    // folder never share a history.
    static QString pathBeside(const QString& storePath);
    // The key a computer is filed under: its name trimmed and in lower case.
    static QString key(const QString& name);

    explicit TimingHistory(QString path);
    const QString& path() const { return path_; }

    // Oldest first; empty when there is nothing on record or the file is unreadable.
    std::vector<Entry> entries(const QString& computer) const;
    std::optional<LearnedTiming> learned(const QString& computer) const;

    // Each writes the whole file atomically; false (with a reason) when it cannot.
    bool record(const QString& computer, qint64 ms, const QDateTime& at = QDateTime::currentDateTimeUtc(),
                QString* error = nullptr) const;
    bool remove(const QString& computer, QString* error = nullptr) const;  // true also when nothing was on record
    // Moves the entries to the new name, replacing what the new name had.
    bool rename(const QString& from, const QString& to, QString* error = nullptr) const;

private:
    using Table = std::map<QString, std::vector<Entry>>;
    Table read() const;
    bool write(const Table& table, QString* error) const;

    QString path_;
};

}  // namespace wakedeck
