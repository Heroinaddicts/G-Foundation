#pragma once
#include "InspectorPanel.h"
#include "Header.h"
class QDialog;
namespace Ui {class PrefabInstanceSectionForm;class PrefabOverridesPopup;}

class PrefabInstanceSection final : public InspectorSection, public IPrefabListener, public IPlayModeListener {
public:
    PrefabInstanceSection(const InspectorServices&,QWidget*);
    ~PrefabInstanceSection() override;
    bool Load(const InspectorTarget&,bool,std::string&) override;
    void Deactivate() override;
    void OnPrefabStateChanged() override;
    void OnPlayModeStateChanged(const PlayModeState&) override;
private:
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
    void UpdateSourceName();
    bool Read(PrefabInstanceInfo&,std::string&) const;
    bool CanAuthor() const;
    bool Refresh(std::string&);
    void Present(const PrefabInstanceInfo&,bool showOverrides);
    void QueueRefresh();
    void OpenOverrides();
    void ApplyAll();
    void RevertAll();
    void SelectSource();
    void OpenSource();
    void Error(const std::string&);
    Ui::PrefabInstanceSectionForm* _Ui=nullptr;
    QDialog* _Popup=nullptr;
    Ui::PrefabOverridesPopup* _PopupUi=nullptr;
    IPrefabManager* _Prefabs=nullptr;
    IPlayMode* _Play=nullptr;
    InspectorTarget _Target;
    QString _SourceName;
    bool _PrefabSubscribed=false,_PlaySubscribed=false;
    bool _Queued=false;
};
