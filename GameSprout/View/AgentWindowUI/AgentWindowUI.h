#ifndef __AgentWindowUI_h__
#define __AgentWindowUI_h__

#include "Header.h"

QT_BEGIN_NAMESPACE
class QMainWindow;
namespace Ui {
class AgentWindow;
}
QT_END_NAMESPACE

class AgentWindowUI : public IAgentWindowUI {
public:
    virtual ~AgentWindowUI() {}

    void Show() override;
    void Hide() override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;

private:
    IIDEDocker * _AgentWinDocker = nullptr;

    QWidget* _Window = nullptr;
    QObject* _Dispatch = nullptr;
    QObject* _ContextCapture = nullptr;
    Ui::AgentWindow* _Ui = nullptr;
};
#endif //__AgentWindowUI_h__
