#pragma once
#include <QWidget>
#include "IDEDockerDragController.h"

namespace Ui {class IDEDockerDropOverlayForm;}

class IDEDockerDropOverlay final : public QWidget {
public:
    explicit IDEDockerDropOverlay(QWidget* owner);
    ~IDEDockerDropOverlay() override;
    void ShowPreview(const QRect& globalRectangle,DockerDropPosition position,bool root,bool reordering=false);
private:
    Ui::IDEDockerDropOverlayForm* _Ui;
};
