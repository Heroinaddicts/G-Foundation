#pragma once
#include "RuntimeAnimationData.h"
#include <QDialog>
#include <map>
class IRuntimeAssets;
class IAssetsManager;
class ILibraryManager;
class ISelectionManager;
class QCloseEvent;
namespace Ui {class AnimatorControllerDialogForm;}

// View owns the draft and UI only. Source IO/validation/reimport is RuntimeAssets' job.
class AnimatorControllerDialog final : public QDialog {
public:
    AnimatorControllerDialog(IRuntimeAssets*,IAssetsManager*,ILibraryManager*,ISelectionManager*,
        GameSproutRuntime::ResourceReference,QWidget* parent=nullptr);
    ~AnimatorControllerDialog() override;
    const GameSproutRuntime::ResourceReference& Reference() const {return _Reference;}
    void reject() override;
protected:
    void closeEvent(QCloseEvent*) override;
private:
    GameSproutRuntime::AnimatorLayer& Layer();
    void SelectLayer(int);
    void UpdateLayerControls();
    void SetSyncSource(int);
    void RemapSyncIndices(int,int);
    int GraphIndex(int) const;
    std::map<int,GameSproutRuntime::AnimatorLayer> _IndependentDrafts;
    void Reload();
    void Save();
    void Fill();
    void SelectState(int);
    void SelectTransition(int);
    void SelectMotion(int);
    bool StoreStateDetail();
    bool StoreConditions();
    bool Collect(GameSproutRuntime::AnimatorControllerData&,std::string&);
    bool ConfirmDiscard();
    QString ClipName(const GameSproutRuntime::ResourceReference&) const;
    void Error(const std::string&);
    Ui::AnimatorControllerDialogForm* _Ui=nullptr;
    IRuntimeAssets* _Resources=nullptr;
    IAssetsManager* _Assets=nullptr;
    ILibraryManager* _Library=nullptr;
    ISelectionManager* _Selection=nullptr;
    GameSproutRuntime::ResourceReference _Reference;
    GameSproutRuntime::AnimatorControllerData _Data;
    std::string _Snapshot;
    bool _Filling=false;
    bool _Dirty=false;
    int _LayerRow=0;
    int _StateRow=-1,_TransitionRow=-1,_MotionRow=-1;
};
