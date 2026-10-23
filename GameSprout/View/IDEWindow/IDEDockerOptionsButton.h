#ifndef GAMESPROUT_IDE_DOCKER_OPTIONS_BUTTON_H
#define GAMESPROUT_IDE_DOCKER_OPTIONS_BUTTON_H

#include <QToolButton>

namespace Ui { class IDEDockerOptionsButton; }

// 所有 Docker 选项按钮共用的 Designer 表单。
class IDEDockerOptionsButton final : public QToolButton {
public:
    explicit IDEDockerOptionsButton(QWidget* parent);
    ~IDEDockerOptionsButton() override;
    IDEDockerOptionsButton(const IDEDockerOptionsButton&) = delete;
    IDEDockerOptionsButton& operator=(const IDEDockerOptionsButton&) = delete;

private:
    Ui::IDEDockerOptionsButton* _Ui = nullptr;
};

#endif
