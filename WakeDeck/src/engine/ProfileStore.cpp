#include "ProfileStore.h"

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
#include <utility>

namespace wakedeck {
namespace {

QString timestamp() {
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
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
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    QString corruptReason;
    if (document.isNull()) {
        corruptReason = QStringLiteral("is not valid JSON (%1)").arg(parseError.errorString());
    } else if (!document.isObject()) {
        corruptReason = QStringLiteral("does not contain a JSON object");
    } else {
        const int version = document.object().value(QLatin1String("schemaVersion")).toInt(1);
        if (version > Profile::kSchemaVersion) {
            result.error = QStringLiteral("%1 was written by a newer WakeDeck (schema %2). Update WakeDeck; "
                                          "the file was left untouched.")
                               .arg(path_)
                               .arg(version);
            return result;
        }
        if (!document.object().value(QLatin1String("profiles")).isArray()) {
            corruptReason = QStringLiteral("has no \"profiles\" list");
        }
    }

    if (!corruptReason.isEmpty()) {
        // Keep the unreadable file for the user to inspect; start with an empty list.
        const QString aside = path_ + QStringLiteral(".corrupt-") + timestamp();
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
        const QString copy = path_ + QStringLiteral(".invalid-") + timestamp();
        QFile::copy(path_, copy);
        result.warning = QStringLiteral("Skipped %1 unreadable profile(s) in %2: %3. A copy of the original file was "
                                        "kept as %4; saving will drop the skipped entries.")
                             .arg(skipped.size())
                             .arg(path_, skipped.join(QStringLiteral("; ")), copy);
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
