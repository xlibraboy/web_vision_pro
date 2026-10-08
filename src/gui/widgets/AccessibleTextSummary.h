#pragma once

#include <QAccessible>
#include <QAccessibleTextInterface>
#include <QAccessibleWidget>
#include <QWidget>

/**
 * Contract for custom-painted widgets whose content exists only as pixels
 * (charts, schematics): the widget returns a plain-text summary of what it
 * draws, and the accessibility interface below exposes that summary to screen
 * readers as read-only text.
 *
 * Implement it alongside QWidget (QWidget first), give the widget an
 * accessibleName(), and install accessibleTextSummaryFactory() once per
 * process. The summary is queried fresh on every access, so it always
 * describes the current data.
 */
class AccessibleSummaryProvider {
public:
    virtual ~AccessibleSummaryProvider() = default;
    virtual QString accessibleSummary() const = 0;
    // Role announced to assistive tech; Chart fits data views, Graphic
    // schematics.
    virtual QAccessible::Role accessibleSummaryRole() const { return QAccessible::Chart; }
};

/**
 * Read-only QAccessibleTextInterface over an AccessibleSummaryProvider.
 *
 * The summary carries no selection, cursor or per-character geometry, and it
 * is also served as the accessible Description so screen readers that read
 * descriptions (rather than text interfaces) still announce the content.
 */
class AccessibleTextSummary : public QAccessibleWidget, public QAccessibleTextInterface {
public:
    explicit AccessibleTextSummary(QWidget* widget, QAccessible::Role role);

    // Capability discovery: textInterface() dispatches through this, and the
    // base reports no text interface even though this class implements one.
    void* interface_cast(QAccessible::InterfaceType type) override;

    QString text(QAccessible::Text t) const override;

    // QAccessibleTextInterface.
    void selection(int selectionIndex, int* startOffset, int* endOffset) const override;
    int selectionCount() const override;
    void addSelection(int startOffset, int endOffset) override;
    void removeSelection(int selectionIndex) override;
    void setSelection(int selectionIndex, int startOffset, int endOffset) override;
    int cursorPosition() const override;
    void setCursorPosition(int position) override;
    QString text(int startOffset, int endOffset) const override;
    int characterCount() const override;
    QRect characterRect(int offset) const override;
    int offsetAtPoint(const QPoint& point) const override;
    void scrollToSubstring(int startIndex, int endIndex) override;
    QString attributes(int offset, int* startOffset, int* endOffset) const override;

private:
    QString summary() const;
};

// QAccessible::installFactory() entry point: creates the summary interface for
// any widget implementing AccessibleSummaryProvider.
QAccessibleInterface* accessibleTextSummaryFactory(const QString& className, QObject* object);
