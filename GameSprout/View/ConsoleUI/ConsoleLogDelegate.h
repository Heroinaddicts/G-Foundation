#ifndef GAMESPROUT_CONSOLE_LOG_DELEGATE_H
#define GAMESPROUT_CONSOLE_LOG_DELEGATE_H

#include <QStyledItemDelegate>

class ConsoleLogDelegate final : public QStyledItemDelegate {
public:
    explicit ConsoleLogDelegate(QObject* parent = nullptr);

protected:
    void initStyleOption(QStyleOptionViewItem* option,
                         const QModelIndex& index) const override;
};

#endif // GAMESPROUT_CONSOLE_LOG_DELEGATE_H
