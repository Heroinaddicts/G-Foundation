#ifndef __IDEWindow_h__
#define __IDEWindow_h__

#include "Header.h"
#include <utility>
#include <vector>

QT_BEGIN_NAMESPACE
class QMainWindow;
namespace Ui {
class IDEWindowWindow;
}
QT_END_NAMESPACE
class IDEDockerManager;
class IDELoadingOverlay;

class IDEWindow : public IIDEWindow {
public:
    IDEWindow() = default;
    ~IDEWindow() override;

    IDEWindow(const IDEWindow&) = delete;
    IDEWindow& operator=(const IDEWindow&) = delete;

    IIDEMenuBar* GetMenuBar() const override;
    IIDEDocker* CreateDocker(const QString& name) override;
    IIDEDocker* QueryDocker(const QString& name) override;
    bool ResizeDockers(const QList<IIDEDocker*>& dockers,
                       const QList<int>& sizes, Qt::Orientation orientation) override;

    void Loading(const std::string & title, const std::string & desc) override;
    void Unloading() override;
    void SetLoadingInfo(const std::string & title, const std::string & desc) override;

    void Show() override;
    void Hide() override;

private:
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;

    // Qt 窗口及 C++ 包装对象均在 Release 中按顺序显式清理。
    QMainWindow* _Window = nullptr;
    Ui::IDEWindowWindow* _Ui = nullptr;
    IIDEMenuBar* _MenuBar = nullptr;
    IDEDockerManager* _DockerManager = nullptr;
    IDELoadingOverlay* _LoadingOverlay = nullptr;
    std::vector<std::pair<QString, IIDEDocker*>> _Dockers;

    int _LoadingRefCount = 0;
    int _TotalCount = 0;
};

#endif //__IDEWindow_h__
