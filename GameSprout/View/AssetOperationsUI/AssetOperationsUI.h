#ifndef __AssetOperationsUI_h__
#define __AssetOperationsUI_h__

#include "Header.h"
#include "IAssetOperations.h"
#include <QPointer>
class IProjectAssetsUI;
struct ProjectAssetActionContext;
class QObject;
class QMenu;
class QDialog;

QT_BEGIN_NAMESPACE
namespace Ui {
class AssetOperationsActionsForm;
}
QT_END_NAMESPACE

class AssetOperationsUI : public IAssetOperationsUI {
public:
    ~AssetOperationsUI() override;

    void Show() override;
    void Hide() override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
    static void DeleteMenu();
    bool CanDelete(const ProjectAssetActionContext&) const;
    void ConfirmDelete();
    IProjectAssetsUI* _Browser=nullptr;
    IAssetOperations* _Operations=nullptr;
    QObject* _Dispatch=nullptr;
    QMenu* _Actions=nullptr;
    Ui::AssetOperationsActionsForm* _Ui=nullptr;
    QPointer<QDialog> _Dialog;
};
#endif //__AssetOperationsUI_h__
