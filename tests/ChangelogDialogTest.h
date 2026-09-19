#pragma once

#include <QObject>

// Regression tests for the in-app changelog (Help > Changelog).
//
// The bundled CHANGELOG.md is the only place release history lives, and both
// its section format and the dialog's markdown pass are load-bearing: a format
// slip (or an empty Unreleased section) must fail here instead of shipping a
// blank or literal-markdown pane. The newest entry also has to match
// AppVersion::kNumber so the file and the version constant cannot drift apart.
class ChangelogDialogTest : public QObject {
    Q_OBJECT

private slots:
    void bundledChangelogCoversEveryRelease();
    void releaseNotesRenderAsHtml();
};
