#pragma once
#include "RuntimeAnimationData.h"
#include <QWidget>
#include <functional>
class IAnimationEditor;class IAssetsManager;class ILibraryManager;class ISelectionManager;
namespace Ui {class AvatarMaskInspectorForm;}
class AvatarMaskInspector final : public QWidget {
public:
    explicit AvatarMaskInspector(QWidget* parent=nullptr);
    ~AvatarMaskInspector() override;
    void Load(IAnimationEditor*,IAssetsManager*,ILibraryManager*,ISelectionManager*,const GameSproutRuntime::ResourceReference&);
    std::function<void()> Edit;
private:
    Ui::AvatarMaskInspectorForm* _Ui=nullptr;
};
