
#include <QApplication>
#include <QTimer>
#include "GameSproutFinal.h"


int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    GameSprout::GameSproutFinal * gsf = GameSprout::GameSproutFinal::GetInstance();

    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);

    QObject::connect(&timer, &QTimer::timeout, &app, [gsf]() {
        gsf->Update();
    });

    timer.start(16); // 每 16ms 调用一次，约 60 次/秒

    return QApplication::exec();
}
