#ifndef __ConsoleUI_h__
#define __ConsoleUI_h__

#include "Header.h"
#include <array>
#include <QString>

QT_BEGIN_NAMESPACE
class QObject;
class QMainWindow;
class QCheckBox;
namespace Ui {
class ConsoleUIWindow;
}
QT_END_NAMESPACE
class IIDEDocker;
class ConsoleFilterPopup;

class ConsoleUI : public IConsoleUI {
public:
    ConsoleUI() = default;
    ~ConsoleUI() override;

    void ConsoleLog(const char * file, const int line, const eConsoleLog e, const std::string & log) override;
    void Show() override;
    void Hide() override;

private:
    void AppendLog(eConsoleLog level, const QString& message, const QString& location);
    void ApplyFilters();
    void UpdateDetails();
    void UpdateCounters();
    void ClearLogs();
    void UpdateFilterSummary();
    QCheckBox* LevelCheckBox(eConsoleLog level) const;
    bool MatchesFilters(eConsoleLog level, const QString& message, const QString& location) const;

    ConsoleUI(const ConsoleUI&) = delete;
    ConsoleUI& operator=(const ConsoleUI&) = delete;

private:
    void BeforeInitialize(GameSprout::GameSproutEditor * const editor) override;
    bool Initialize(GameSprout::GameSproutEditor * const editor) override;
    bool Launch(GameSprout::GameSproutEditor * const editor) override;
    void LaunchFinished(GameSprout::GameSproutEditor * const editor) override;
    void Update(GameSprout::GameSproutEditor * const editor) override;
    void Release(GameSprout::GameSproutEditor * const editor) override;

    QMainWindow* _Window = nullptr; // Owned here until AttachWidget succeeds.
    Ui::ConsoleUIWindow* _Ui = nullptr;
    ConsoleFilterPopup* _Filters = nullptr; // Parented to the console window.
    QObject* _Dispatch = nullptr; // Owns callback lifetime, not the dock content.
    IIDEDocker* _Docker = nullptr; // Borrowed from IDEWindow.
    bool _Attached = false;
    std::array<int, 4> _Counts{};
};
#endif //__ConsoleUI_h__
