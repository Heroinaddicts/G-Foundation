#include "IIDEWindow.h"
#include <QApplication>
#include <QLibrary>
#include <iostream>
#include <stdexcept>

// Exercise the same lifecycle access as the Editor without linking its executable.
namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static bool Initialize(IGameSproutView* view) { return view->Initialize(nullptr); }
    static void Release(IGameSproutView* view) { view->Release(nullptr); }
};
}

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class TestTasks final : public Foundation::ITaskApi {
public:
    UInt64 Budget = 1000;
    int Changes = 0;
    bool Supported = true;
    UInt64 RegisterTaskOwner() override { return 1; }
    void UnregisterTaskOwner(UInt64) override {}
    Foundation::TaskSubmitResult SubmitTask(const Foundation::TaskSubmitOptions&,
        Foundation::fTask, Foundation::fTaskCompleted) override { return {}; }
    bool CancelTask(UInt64) override { return false; }
    UInt64 GetCompletionBudgetMicroseconds() const override { return Budget; }
    bool SetCompletionBudgetMicroseconds(UInt64 value) override {
        if (!Supported || value == 0) return false;
        Budget = value;
        ++Changes;
        return true;
    }
};
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    TestTasks first, second;
    if (argc != 2) return 2;
    QLibrary library(QString::fromLocal8Bit(argv[1]));
    IIDEWindow* window = nullptr;
    try {
        Check(library.load(), qPrintable(library.errorString()));
        using GetViewFunction = GameSproutEditor::IGameSproutView* (*)();
        auto getView = reinterpret_cast<GetViewFunction>(library.resolve("GetView"));
        Check(getView != nullptr, "GetView export missing");
        window = dynamic_cast<IIDEWindow*>(getView());
        Check(window && GameSproutEditor::GameSproutEditorFinal::Initialize(window), "IDEWindow initialization failed");

        window->BindTaskApi(&first);
        Check(first.Changes == 0, "Idle binding changed the budget");
        window->Loading("outer", "first");
        Check(first.Budget == 12000 && first.Changes == 1, "Loading did not raise budget");
        window->Loading("inner", "second");
        window->BindTaskApi(&first);
        Check(first.Changes == 1, "Nested Loading replaced saved budget");
        window->Unloading();
        Check(first.Budget == 12000, "Nested Unloading restored too early");
        window->Unloading();
        Check(first.Budget == 1000 && first.Changes == 2, "Final Unloading did not restore budget");
        window->Unloading();
        Check(first.Changes == 2, "Extra Unloading changed the budget");

        first.Budget = 20000;
        window->Loading("higher", "preserve");
        window->Unloading();
        Check(first.Budget == 20000 && first.Changes == 2, "Loading reduced a larger caller budget");

        window->BindTaskApi(nullptr);
        window->Loading("late", "binding");
        first.Budget = 3000;
        window->BindTaskApi(&first);
        Check(first.Budget == 12000, "Late binding missed active Loading");
        second.Budget = 7000;
        window->BindTaskApi(&second);
        Check(first.Budget == 3000 && second.Budget == 12000, "Active rebinding did not restore old owner");
        window->BindTaskApi(nullptr);
        Check(second.Budget == 7000, "Unbinding did not restore previous budget");
        window->Unloading();

        first.Budget = 5000;
        first.Supported = false;
        window->BindTaskApi(&first);
        window->Loading("unsupported", "fallback");
        window->Unloading();
        Check(first.Budget == 5000, "Unsupported TaskApi budget changed");

        first.Supported = true;
        first.Budget = 6000;
        window->Loading("release", "restore");
        Check(first.Budget == 12000, "Release case did not raise budget");
        GameSproutEditor::GameSproutEditorFinal::Release(window);
        window = nullptr;
        Check(first.Budget == 6000, "Release did not restore budget");
        std::cout << "Loading nesting, late binding, rebinding and budget restoration passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (window) GameSproutEditor::GameSproutEditorFinal::Release(window);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
