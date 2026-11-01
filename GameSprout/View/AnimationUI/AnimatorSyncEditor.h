#pragma once
#include "RuntimeAnimationData.h"
#include <QWidget>
#include <functional>
class IAssetsManager;class ILibraryManager;class ISelectionManager;
namespace Ui{class AnimatorSyncEditorForm;}
class AnimatorSyncEditor final:public QWidget{
public:
    explicit AnimatorSyncEditor(QWidget* parent=nullptr);~AnimatorSyncEditor()override;
    void Bind(IAssetsManager*,ILibraryManager*,ISelectionManager*);
    void Load(const GameSproutRuntime::AnimatorControllerData&,int layer);
    bool Read(GameSproutRuntime::AnimatorLayer&,std::string&);
    void RenameParameter(const std::string&,const std::string&);
    std::function<void()> Changed;
private:
    bool Store(std::string&);void Select(int);void Fill();void SetEnabled();
    Ui::AnimatorSyncEditorForm* _Ui=nullptr;
    std::vector<GameSproutRuntime::AnimatorState> _States;
    std::vector<GameSproutRuntime::AnimatorMotionOverride> _Overrides;
    int _State=-1;bool _Loading=false;
};
