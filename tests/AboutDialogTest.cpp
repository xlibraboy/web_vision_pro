#include "AboutDialogTest.h"

#include "config/AppVersion.h"
#include "gui/MainWindow.h"

#include <pylon/PylonVersionInfo.h>

#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QtTest>

// The About body must name the app version and the pylon runtime in use; the
// runtime string is read from the linked SDK, so a hard-coded version fails.
void AboutDialogTest::aboutTextReportsTheRunningPylonVersion()
{
    const QString runtime = QString::fromLatin1(Pylon::VersionInfo::getVersionString());
    const QString text = MainWindow::aboutText();

    QVERIFY2(!runtime.isEmpty(), "the linked pylon runtime must report a version");
    QVERIFY2(text.contains(runtime), qPrintable(text));
    QVERIFY2(text.contains(QString::fromLatin1(AppVersion::kNumber)), qPrintable(text));
}

// The rendered dialog carries the version line in its text label; the saved
// preview image is the visual artifact for review.
void AboutDialogTest::aboutDialogRendersTheVersionLine()
{
    QMessageBox dialog(QMessageBox::Information,
                       QStringLiteral("About Web Vision Pro"),
                       MainWindow::aboutText());
    dialog.show();
    QCoreApplication::processEvents();

    auto* label = dialog.findChild<QLabel*>(QStringLiteral("qt_msgbox_label"));
    QVERIFY2(label, "the message box must carry its text label");
    QVERIFY2(label->text().contains(QString::fromLatin1(Pylon::VersionInfo::getVersionString())),
             qPrintable(label->text()));

    const QPixmap preview = dialog.grab();
    dialog.close();

    QVERIFY2(!preview.isNull() && preview.width() > 0, "the About dialog must render");
    const QString previewPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("about-dialog-preview.png"));
    QVERIFY2(preview.save(previewPath), qPrintable(previewPath));
}
