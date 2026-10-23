#include "HubWindow.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    HubWindow w;
    w.show();
    return QApplication::exec();
}
