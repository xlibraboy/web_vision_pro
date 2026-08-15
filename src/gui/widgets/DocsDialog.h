#pragma once

#include <QDialog>

class QListWidget;
class QTextBrowser;

/**
 * In-app documentation browser. A section list on the left and rich-text
 * content on the right, opened from the Docs menu. Styled to match the current
 * theme preset and kept as a single instance so it can stay open while the
 * operator consults it next to the live views.
 */
class DocsDialog : public QDialog {
    Q_OBJECT

public:
    explicit DocsDialog(QWidget* parent = nullptr);

private:
    void addSection(const QString& title, const QString& html);

    QListWidget* sectionList_ = nullptr;
    QTextBrowser* contentBrowser_ = nullptr;
};
