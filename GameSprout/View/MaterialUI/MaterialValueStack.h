#pragma once
#include <QStackedWidget>

// 隐藏的 Texture 页不应把每个颜色/数字控件撑到贴图编辑器的高度。
class MaterialValueStack final : public QStackedWidget {
public:
    explicit MaterialValueStack(QWidget * parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
};
