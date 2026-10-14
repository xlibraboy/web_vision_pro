#include "AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QtGlobal>

namespace {
const char* const kContainerDataDir = "/app/data";
const char* const kLegacyParamsDir = "/etc/papervision/cameras";

QString envDir(const char* name) {
    // Empty values count as unset, matching the XDG specification.
    return qEnvironmentVariable(name).trimmed();
}

bool inContainer() {
    return QFileInfo(kContainerDataDir).isDir();
}

QString xdgBase(const char* envName, const QString& homeSuffix) {
    const QString configured = envDir(envName);
    return configured.isEmpty() ? QDir::homePath() + homeSuffix : configured;
}
}

QString AppPaths::configBaseDir() {
    const QString override = envDir("PAPERVISION_CONFIG_DIR");
    if (!override.isEmpty()) {
        return override;
    }
    if (inContainer()) {
        return QStringLiteral("/app/data/config");
    }
    return xdgBase("XDG_CONFIG_HOME", QStringLiteral("/.config"));
}

QString AppPaths::dataDir() {
    const QString override = envDir("PAPERVISION_DATA_DIR");
    if (!override.isEmpty()) {
        return override;
    }
    if (inContainer()) {
        return QStringLiteral("/app/data");
    }
    return xdgBase("XDG_DATA_HOME", QStringLiteral("/.local/share"))
        + QStringLiteral("/papervision");
}

QString AppPaths::cameraParamsDir() {
    const QString dir = QDir(dataDir()).filePath(QStringLiteral("cameras"));

    // Adopt parameter files saved by an earlier deployment into the old
    // system-wide location (lost on every container recreation).
    if (!QFileInfo(dir).isDir() && QFileInfo(kLegacyParamsDir).isDir()) {
        QDir().mkpath(dir);
        const QFileInfoList legacy = QDir(kLegacyParamsDir).entryInfoList(
            QStringList() << QStringLiteral("*.pfs"), QDir::Files);
        for (const QFileInfo& file : legacy) {
            const QString target = QDir(dir).filePath(file.fileName());
            if (!QFileInfo::exists(target)) {
                QFile::copy(file.absoluteFilePath(), target);
            }
        }
    }

    QDir().mkpath(dir);
    return dir;
}
