#pragma once
#include "AssetReferenceResolver.h"
#include <QWidget>
#include <functional>
namespace Ui {class ResourceFieldForm;}
class ResourcePicker;
class QMimeData;

class ResourceField final : public QWidget,public IAssetListener {
    Q_OBJECT
public:
    explicit ResourceField(QWidget* parent=nullptr);
    ~ResourceField() override;
    void Bind(IAssetsManager*,ILibraryManager*,ISelectionManager*,eAssetType);
    void SetContextKey(const std::string& key);
    void SetValue(const GameSproutRuntime::ResourceReference& value);
    GameSproutRuntime::ResourceReference GetValue() const {return _Value;}
    void SetError(const std::string& error);
    bool AssignAssetSelection(const ProjectAssetSelection& selection,std::string& error);
    std::function<void(const GameSproutRuntime::ResourceReference&)> Changed;
    void OnAssetCreate(const AssetEvent&) override;
    void OnAssetModify(const AssetEvent&) override;
    void OnAssetRemove(const AssetEvent&) override;
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
private:
    void Refresh();
    void ClosePicker();
    void OpenPicker(const std::vector<AssetReferenceCandidate>& candidates,const std::string& sourceGuid={});
    void RefreshPicker();
    bool AssignReference(const GameSproutRuntime::ResourceReference&,std::string& error);
    bool CanDrop(const QMimeData*,std::string& error);
    Ui::ResourceFieldForm* _Ui=nullptr;
    IAssetsManager* _Assets=nullptr;
    ILibraryManager* _Library=nullptr;
    ISelectionManager* _Selection=nullptr;
    eAssetType _Expected=eAssetType::Unknown;
    bool _Subscribed=false;
    GameSproutRuntime::ResourceReference _Value;
    ResourcePicker* _Picker=nullptr;
    std::string _PickerSourceGuid;
    std::string _ContextKey;
    std::uint64_t _Generation=0;
};
