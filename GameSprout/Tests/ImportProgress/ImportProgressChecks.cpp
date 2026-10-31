#include "IIDEWindow.h"
#include <QApplication>
#include <QLabel>
#include <QLibrary>
#include <QProgressBar>
#include <QWidget>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static bool Initialize(IGameSproutView* view) { return view->Initialize(nullptr); }
    static void Update(IGameSproutView* view) { view->Update(nullptr); }
    static void Release(IGameSproutView* view) { view->Release(nullptr); }
};
}

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

QWidget* Overlay() {
    for (QWidget* widget : QApplication::allWidgets())
        if (widget->objectName() == "loadingOverlay") return widget;
    throw std::runtime_error("Loading overlay missing");
}

void CheckProgress(uint64_t completed, uint64_t total, int percentage, const QString& path) {
    QWidget* overlay = Overlay();
    auto* progress = overlay->findChild<QProgressBar*>("progressBar");
    auto* count = overlay->findChild<QLabel*>("completedLabel");
    auto* description = overlay->findChild<QLabel*>("descriptionLabel");
    auto* title = overlay->findChild<QLabel*>("titleLabel");
    Check(progress && count && description && title, "Progress form widgets missing");
    Check(progress->minimum() == 0 && progress->maximum() == 100 && progress->value() == percentage,
        "Unexpected percentage or indeterminate resource progress");
    Check(count->text() == QString("已完成 %1 / %2").arg(qulonglong(completed)).arg(qulonglong(total)),
        "Loading references changed the planned denominator");
    Check(description->text() == path && title->text() == "资源导入", "Current resource path/title is stale");
}

void CheckIndeterminate() {
    QWidget* overlay = Overlay();
    auto* progress = overlay->findChild<QProgressBar*>("progressBar");
    auto* count = overlay->findChild<QLabel*>("completedLabel");
    Check(progress && count && progress->maximum() == 0 && progress->minimum() == 0,
        "Barrier or GC is being counted as resource imports");
    Check(count->text() == "正在处理…", "Indeterminate stage retained a fake completed/total count");
}
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    if (argc != 2) return 2;
    QLibrary library(QString::fromLocal8Bit(argv[1]));
    IIDEWindow* window = nullptr;
    try {
        Check(library.load(), qPrintable(library.errorString()));
        using GetViewFunction = GameSproutEditor::IGameSproutView* (*)();
        auto getView = reinterpret_cast<GetViewFunction>(library.resolve("GetView"));
        Check(getView != nullptr, "GetView export missing");
        window = dynamic_cast<IIDEWindow*>(getView());
        Check(window != nullptr, "IIDEWindow missing");
        Check(window->BeginImportProgress(3, "not initialized") == 0, "Progress accepted before Initialize");
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(window), "IDEWindow initialization failed");
        Check(window->BeginImportProgress(0, "empty") == 0, "Empty plan should not create a batch");

        window->Loading("初始化资源", "扫描 Assets");
        CheckIndeterminate();
        const uint64_t first = window->BeginImportProgress(3, "准备导入");
        Check(first != 0, "Could not register fixed plan");
        CheckProgress(0, 3, 0, "准备导入");
        window->Loading("资源导入", "first.png");
        window->Loading("资源导入", "second.png");
        window->Loading("资源导入", "third.png");
        window->Loading("清理资源缓存", "检查遗留产物");
        window->SetLoadingInfo("清理资源缓存", "删除过期产物");
        GameSproutEditor::GameSproutEditorFinal::Update(window);
        CheckProgress(0, 3, 0, "准备导入");
        window->Unloading(); // GC completion must not advance the resource numerator.
        CheckProgress(0, 3, 0, "准备导入");
        Check(!window->EndImportProgress(first), "Incomplete batch ended early");
        Check(!window->UpdateImportProgress(first, 4, "overflow"), "Progress accepted more completions than planned");
        Check(!window->UpdateImportProgress(0, 1, "invalid") && !window->EndImportProgress(0), "Invalid token accepted");

        const uint64_t second = window->BeginImportProgress(2, "queued");
        Check(second > first, "Batch identity did not advance");
        Check(window->UpdateImportProgress(second, 1, "queued.png"), "Queued batch did not advance independently");
        CheckProgress(0, 3, 0, "准备导入");
        Check(window->UpdateImportProgress(first, 1, "小熊.fbx"), "Could not publish resource completion");
        window->Unloading();
        CheckProgress(1, 3, 33, "小熊.fbx");
        Check(!window->UpdateImportProgress(first, 0, "backwards"), "Progress regressed");
        Check(window->UpdateImportProgress(first, 1, "next.png"), "Path update should be idempotent");
        CheckProgress(1, 3, 33, "next.png");

        Check(window->UpdateImportProgress(first, 3, "last.png") && window->EndImportProgress(first), "Completed plan did not finish");
        CheckProgress(1, 2, 50, "queued.png");
        Check(!window->UpdateImportProgress(first, 3, "late"), "Ended token remained live");
        Check(window->UpdateImportProgress(second, 2, "queued-last.png") && window->EndImportProgress(second),
            "Queued plan did not finish");
        CheckIndeterminate(); // Startup barrier is still pending; 100% would be misleading.
        window->Unloading();
        window->Unloading();
        window->Unloading();
        Check(Overlay()->isHidden(), "Final Unloading did not hide overlay");

        const uint64_t hugeTotal = (std::numeric_limits<uint64_t>::max)();
        const uint64_t huge = window->BeginImportProgress(hugeTotal, "large plan");
        window->Loading("large", "work");
        Check(window->UpdateImportProgress(huge, hugeTotal - 1, "near completion"), "Large plan update failed");
        CheckProgress(hugeTotal - 1, hugeTotal, 99, "near completion");
        Check(window->UpdateImportProgress(huge, hugeTotal, "done") && window->EndImportProgress(huge), "Large plan did not finish");
        window->Unloading();

        window->Loading("release", "pending");
        const uint64_t stale = window->BeginImportProgress(1, "cancelled");
        GameSproutEditor::GameSproutEditorFinal::Release(window);
        Check(!window->UpdateImportProgress(stale, 1, "late") && !window->EndImportProgress(stale), "Release retained a token");
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(window), "Reinitialize failed");
        window->Loading("new", "new cycle");
        const uint64_t renewed = window->BeginImportProgress(1, "new resource");
        Check(renewed > stale && !window->UpdateImportProgress(stale, 1, "late"), "Old token collided after reinitialize");
        Check(window->UpdateImportProgress(renewed, 1, "finished") && window->EndImportProgress(renewed), "Reinitialized batch failed");
        window->Unloading();
        GameSproutEditor::GameSproutEditorFinal::Release(window);
        window = nullptr;
        std::cout << "PASS fixed import plans, independent Loading/GC references, concurrent batches, stale tokens and pending finalization\n";
        return 0;
    } catch (const std::exception& error) {
        if (window) GameSproutEditor::GameSproutEditorFinal::Release(window);
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
