#include <QApplication>
#include <QTimer>
#include <cstdlib>
#include "GameSproutFinal.h"


int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    GameSproutEditor::GameSproutFinal * gsf = GameSproutEditor::GameSproutFinal::GetInstance();
    if (!gsf) {
        return EXIT_FAILURE;
    }

    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);

    QObject::connect(&timer, &QTimer::timeout, &app, [gsf]() {
        gsf->Update();
    });

    timer.start(16); // 每 16ms 调用一次，约 60 次/秒

    return QApplication::exec();
}
