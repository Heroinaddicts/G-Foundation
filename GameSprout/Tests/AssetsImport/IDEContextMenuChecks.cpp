#include "ProjectViewTestSupport.h"
#include <QAction>
#include <QContextMenuEvent>
#include <QDockWidget>
#include <QMenu>

static int Clicks = 0;
static void OnClick() { ++Clicks; }

class EmptyAssets final : public IAssetsManager {
public:
    IAssetMeta* QueryAsset(const Guid&) const override { return nullptr; }
    bool ReimportAsset(const Guid&) override { return false; }
    bool ApplyImportSettings(const Guid&, const std::string&, const std::string&, std::string&) override { return false; }
    bool LoadMaterialSource(const Guid&, std::string&, std::string&) const override { return false; }
    bool SaveMaterialSource(const Guid&, const std::string&, const std::string&, std::string&) override { return false; }
    bool ExtractMaterial(const Guid&, uint64_t, const std::string&, std::string&, std::string&) override { return false; }
    std::vector<IAssetMeta*> GetAssetMetas() const override { return {}; }
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override { return {}; }
    bool RegisterAssetListener(IAssetListener*) override { return true; }
    void UnregisterAssetListener(IAssetListener*) override {}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override { return true; }
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override { return true; }
    void Release(GameSproutEditor::GameSproutModuleApi*) override {}
};

static QAction* FindAction(QMenu* menu, const QString& text) {
    if (menu) for (QAction* action : menu->actions()) if (action->text() == text) return action;
    throw std::runtime_error("Menu action missing: " + text.toStdString());
}

static QMenu* ContentMenu(QWidget* content, const QString& dockerName) {
    QMenu* menu = content->parentWidget()->findChild<QMenu*>(dockerName + ".ContentContextMenu");
    Check(menu != nullptr, "Docker content menu missing");
    return menu;
}

static void RightClick(QWidget* target) {
    const QPoint local(8, 8);
    QContextMenuEvent event(QContextMenuEvent::Mouse, local, target->mapToGlobal(local));
    QCoreApplication::sendEvent(target, &event);
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    IIDEWindow* ide = nullptr;
    IProjectAssetsUI* project = nullptr;
    PreviewEditor editor;
    EmptyAssets assets;
    try {
        ide = static_cast<IIDEWindow*>(LoadView("IDEWindow"));
        editor.IDE = ide;
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(ide, &editor), "IDE initialization failed");
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(ide, &editor), "IDE launch failed");
        IIDEDocker* docker = ide->CreateDocker("ContextTest");
        auto* content = new QWidget();
        auto* label = new QLabel("Content", content);
        Check(docker->AttachWidget(content), "Cannot attach menu test content");
        IContextMenuManager* manager = docker->GetContextMenuManager();
        Check(manager != nullptr && manager->CreateMenu("  ") == nullptr, "Invalid context manager");
        IContextMenu* create = manager->CreateMenu("Create");
        Check(create == manager->CreateMenu("Create"), "Duplicate root registration");
        IContextMenu* shader = create->CreateChildMenu("Shader");
        IContextMenu* vertex = shader->CreateChildMenu("Vertex");
        Check(vertex == shader->CreateChildMenu("Vertex"), "Duplicate child registration");
        create->SetClickCalback(&OnClick);
        vertex->SetClickCalback(&OnClick);
        manager->CreateMenu("Unimplemented");
        IContextMenu* separator = manager->CreateMenu("");
        Check(separator && !separator->CreateChildMenu("Invalid"), "Separator accepted child");

        QMenu* menu = ContentMenu(content, "ContextTest");
        RightClick(label);
        Check(menu->isVisible(), "Child right click did not show content menu");
        QAction* createAction = FindAction(menu, "Create");
        QMenu* shaderMenu = FindAction(createAction->menu(), "Shader")->menu();
        QAction* vertexAction = FindAction(shaderMenu, "Vertex");
        Check(vertexAction->isEnabled() && !FindAction(menu, "Unimplemented")->isEnabled(), "Incorrect command state");
        createAction->trigger();
        Check(Clicks == 0, "Submenu invoked a leaf callback");
        vertexAction->trigger();
        Check(Clicks == 1, "Leaf callback missing or duplicated");
        vertex->SetClickCalback(nullptr);
        Check(!vertexAction->isEnabled(), "Null callback did not disable command");
        menu->hide();

        // 动态创建的控件、滚动视口也应进入同一 Docker 的菜单。
        auto* tree = new QTreeView(content);
        RightClick(tree->viewport());
        Check(menu->isVisible(), "Dynamic viewport right click missing");
        menu->hide();
        bool customRequested = false;
        auto* custom = new QWidget(content);
        custom->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(custom, &QWidget::customContextMenuRequested, custom, [&]() { customRequested = true; });
        RightClick(custom);
        Check(customRequested && !menu->isVisible(), "View-owned context menu was intercepted");
        // 输入框仍应弹出 Qt 自己的复制/粘贴菜单，而不是 Docker 菜单。
        auto* input = new QLineEdit(content);
        QTimer::singleShot(0, input, []() {
            if (QWidget* popup = QApplication::activePopupWidget()) popup->close();
        });
        RightClick(input);
        Check(!menu->isVisible(), "Text editor context menu was intercepted");

        IIDEDocker* other = ide->CreateDocker("OtherContext");
        auto* otherContent = new QWidget();
        Check(other->AttachWidget(otherContent), "Cannot attach second content");
        other->GetContextMenuManager()->CreateMenu("Other")->SetClickCalback(&OnClick);
        QMenu* otherMenu = ContentMenu(otherContent, "OtherContext");
        RightClick(otherContent);
        Check(otherMenu->isVisible() && !menu->isVisible() && otherMenu->actions().size() == 1,
              "Docker menus leaked across panels");
        otherMenu->hide();
        QDockWidget* dockWidget = qobject_cast<QDockWidget*>(content->parentWidget());
        QMenu* panelMenu = dockWidget->findChild<QMenu*>("DockerContextMenu.ContextTest");
        Check(panelMenu && panelMenu != menu, "Panel menu replaced by content menu");

        editor.Modules["AssetsManager"] = &assets;
        project = LoadProjectView();
        GameSproutEditor::GameSproutEditorFinal::Before(project, &editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(project, &editor), "Project initialization failed");
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(project, &editor), "Project launch failed");
        QDockWidget* projectDock = dockWidget->parentWidget()->findChild<QDockWidget*>("ProjectAssets");
        Check(projectDock != nullptr, "Project Docker missing");
        auto* assetTree = projectDock->findChild<QTreeView*>("assetTree");
        auto* folderTree = projectDock->findChild<QTreeView*>("folderTree");
        QMenu* projectMenu = projectDock->findChild<QMenu*>("ProjectAssets.ContentContextMenu");
        RightClick(assetTree->viewport());
        Check(projectMenu->isVisible(), "Project asset area menu missing");
        QMenu* projectCreate = FindAction(projectMenu, "Create")->menu();
        Check(projectCreate && FindAction(projectCreate, "Scene") && FindAction(projectCreate, "Material"),
              "Project Create commands missing");
        QAction* fragment = FindAction(FindAction(projectCreate, "Shader")->menu(), "Fragment Shader");
        Check(!fragment->isEnabled() && FindAction(projectMenu, "Refresh")->isEnabled(), "Placeholder commands were enabled");
        projectMenu->grab().save("/tmp/gamesprout-context-menu.png");
        projectMenu->hide();
        RightClick(folderTree->viewport());
        Check(projectMenu->isVisible(), "Project folder area menu missing");
        projectMenu->hide();
        FindAction(projectMenu, "Refresh")->trigger();
        GameSproutEditor::GameSproutEditorFinal::Update(project, &editor);
        GameSproutEditor::GameSproutEditorFinal::Release(project, &editor);
        project = nullptr;

        // 内容控件先销毁，Docker 保留并挂载新内容，过滤器与菜单仍应可用。
        delete content;
        content = new QWidget();
        Check(docker->AttachWidget(content), "Cannot replace destroyed content");
        RightClick(content);
        Check(menu->isVisible(), "Menu failed after content recreation");
        GameSproutEditor::GameSproutEditorFinal::Release(ide, &editor);
        ide = nullptr;
        QCoreApplication::processEvents();
        std::cout << "PASS Docker context menus: nested registration/callbacks, child and viewport events, native input menus, isolation, Project UI and cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        if (project) GameSproutEditor::GameSproutEditorFinal::Release(project, &editor);
        if (ide) GameSproutEditor::GameSproutEditorFinal::Release(ide, &editor);
        std::cerr << "FAIL " << error.what() << "\n";
        return 1;
    }
}
