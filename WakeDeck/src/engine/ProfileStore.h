// ProfileStore: reads and writes the profiles file. One JSON document holds
// every saved computer, with a schema version so later WakeDeck versions can
// convert it. A file this version cannot read is moved aside, never deleted.
#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "Profile.h"

namespace wakedeck {

class ProfileStore {
public:
    struct LoadResult {
        bool ok = false;  // false: `error` says why nothing could be loaded
        std::vector<Profile> profiles;
        QString warning;  // something was recovered or skipped; worth showing
        QString error;
    };

    // ~/Library/Application Support/WakeDeck/profiles.json on macOS.
    static QString defaultPath();

    explicit ProfileStore(QString path = defaultPath());
    const QString& path() const { return path_; }

    LoadResult load() const;
    // Writes atomically: the old file is replaced only once the new one is complete.
    bool save(const std::vector<Profile>& profiles, QString* error) const;

    // One profile in a file of its own, for import and export.
    static std::optional<Profile> readProfileFile(const QString& path, QString* error);
    static bool writeProfileFile(const Profile& profile, const QString& path, QString* error);

private:
    QString path_;
};

}  // namespace wakedeck
