#pragma once
#include "RuntimeAnimationData.h"
#include <QWidget>
#include <functional>
class IAnimationEditor;namespace Ui{class AvatarInspectorForm;}
class AvatarInspector final:public QWidget{
public:
    explicit AvatarInspector(QWidget* parent=nullptr);~AvatarInspector()override;
    void Load(IAnimationEditor*,const GameSproutRuntime::ResourceReference&);
    std::function<void()> Edit;
private:Ui::AvatarInspectorForm* _Ui=nullptr;
};
