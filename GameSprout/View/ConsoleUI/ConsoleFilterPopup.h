#ifndef __ConsoleFilterPopup_h__
#define __ConsoleFilterPopup_h__

#include "IConsoleUI.h"
#include <QWidget>

class QCheckBox;
namespace Ui { class ConsoleFilterPopup; }

class ConsoleFilterPopup : public QWidget {
public:
    explicit ConsoleFilterPopup(QWidget* parent);
    ~ConsoleFilterPopup() override;

    QCheckBox* LevelCheckBox(eConsoleLog level) const;
    void ShowBelow(QWidget* anchor);

private:
    ConsoleFilterPopup(const ConsoleFilterPopup&) = delete;
    ConsoleFilterPopup& operator=(const ConsoleFilterPopup&) = delete;
    Ui::ConsoleFilterPopup* _Ui = nullptr;
};

#endif // __ConsoleFilterPopup_h__
