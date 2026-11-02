#include "AssetOperationsUI.h"
#include <QDialog>
IAssetOperationsUI* CreateDeletionView(GameSproutEditor::GameSproutViewApi* api) {
    auto* view=new AssetOperationsUI();view->BeforeInitialize(api);
    if(!view->Initialize(api)||!view->Launch(api)){delete view;return nullptr;}
    return view;
}
QDialog* DeletionDialog(IAssetOperationsUI* view){return static_cast<AssetOperationsUI*>(view)->_Dialog.data();}
