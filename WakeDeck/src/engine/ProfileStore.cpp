#include "ProfileStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <cmath>
#include <utility>

namespace wakedeck {
namespace {

// "<base>-<date>-<time>" that does not exist yet; a counter is added when the
// same second already produced one, since rename never overwrites.
QString unusedPath(const QString& base) {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString candidate = base + stamp;
    for (int n = 2; QFile::exists(candidate); ++n) {
        candidate = base + stamp + QStringLiteral("-%1").arg(n);
    }
    return candidate;
}

bool writeJsonAtomically(const QString& path, const QJsonDocument& document, QString* error) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = QStringLiteral("cannot create the folder %1").arg(info.absolutePath());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = QStringLiteral("cannot finish writing %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

}  // namespace

QString ProfileStore::defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
           QStringLiteral("/WakeDeck/profiles.json");
}

ProfileStore::ProfileStore(QString path) : path_(std::move(path)) {}

ProfileStore::LoadResult ProfileStore::load() const {
    LoadResult result;
    if (!QFile::exists(path_)) {
        result.ok = true;  // nothing saved yet
        return result;
    }

    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("cannot read %1: %2").arg(path_, file.errorString());
        return result;
    }
    const QByteArray data = file.readAll();
    file.close();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);

    QString corruptReason;
    if (document.isNull()) {
        corruptReason = QStringLiteral("is not valid JSON (%1)").arg(parseError.errorString());
    } else if (!document.isObject()) {
        corruptReason = QStringLiteral("does not contain a JSON object");
    } else {
        const QJsonObject root = document.object();
        const QJsonValue versionValue = root.value(QLatin1String("schemaVersion"));
        const double version = versionValue.toDouble(-1);
        if (!versionValue.isUndefined() && (!versionValue.isDouble() || version != std::floor(version) || version < 1)) {
            corruptReason = QStringLiteral("has an invalid schemaVersion");
        } else if (version > Profile::kSchemaVersion) {
            result.error = QStringLiteral("%1 was written by a newer WakeDeck (schema %2). Update WakeDeck; "
                                          "the file was left untouched.")
                               .arg(path_)
                               .arg(static_cast<int>(version));
            return result;
        } else if (!root.value(QLatin1String("profiles")).isArray()) {
            if (root.contains(QLatin1String("name")) && root.contains(QLatin1String("check"))) {
                // Somebody pointed --store at an exported profile: say so, touch nothing.
                result.error = QStringLiteral("%1 is a single profile file, not a profiles store. Import it with "
                                              "\"wakedeck-cli profile import %1\" instead; it was left untouched.")
                                   .arg(path_);
                return result;
            }
            corruptReason = QStringLiteral("has no \"profiles\" list");
        }
    }

    if (!corruptReason.isEmpty()) {
        // Keep the unreadable file for the user to inspect; start with an empty list.
        const QString aside = unusedPath(path_ + QStringLiteral(".corrupt-"));
        if (!QFile::rename(path_, aside)) {
            result.error = QStringLiteral("%1 %2 and could not be moved aside").arg(path_, corruptReason);
            return result;
        }
        result.ok = true;
        result.warning = QStringLiteral("%1 %2. It was moved to %3; starting with no profiles.")
                             .arg(path_, corruptReason, aside);
        return result;
    }

    QStringList skipped;
    for (const QJsonValue& entry : document.object().value(QLatin1String("profiles")).toArray()) {
        if (!entry.isObject()) {
            skipped << QStringLiteral("an entry that is not an object");
            continue;
        }
        QString reason;
        auto profile = profileFromJson(entry.toObject(), &reason);
        if (!profile) {
            skipped << QStringLiteral("\"%1\" (%2)")
                           .arg(entry.toObject().value(QLatin1String("name")).toString(QStringLiteral("unnamed")), reason);
            continue;
        }
        result.profiles.push_back(std::move(*profile));
    }
    if (!skipped.isEmpty()) {
        // One copy per distinct content, named by a hash of it, so repeated reads
        // of the same file do not pile up copies.
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(8));
        const QString copy = path_ + QStringLiteral(".invalid-") + hash;
        const bool kept = QFile::exists(copy) || QFile::copy(path_, copy);
        result.warning = QStringLiteral("Skipped %1 unreadable profile(s) in %2: %3. %4")
                             .arg(skipped.size())
                             .arg(path_, skipped.join(QStringLiteral("; ")),
                                  kept ? QStringLiteral("A copy of the original file is kept as %1; saving will drop the "
                                                        "skipped entries.")
                                             .arg(copy)
                                       : QStringLiteral("A copy of the original file could not be written next to it; "
                                                        "saving would drop the skipped entries."));
    }
    result.ok = true;
    return result;
}

bool ProfileStore::save(const std::vector<Profile>& profiles, QString* error) const {
    QJsonArray array;
    for (const Profile& profile : profiles) array.append(toJson(profile));
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = Profile::kSchemaVersion;
    root[QStringLiteral("profiles")] = array;
    return writeJsonAtomically(path_, QJsonDocument(root), error);
}

std::optional<Profile> ProfileStore::readProfileFile(const QString& path, QString* error) {
    QString ignored;
    QString* reason = error ? error : &ignored;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *reason = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) {
        *reason = QStringLiteral("%1 is not a JSON object (%2)").arg(path, parseError.errorString());
        return std::nullopt;
    }
    if (document.object().contains(QLatin1String("profiles"))) {
        *reason = QStringLiteral("%1 is a whole profiles file, not a single profile; use it with --store instead").arg(path);
        return std::nullopt;
    }
    QString why;
    auto profile = profileFromJson(document.object(), &why);
    if (!profile) {
        *reason = QStringLiteral("%1: %2").arg(path, why);
        return std::nullopt;
    }
    return profile;
}

bool ProfileStore::writeProfileFile(const Profile& profile, const QString& path, QString* error) {
    return writeJsonAtomically(path, QJsonDocument(toJson(profile)), error);
}

}  // namespace wakedeck
