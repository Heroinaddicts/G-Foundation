#ifndef GAMESPROUT_IDE_LOADING_OVERLAY_H
#define GAMESPROUT_IDE_LOADING_OVERLAY_H

#include <QFrame>
#include <QMetaObject>

class QMainWindow;
namespace Ui { class IDELoadingOverlay; }

// Main-thread UI only. The main window owns this child and all form widgets.
class IDELoadingOverlay final : public QFrame {
public:
    explicit IDELoadingOverlay(QMainWindow* window);
    ~IDELoadingOverlay() override;
    IDELoadingOverlay(const IDELoadingOverlay&) = delete;
    IDELoadingOverlay& operator=(const IDELoadingOverlay&) = delete;

    void Begin(const QString& title);
    void SetDescription(const QString& description);
    void SetProgress(int completed, int total);
    void End();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool BelongsToWindow(QObject* object) const;

    QMainWindow* _Window;
    Ui::IDELoadingOverlay* _Ui = nullptr;
    QWidget* _PreviousFocus = nullptr; // Borrowed; destruction clears this pointer.
    QMetaObject::Connection _FocusDestroyed;
    bool _Active = false;
    bool _MenuWasEnabled = false;
};

#endif
