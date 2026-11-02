#ifndef __IHierarchyUI_h__
#define __IHierarchyUI_h__

#include "GameSproutEditorApi.h"
#include <cstdint>
#include "ISelectionManager.h"
#include <functional>

class QWidget;

struct HierarchyActionContext {
    uint64_t SceneID=0,ObjectID=0;
};
struct HierarchyAssetDropContext {
    uint64_t SceneID=0,ParentObjectID=0;
    std::vector<ProjectAssetSelection> Assets;
};

class IHierarchyUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IHierarchyUI() {}
    // 当前运行节点身份，0 表示未选择；不持有场景对象指针。
    virtual std::uint64_t GetSelectedObjectID() const = 0;
    // 功能 View 提供 .ui 页面；成功挂载后由 Hierarchy 持有，移除时销毁。
    virtual bool AttachHeaderWidget(QWidget*) {return false;}
    virtual bool SetHeaderWidgetVisible(QWidget*,bool) {return false;}
    virtual bool RemoveHeaderWidget(QWidget*) {return false;}
    using fAssetDropProbe=std::function<bool(const HierarchyAssetDropContext&)>;
    using fAssetDropExecute=std::function<bool(const HierarchyAssetDropContext&,std::string&)>;
    using fContextActionCallback=void(*)();
    using fContextActionEnabled=std::function<bool(const HierarchyActionContext&)>;
    virtual bool RegisterAssetDropHandler(const void*,const fAssetDropProbe&,const fAssetDropExecute&) {return false;}
    // 菜单文本由功能View的.ui提供。回调通过GetActionContext读取本次右键目标。
    virtual bool RegisterContextAction(const void*,const std::vector<std::string>&,fContextActionCallback,const fContextActionEnabled& = {}) {return false;}
    virtual HierarchyActionContext GetActionContext() const {return {};}
    virtual void UnregisterExtensions(const void*) {}
};

#endif //__IHierarchyUI_h__
