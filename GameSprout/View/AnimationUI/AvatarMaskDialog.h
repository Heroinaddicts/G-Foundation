#pragma once
#include "RuntimeAnimationData.h"
#include <QDialog>
#include <vector>
namespace GameSproutRuntime { class IRuntimeResourceProvider; }
class IAnimationEditor;
class IAssetsManager;
class ILibraryManager;
class ISelectionManager;
class QTreeWidgetItem;
class QCloseEvent;
namespace Ui {class AvatarMaskDialogForm;}

// View edits a value draft. Module owns source IO, validation and reimport.
class AvatarMaskDialog final : public QDialog {
public:
    AvatarMaskDialog(IAnimationEditor*,GameSproutRuntime::IRuntimeResourceProvider*,IAssetsManager*,ILibraryManager*,ISelectionManager*,
        GameSproutRuntime::ResourceReference,QWidget* parent=nullptr);
    ~AvatarMaskDialog() override;
    const GameSproutRuntime::ResourceReference& Reference() const {return _Reference;}
    void reject() override;
protected:
    void closeEvent(QCloseEvent*) override;
private:
    void Reload();
    void Save();
    void FillTree();
    void ItemChanged(QTreeWidgetItem*,int);
    bool StoreTree();
    bool ConfirmDiscard();
    void Error(const std::string&);
    Ui::AvatarMaskDialogForm* _Ui=nullptr;
    IAnimationEditor* _Resources=nullptr;
    GameSproutRuntime::IRuntimeResourceProvider* _Provider=nullptr;
    GameSproutRuntime::ResourceReference _Reference;
    GameSproutRuntime::AvatarMaskData _Data;
    GameSproutRuntime::SkeletonData _Skeleton;
    std::vector<QTreeWidgetItem*> _Items;
    std::string _Snapshot;
    bool _Filling=false,_Dirty=false;
};
