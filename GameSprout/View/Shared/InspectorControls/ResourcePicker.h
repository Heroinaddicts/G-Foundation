#pragma once
#include "AssetReferenceResolver.h"
#include <QDialog>
#include <functional>
namespace Ui {class ResourcePickerDialog;}
class ResourcePicker final : public QDialog {
    Q_OBJECT
public:
    explicit ResourcePicker(QWidget* parent=nullptr);
    ~ResourcePicker() override;
    void SetCandidates(const std::vector<AssetReferenceCandidate>& candidates,eAssetType type);
    void SetError(const std::string& error);
    std::function<void(const GameSproutRuntime::ResourceReference&)> Selected;
private:
    void Filter();
    void Choose();
    Ui::ResourcePickerDialog* _Ui=nullptr;
};
