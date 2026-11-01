#ifndef __IProjectAssetsUI_h__
#define __IProjectAssetsUI_h__

#include "GameSproutEditorApi.h"
#include "ISelectionManager.h"
#include <functional>

struct ProjectAssetActionContext {
    std::string Directory; // Assets 相对目录；空字符串表示 Assets 根目录。
    std::vector<ProjectAssetSelection> Selection; // 右键目标或当前选择的身份快照。
};

class IProjectAssetsUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IProjectAssetsUI() {}
    // 主线程；返回身份快照，不借用资源对象。
    virtual std::vector<ProjectAssetSelection> GetSelection() const = 0;
    virtual bool NavigateToDirectory(const std::string & path) = 0;
    using fAssetOpenHandler = std::function<bool(const ProjectAssetSelection&)>;
    using fContextActionCallback = void(*)();
    using fContextActionEnabled = std::function<bool(const ProjectAssetActionContext&)>;
    // 由功能 View 在 Launch 注册，在 Release 注销；不转移 owner 所有权。
    // handler 返回 true 表示已经接管打开操作；未接管的源文件交给系统打开。
    virtual bool RegisterAssetOpenHandler(const void* owner, const fAssetOpenHandler& handler) = 0;
    // menuPath 中每级文本来自功能 View 自己的 .ui 菜单定义。
    // 路径不能重复；predicate 每次弹出前重新判断，false 时禁用菜单。
    virtual bool RegisterContextAction(const void* owner, const std::vector<std::string>& menuPath,
        fContextActionCallback callback, const fContextActionEnabled& enabled = {}) = 0;
    virtual void UnregisterExtensions(const void* owner) = 0;
    virtual ProjectAssetActionContext GetActionContext() const = 0;
    // 文件创建成功后等待 AssetsManager 登记，再按身份定位选中；不主动导入或打开。
    virtual void NotifyAssetCreated(const std::string& relativePath) = 0;
};

#endif //__IProjectAssetsUI_h__
