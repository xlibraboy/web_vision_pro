#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QListWidget;
class QTextBrowser;

/**
 * In-app changelog browser: one row per release on the left, that release's
 * notes on the right. Content comes from the bundled CHANGELOG.md (the single
 * source of truth for the version history), opened from the Help menu and
 * styled to match the current theme preset like the documentation dialog.
 */
class ChangelogDialog : public QDialog {
    Q_OBJECT

public:
    explicit ChangelogDialog(QWidget* parent = nullptr);

    // Re-applies every preset-derived color (header, browser document, list).
    void updateTheme();

private:
    struct VersionSection {
        QString title;
        QString html;
    };

    // Splits the markdown on "## " headings into release sections; an empty
    // Unreleased section is dropped so it never shows as an empty row.
    static QList<VersionSection> parseChangelog(const QString& markdown);
    // Translates the small markdown subset the changelog uses into the HTML the
    // browser is themed for: "### ", "- ", **bold**, `code`, [text](url).
    static QString markdownToHtml(const QString& markdown);

    QLabel* headerLabel_ = nullptr;
    QListWidget* versionList_ = nullptr;
    QTextBrowser* contentBrowser_ = nullptr;
};
