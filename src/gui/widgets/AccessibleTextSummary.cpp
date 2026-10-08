#include "AccessibleTextSummary.h"

AccessibleTextSummary::AccessibleTextSummary(QWidget* widget, QAccessible::Role role)
    : QAccessibleWidget(widget, role) {
}

QString AccessibleTextSummary::summary() const {
    QWidget* w = widget();
    if (!w) {
        return QString();
    }
    const auto* provider = dynamic_cast<const AccessibleSummaryProvider*>(w);
    return provider ? provider->accessibleSummary() : QString();
}

QString AccessibleTextSummary::text(QAccessible::Text t) const {
    if (t == QAccessible::Description || t == QAccessible::Help) {
        return summary();
    }
    return QAccessibleWidget::text(t);
}

void* AccessibleTextSummary::interface_cast(QAccessible::InterfaceType type) {
    if (type == QAccessible::TextInterface) {
        return static_cast<QAccessibleTextInterface*>(this);
    }
    return QAccessibleWidget::interface_cast(type);
}

void AccessibleTextSummary::selection(int, int* startOffset, int* endOffset) const {
    *startOffset = -1;
    *endOffset = -1;
}

int AccessibleTextSummary::selectionCount() const {
    return 0;
}

void AccessibleTextSummary::addSelection(int, int) {
}

void AccessibleTextSummary::removeSelection(int) {
}

void AccessibleTextSummary::setSelection(int, int, int) {
}

int AccessibleTextSummary::cursorPosition() const {
    return 0;
}

void AccessibleTextSummary::setCursorPosition(int) {
}

QString AccessibleTextSummary::text(int startOffset, int endOffset) const {
    const QString all = summary();
    if (endOffset < 0 || endOffset > all.size()) {
        endOffset = all.size();
    }
    if (startOffset < 0 || startOffset >= endOffset) {
        return QString();
    }
    return all.mid(startOffset, endOffset - startOffset);
}

int AccessibleTextSummary::characterCount() const {
    return summary().size();
}

QRect AccessibleTextSummary::characterRect(int) const {
    return QRect();
}

int AccessibleTextSummary::offsetAtPoint(const QPoint&) const {
    return -1;
}

void AccessibleTextSummary::scrollToSubstring(int, int) {
}

QString AccessibleTextSummary::attributes(int, int* startOffset, int* endOffset) const {
    *startOffset = 0;
    *endOffset = characterCount();
    return QString();
}

QAccessibleInterface* accessibleTextSummaryFactory(const QString&, QObject* object) {
    QWidget* widget = qobject_cast<QWidget*>(object);
    if (!widget) {
        return nullptr;
    }
    const auto* provider = dynamic_cast<const AccessibleSummaryProvider*>(widget);
    if (!provider) {
        return nullptr;
    }
    return new AccessibleTextSummary(widget, provider->accessibleSummaryRole());
}
