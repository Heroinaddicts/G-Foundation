#include "ProjectAssetsUI.h"
#include "ui_ProjectAssetsContextMenu.h"
#include <QAction>
#include <QMenu>

void ProjectAssetsUI::RegisterContextMenus() {
    IContextMenuManager* manager = _Docker->GetContextMenuManager();
    if (!manager) return;

    // 固定菜单结构来自 Designer；此处只向 Docker 注册并绑定已有操作。
    QMenu definition;
    Ui::ProjectAssetsContextMenu ui;
    ui.setupUi(&definition);
    const std::function<void(const QMenu*, IContextMenu*)> registerItems =
        [&](const QMenu* source, IContextMenu* parent) {
            for (QAction* action : source->actions()) {
                const std::string name = action->isSeparator() ? "" : action->text().toStdString();
                IContextMenu* menu = parent ? parent->CreateChildMenu(name) : manager->CreateMenu(name);
                if (!menu) continue;
                if (action->menu()) registerItems(action->menu(), menu);
                else if (action->objectName() == QStringLiteral("actionRefresh")) {
                    menu->SetClickCalback(&ProjectAssetsUI::RefreshMenu);
                } else if (_Scenes && action->objectName() == QStringLiteral("actionScene")) {
                    menu->SetClickCalback(&ProjectAssetsUI::CreateSceneMenu);
                }
                else if (_RuntimeAssets && action->objectName()==QStringLiteral("actionAnimatorController")) {
                    menu->SetClickCalback(&ProjectAssetsUI::CreateAnimatorControllerMenu);
                } else if (_RuntimeAssets && action->objectName()==QStringLiteral("actionAvatarMask")) {
                    menu->SetClickCalback(&ProjectAssetsUI::CreateAvatarMaskMenu);
                } else if (_Scenes && action->objectName()==QStringLiteral("actionInstantiateModel")) {
                    menu->SetClickCalback(&ProjectAssetsUI::InstantiateModelMenu);
                }
                // 尚未实现的资源操作保持禁用，不绑定占位回调。
            }
        };
    registerItems(&definition, nullptr);
}
