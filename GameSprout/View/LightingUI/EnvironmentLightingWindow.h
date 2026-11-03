#pragma once
#include "ILightingAuthoring.h"
#include <QDialog>
#include <array>

class QLineEdit;
class QLabel;
class QPushButton;
namespace Ui {class EnvironmentLightingForm;}

// Presentation only. Settings, scene identities and authoring transactions are
// owned by LightingAuthoring; this window receives value snapshots by event.
class EnvironmentLightingWindow final : public QDialog,public ILightingEnvironmentListener {
public:
    explicit EnvironmentLightingWindow(QWidget* parent=nullptr);
    ~EnvironmentLightingWindow() override;
    bool Bind(ILightingAuthoring* authoring);
    void Shutdown();
    void OnEnvironmentLightingChanged(const EnvironmentLightingSnapshot&) override;
private:
    struct ColorRow {QLabel* Label;QWidget* Row;std::array<QLineEdit*,3> Fields;QPushButton* Button;};
    GameSproutRuntime::Color& Color(GameSproutRuntime::EnvironmentLightingSettings&,std::size_t row) const;
    void UpdateControls();
    void Commit(const GameSproutRuntime::EnvironmentLightingSettings&);
    void CommitColor(std::size_t row,std::size_t channel);
    void ChooseColor(std::size_t row);
    void Error(const std::string& message);
    Ui::EnvironmentLightingForm* _Ui=nullptr;
    ILightingAuthoring* _Authoring=nullptr;
    EnvironmentLightingSnapshot _Snapshot;
    std::array<ColorRow,7> _Colors;
    bool _Subscribed=false,_Loading=false,_Committing=false;
};
