#include "ConsoleLogDelegate.h"

#include <QBrush>
#include <QStyleOptionViewItem>

ConsoleLogDelegate::ConsoleLogDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void ConsoleLogDelegate::initStyleOption(QStyleOptionViewItem* option,
                                         const QModelIndex& index) const {
    QStyledItemDelegate::initStyleOption(option, index);
    const QVariant foreground = index.data(Qt::ForegroundRole);
    if (foreground.canConvert<QBrush>()) {
        const QColor color = foreground.value<QBrush>().color();
        // Keep severity colors readable even when selected or unfocused.
        option->palette.setColor(QPalette::All, QPalette::Text, color);
        option->palette.setColor(QPalette::All, QPalette::HighlightedText, color);
    }
}
