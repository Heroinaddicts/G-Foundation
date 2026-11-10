#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void SendMessage();

    Ui::MainWindow* ui = nullptr;
    QNetworkAccessManager* _Http = nullptr;

    bool _IsComposing = false;
    bool _IsSending = false;
};

#endif // MAINWINDOW_H