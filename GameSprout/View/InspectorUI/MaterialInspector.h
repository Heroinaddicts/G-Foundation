#pragma once
#include "ImportSettingsInspector.h"
class IAssetsManager;
class ILibraryManager;
class ISelectionManager;
class MaterialPropertyRow;
namespace Ui { class MaterialInspectorForm; }

class MaterialInspector final : public ImportSettingsInspector {
public:
    explicit MaterialInspector(QWidget * parent);
    ~MaterialInspector() override;
    void Bind(IAssetsManager *, ILibraryManager *, ISelectionManager *);
    void SetContext(const std::string & key, bool embedded, const std::string & extractionPath = {});
    void Load(const Json::Value & document) override;
    bool Read(Json::Value & document, std::string & error) const override;
    bool Snapshot(Json::Value & document, std::string & error) const { return Capture(document,error); }
    void SetDiagnostics(const Json::Value & diagnostics);
    void SetDebugMode(bool debug);
    void Deactivate();
    std::function<bool(const std::string & path, std::string & error)> Extract;
    std::function<void(const std::string & guid)> OpenExternal;
private:
    void Rebuild();
    void ChangeShader();
    void ExtractMaterial();
    bool Capture(Json::Value & document, std::string & error) const;
    Ui::MaterialInspectorForm * _Ui = nullptr;
    IAssetsManager * _Assets = nullptr;
    ILibraryManager * _Library = nullptr;
    ISelectionManager * _Selection = nullptr;
    Json::Value _Document;
    Json::Value _Schema;
    std::vector<MaterialPropertyRow *> _Rows;
    std::string _Context;
    std::string _ExtractionPath;
    std::string _Error;
    bool _Loading = false;
    bool _Embedded = false;
    bool _DebugMode = false;
};
