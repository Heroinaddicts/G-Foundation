#include "DockerLayoutManager.h"

#include <QDebug>

DockerLayoutManager* DockerLayoutManager::s_Instance = nullptr;

void DockerLayoutManager::BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) {
    s_Instance = this;
    g_Editor = editor;
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);

    GSRegLaunchedCallback(g_IIDEWindow, [&](GameSproutEditor::GameSproutViewApi * const editor) {
        // 菜单与其他 View 的 Docker 一样，统一在 Launch 阶段创建。
        IIDEMenu* layoutMenu = g_IIDEWindow->GetMenuBar()->CreateChildMenu(QStringLiteral("布局"));
        IIDEMenu* resetMenu = layoutMenu ? layoutMenu->CreateChildMenu(QStringLiteral("恢复默认布局")) : nullptr;
        if (!resetMenu) {
            return false;
        }
        resetMenu->SetClickCallback(&DockerLayoutManager::OnResetLayout);
        return true;
    });
}

bool DockerLayoutManager::Initialize(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

bool DockerLayoutManager::Launch(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

void DockerLayoutManager::LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) {
    ResetDefaultLayout();
}

bool DockerLayoutManager::ResetDefaultLayout() {
    IIDEDocker* hierarchy = g_IIDEWindow->QueryDocker(QStringLiteral("Hierarchy"));
    IIDEDocker* scene = g_IIDEWindow->QueryDocker(QStringLiteral("Scene"));
    IIDEDocker* game = g_IIDEWindow->QueryDocker(QStringLiteral("Game"));
    IIDEDocker* inspector = g_IIDEWindow->QueryDocker(QStringLiteral("Inspector"));
    IIDEDocker* project = g_IIDEWindow->QueryDocker(QStringLiteral("ProjectAssets"));
    IIDEDocker* console = g_IIDEWindow->QueryDocker(QStringLiteral("Console"));
    if (!hierarchy || !scene || !game || !inspector || !project || !console) {
        return false;
    }

    // 先逐一解除旧标签组/浮动/关闭状态。保留原面板及其内容，不重新创建。
    hierarchy->Layout(eDockerLayout::Left);
    scene->Layout(eDockerLayout::Left);
    game->Layout(eDockerLayout::Left);
    inspector->Layout(eDockerLayout::Right);
    project->Layout(eDockerLayout::Bottom);
    console->Layout(eDockerLayout::Bottom);
    project->SetTitle(QStringLiteral("Project"));

    if (!hierarchy->Split(scene, Qt::Horizontal) ||
        !scene->Tabify(game) || !project->Tabify(console)) {
        return false;
    }
    // 约 19% / 57% / 24%；下方资源区域约占高度的 40%。
    // Qt 根据窗口实际尺寸和控件最小尺寸分配空间，用户仍可拖动分隔线。
    if (!g_IIDEWindow->ResizeDockers({hierarchy, scene, inspector}, {19, 57, 24}, Qt::Horizontal) ||
        !g_IIDEWindow->ResizeDockers({scene, project}, {60, 40}, Qt::Vertical)) {
        return false;
    }
    scene->Activate();
    project->Activate();
    return true;
}

void DockerLayoutManager::OnResetLayout() {
    if (s_Instance && !s_Instance->ResetDefaultLayout()) {
        qWarning() << "Cannot reset default dock layout.";
    }
}

void DockerLayoutManager::Show() {
}

void DockerLayoutManager::Hide() {
}

void DockerLayoutManager::Update(GameSproutEditor::GameSproutViewApi * const editor) {
    Q_UNUSED(editor);
}

void DockerLayoutManager::Release(GameSproutEditor::GameSproutViewApi * const editor) {
    Q_UNUSED(editor);
    if (s_Instance == this) {
        s_Instance = nullptr;
    }
    g_IIDEWindow = nullptr;
}
