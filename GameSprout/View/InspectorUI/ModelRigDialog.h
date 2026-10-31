#pragma once
#include "HumanoidImportSettings.h"
#include "RuntimeAnimationData.h"
#include <QDialog>
class IRuntimeAssets;class QCloseEvent;
namespace Ui {class ModelRigDialogForm;}
class ModelRigDialog final : public QDialog {
public:
    ModelRigDialog(IRuntimeAssets*,GameSproutRuntime::ResourceReference,QWidget* parent=nullptr);
    ~ModelRigDialog() override;
    const GameSproutRuntime::ResourceReference& Reference()const{return _Reference;}
    void reject() override;
protected:void closeEvent(QCloseEvent*)override;
private:
    void Reload();void Save();void Fill();void SelectBone(int);void StoreBone();bool Read(ModelRigImportSettings&,std::string&);
    bool ConfirmDiscard();void Error(const std::string&);
    Ui::ModelRigDialogForm* _Ui=nullptr;IRuntimeAssets* _Resources=nullptr;
    GameSproutRuntime::ResourceReference _Reference;ModelRigImportSettings _Data;
    GameSproutRuntime::SkeletonData _Skeleton;std::string _Snapshot;
    int _Bone=-1;bool _Filling=false,_Dirty=false;
};
