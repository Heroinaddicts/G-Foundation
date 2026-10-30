#pragma once
#include "Header.h"
#include "IAssetsManager.h"
#include "ISelectionManager.h"
#include "ILibraryManager.h"
#include <map>
namespace Ui { class InspectorUIWindow; }
class QWidget;
class QObject;
class ImportSettingsInspector;
class AssetInfoInspector;

class InspectorUI final : public IInspectorUI, public ISelectionListener, public IAssetListener {
public:
    InspectorUI() = default;
    ~InspectorUI() override;
    InspectorUI(const InspectorUI &) = delete;
    InspectorUI & operator=(const InspectorUI &) = delete;
    void Show() override;
    void Hide() override;
    bool HasPendingChanges() const override;
    bool ApplyChanges() override;
    void RevertChanges() override;
    void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection> &) override { _Dirty = true; }
    void OnAssetCreate(const AssetEvent &) override { _Dirty = true; }
    void OnAssetModify(const AssetEvent &) override { _Dirty = true; }
    void OnAssetRemove(const AssetEvent &) override { _Dirty = true; }
private:
    void BeforeInitialize(GameSprout::GameSproutEditor * editor) override;
    bool Initialize(GameSprout::GameSproutEditor * editor) override;
    bool Launch(GameSprout::GameSproutEditor * editor) override;
    void LaunchFinished(GameSprout::GameSproutEditor *) override { _Dirty = true; }
    void Update(GameSprout::GameSproutEditor *) override;
    void Release(GameSprout::GameSproutEditor *) override;
    void Refresh();
    void UpdateButtons();
    bool ResolvePendingChanges();
    void Clear(const QString & message);
    QWidget * _Window = nullptr;
    QObject * _Dispatch = nullptr;
    Ui::InspectorUIWindow * _Ui = nullptr;
    std::map<eAssetImporterType, ImportSettingsInspector *> _Panels; // Qt parent owns panels.
    ImportSettingsInspector * _ActivePanel = nullptr;
    AssetInfoInspector * _Info = nullptr;
    IIDEDocker * _Docker = nullptr;
    IAssetsManager * _Assets = nullptr;
    ISelectionManager * _Selection = nullptr;
    ILibraryManager * _Library = nullptr;
    bool _Attached = false;
    bool _AssetSubscribed = false;
    bool _SelectionSubscribed = false;
    bool _Dirty = true;
    bool _Editable = false;
    bool _ResolvingSelection = false;
    std::vector<ProjectAssetSelection> _Inspected;
    std::string _BaselineSettings;
    QString _OperationError;
};
