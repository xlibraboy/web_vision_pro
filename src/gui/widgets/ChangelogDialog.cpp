#include "ChangelogDialog.h"

#include <QColor>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QRegularExpression>
#include <QSplitter>
#include <QStringList>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "../../config/AppVersion.h"
#include "../../config/CameraConfig.h"

namespace {

// Escapes the characters that must not reach the browser as markup. Runs
// before the inline markdown is translated so the two never interfere.
QString escapeHtml(const QString& text) {
    QString escaped = text;
    escaped.replace('&', "&amp;");
    escaped.replace('<', "&lt;");
    escaped.replace('>', "&gt;");
    return escaped;
}

// The inline markdown the changelog uses: **bold**, `code`, [text](url).
QString inlineHtml(const QString& line) {
    static const QRegularExpression boldPattern("\\*\\*([^*]+)\\*\\*");
    static const QRegularExpression codePattern("`([^`]+)`");
    static const QRegularExpression linkPattern("\\[([^\\]]+)\\]\\(([^)]+)\\)");

    QString html = escapeHtml(line);
    html.replace(codePattern, "<code>\\1</code>");
    html.replace(boldPattern, "<b>\\1</b>");
    html.replace(linkPattern, "<a href=\"\\2\">\\1</a>");
    return html;
}

}  // namespace

ChangelogDialog::ChangelogDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("PaperVision Changelog");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(760, 600);
    setMinimumSize(560, 400);

    const ThemeColors tc = CameraConfig::getThemeColors();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* header = new QLabel(
        QString("<b>PaperVision</b> v%1 \u2014 release history").arg(AppVersion::kNumber), this);
    header->setStyleSheet(QString("color: %1; font-size: 14px;").arg(tc.text));
    layout->addWidget(header);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(splitter, 1);

    versionList_ = new QListWidget(splitter);
    versionList_->setObjectName("changelogVersionList");
    versionList_->setFixedWidth(190);
    versionList_->setSelectionMode(QAbstractItemView::SingleSelection);

    contentBrowser_ = new QTextBrowser(splitter);
    contentBrowser_->setOpenExternalLinks(true);
    contentBrowser_->setOpenLinks(true);
    contentBrowser_->document()->setDefaultStyleSheet(QString(
        "h2 { color: %1; border-bottom: 1px solid %2; padding-bottom: 4px; font-size: 17px; }"
        "h3 { color: %1; font-size: 14px; margin-top: 14px; }"
        "p  { color: %3; font-size: 13px; }"
        "li { color: %3; font-size: 13px; margin-bottom: 3px; }"
        "code { color: %1; background-color: %4; padding: 1px 4px; border-radius: 3px; }"
        "b  { color: %3; }"
        "a  { color: %1; }"
    ).arg(tc.primary, tc.border, tc.text, tc.btnBg));
    contentBrowser_->setStyleSheet(QString(
        "QTextBrowser { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 4px; padding: 10px; }").arg(tc.bg, tc.text, tc.border));
    contentBrowser_->setFrameShape(QFrame::NoFrame);

    // Soft selection: accent at ~40% opacity over the dark surface, matching
    // the documentation dialog.
    QColor selectionTint = QColor(tc.primary);
    selectionTint.setAlpha(102);
    const QString selectionTintCss = QString("rgba(%1, %2, %3, %4)")
        .arg(selectionTint.red()).arg(selectionTint.green())
        .arg(selectionTint.blue()).arg(selectionTint.alpha());

    splitter->addWidget(versionList_);
    splitter->addWidget(contentBrowser_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({190, 570});

    setStyleSheet(QString(
        "QDialog { background-color: %1; }"
        "QLabel { color: %2; }"
        "QListWidget#changelogVersionList { background-color: %4; color: %2; border: 1px solid %3;"
        " border-radius: 4px; font-size: 13px; padding: 4px; }"
        "QListWidget#changelogVersionList::item { padding: 7px 8px; border-radius: 3px; }"
        "QListWidget#changelogVersionList::item:selected { background-color: %5; color: %6; }"
        "QListWidget#changelogVersionList::item:hover:!selected { background-color: %7; }"
    ).arg(tc.bg, tc.text, tc.border, tc.btnBg, selectionTintCss,
          tc.text,
          tc.btnHover));

    connect(versionList_, &QListWidget::currentRowChanged,
            this, [this](int row) {
        if (row >= 0 && row < versionList_->count()) {
            contentBrowser_->setHtml(versionList_->item(row)->data(Qt::UserRole).toString());
        }
    });

    // Release history comes from the bundled CHANGELOG.md, so the app and the
    // repository can never drift apart.
    QFile changelog(":/CHANGELOG.md");
    if (!changelog.open(QIODevice::ReadOnly | QIODevice::Text)) {
        contentBrowser_->setHtml(QStringLiteral(
            "<p>The changelog could not be loaded from the application resources.</p>"));
        return;
    }

    const QList<VersionSection> sections = parseChangelog(QString::fromUtf8(changelog.readAll()));
    for (const VersionSection& section : sections) {
        auto* item = new QListWidgetItem(section.title);
        item->setData(Qt::UserRole, section.html);
        versionList_->addItem(item);
    }

    if (versionList_->count() > 0) {
        versionList_->setCurrentRow(0);  // newest release, as listed in the file
    } else {
        contentBrowser_->setHtml(QStringLiteral("<p>No release notes yet.</p>"));
    }
}

QList<ChangelogDialog::VersionSection> ChangelogDialog::parseChangelog(const QString& markdown) {
    QList<VersionSection> sections;
    QString title;
    QStringList body;

    const auto flushSection = [&sections, &title, &body]() {
        if (title.isEmpty()) {
            return;
        }
        const QString html = markdownToHtml(body.join('\n'));
        const bool emptyUnreleased =
            title.compare(QStringLiteral("Unreleased"), Qt::CaseInsensitive) == 0
            && html.trimmed().isEmpty();
        if (!emptyUnreleased) {
            sections.append(VersionSection{title, html});
        }
    };

    const QStringList lines = markdown.split('\n');
    for (const QString& line : lines) {
        if (line.startsWith("## ")) {
            flushSection();
            title = line.mid(3).trimmed();
            title.remove('[');
            title.remove(']');
            body.clear();
        } else if (!title.isEmpty()) {
            body.append(line);
        }
    }
    flushSection();
    return sections;
}

QString ChangelogDialog::markdownToHtml(const QString& markdown) {
    QString html;
    bool inList = false;

    const auto closeList = [&html, &inList]() {
        if (inList) {
            html += "</ul>";
            inList = false;
        }
    };

    const QStringList lines = markdown.split('\n');
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            closeList();
        } else if (line.startsWith("### ")) {
            closeList();
            html += QString("<h3>%1</h3>").arg(inlineHtml(line.mid(4)));
        } else if (line.startsWith("- ")) {
            if (!inList) {
                html += "<ul>";
                inList = true;
            }
            html += QString("<li>%1</li>").arg(inlineHtml(line.mid(2)));
        } else {
            closeList();
            html += QString("<p>%1</p>").arg(inlineHtml(line));
        }
    }
    closeList();
    return html;
}
