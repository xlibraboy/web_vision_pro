#include "ChangelogDialogTest.h"

#include "config/AppVersion.h"
#include "gui/widgets/ChangelogDialog.h"

#include <QListWidget>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QtTest>

namespace {

// The row whose label names the given version, or -1.
int rowForVersion(QListWidget* versions, const QString& version) {
    for (int row = 0; row < versions->count(); ++row) {
        if (versions->item(row)->text().contains(version)) {
            return row;
        }
    }
    return -1;
}

}  // namespace

// One row per release, no empty rows (the still-empty Unreleased section is
// dropped), and the version AppVersion reports has to be released in the file.
void ChangelogDialogTest::bundledChangelogCoversEveryRelease()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    ChangelogDialog dialog;
    auto* versions = dialog.findChild<QListWidget*>("changelogVersionList");
    QVERIFY(versions != nullptr);
    QVERIFY(versions->count() >= 2);

    for (int row = 0; row < versions->count(); ++row) {
        QVERIFY2(!versions->item(row)->data(Qt::UserRole).toString().trimmed().isEmpty(),
                 qPrintable(QString("empty release section: %1").arg(versions->item(row)->text())));
    }
    QVERIFY2(rowForVersion(versions, QString::fromLatin1(AppVersion::kNumber)) >= 0,
             "CHANGELOG.md has no section for the version in AppVersion.h");
}

// Release notes render as the themed HTML the browser expects (### group
// headings, - bullets), never as literal markdown lines.
void ChangelogDialogTest::releaseNotesRenderAsHtml()
{
    ChangelogDialog dialog;
    auto* versions = dialog.findChild<QListWidget*>("changelogVersionList");
    auto* browser = dialog.findChild<QTextBrowser*>();
    QVERIFY(versions != nullptr);
    QVERIFY(browser != nullptr);

    const int row = rowForVersion(versions, QString::fromLatin1(AppVersion::kNumber));
    QVERIFY(row >= 0);
    versions->setCurrentRow(row);  // loads the section into the browser

    const QString html = versions->item(row)->data(Qt::UserRole).toString();
    QVERIFY(html.contains("<h3>"));
    QVERIFY(html.contains("<li>"));
    QVERIFY(!html.contains("###"));
    QVERIFY(!browser->toPlainText().trimmed().isEmpty());
}
