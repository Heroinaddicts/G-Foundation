#include "ProjectAssetsUI.h"
#include "ProjectAssetsModel.h"
#include <QTreeView>
#include <QApplication>
IProjectAssetsUI* CreateDeletionBrowser(IAssetsManager* assets) {
    auto* browser=new ProjectAssetsUI();browser->_Assets=assets;
    if(!browser->Initialize(nullptr)){delete browser;return nullptr;}
    browser->_Launched=true;browser->Refresh();return browser;
}
QWidget* DeletionBrowserWidget(IProjectAssetsUI* value){return static_cast<ProjectAssetsUI*>(value)->_Window;}
bool SelectDeletionSource(IProjectAssetsUI* value,const std::string& guid) {
    auto* browser=static_cast<ProjectAssetsUI*>(value);browser->Refresh();
    auto* tree=browser->_Window->findChild<QTreeView*>("assetTree");
    for(int row=0;row<tree->model()->rowCount();++row) {
        const auto index=tree->model()->index(row,0);
        if(index.data(ProjectAssetsModel::GuidRole).toString().toStdString()==guid){tree->selectionModel()->setCurrentIndex(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);return true;}
    }
    return false;
}
