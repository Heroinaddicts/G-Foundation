#ifndef __IDEWindow_h__
#define __IDEWindow_h__

#include "Header.h"
#include <utility>
#include <vector>
#include <map>

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
    void BindTaskApi(Foundation::ITaskApi* taskApi) override;
    uint64_t BeginImportProgress(uint64_t total, const std::string& description) override;
    bool UpdateImportProgress(uint64_t token, uint64_t completed, const std::string& path) override;
    bool EndImportProgress(uint64_t token) override;

    void Show() override;
    void Hide() override;

private:
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
    void IncreaseCompletionBudget();
    void RestoreCompletionBudget();
    void RefreshLoadingProgress();

    // Qt 窗口及 C++ 包装对象均在 Release 中按顺序显式清理。
    QMainWindow* _Window = nullptr;
    Ui::IDEWindowWindow* _Ui = nullptr;
    IIDEMenuBar* _MenuBar = nullptr;
    IDEDockerManager* _DockerManager = nullptr;
    IDELoadingOverlay* _LoadingOverlay = nullptr;
    std::vector<std::pair<QString, IIDEDocker*>> _Dockers;

    int _LoadingRefCount = 0;
    struct ImportProgress {
        uint64_t Total;
        uint64_t Completed = 0;
        std::string Description;
    };
    std::map<uint64_t, ImportProgress> _ImportProgress;
    uint64_t _NextImportProgress = 0;
    std::string _LoadingTitle;
    std::string _LoadingDescription;
    bool _HadImportProgress = false;
    Foundation::ITaskApi* _TaskApi = nullptr; // Borrowed; main thread only.
    UInt64 _SavedCompletionBudgetMicroseconds = 0;
    bool _CompletionBudgetAdjusted = false;
};

#endif //__IDEWindow_h__
