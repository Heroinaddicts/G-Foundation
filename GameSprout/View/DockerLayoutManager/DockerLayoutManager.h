#ifndef __DockerLayoutManager_h__
#define __DockerLayoutManager_h__

#include "Header.h"

class DockerLayoutManager : public IDockerLayoutManager {
public:
    virtual ~DockerLayoutManager() {}

    bool ResetDefaultLayout() override;

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
    static void OnResetLayout();
    static DockerLayoutManager* s_Instance; // 非拥有指针，供无参数菜单回调访问。
};
#endif //__DockerLayoutManager_h__
