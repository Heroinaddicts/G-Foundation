#ifndef __HierarchyUI_h__
#define __HierarchyUI_h__

#include "Header.h"

QT_BEGIN_NAMESPACE
class QMainWindow;
namespace Ui {
class HierarchyUIWindow;
}
QT_END_NAMESPACE

class HierarchyUI : public IHierarchyUI {
public:
    virtual ~HierarchyUI() {}

    void Show() override;
    void Hide() override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
};
#endif //__HierarchyUI_h__
