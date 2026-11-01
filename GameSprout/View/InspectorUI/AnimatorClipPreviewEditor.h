#pragma once
#include "ComponentEditContext.h"
#include <QWidget>

namespace Ui {class AnimatorClipPreviewEditorForm;}
class ResourcePicker;
class QMimeData;

// View only: resource identities and UI state; the scene module owns playback.
class AnimatorClipPreviewEditor final : public QWidget {
public:
    explicit AnimatorClipPreviewEditor(QWidget* parent=nullptr);
    ~AnimatorClipPreviewEditor() override;
    void Bind(const ComponentEditContext&);
    void Refresh();
    void StopPreview();
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
private:
    void SelectClip(const GameSproutRuntime::ResourceReference&);
    bool Start(float seconds=0);
    void SetTime(float seconds);
    void ChangeOptions();
    void RefreshRelatedClips(std::uint64_t revision);
    void ShowError(const std::string&);
    void OpenPicker();
    void ClosePicker();
    bool Choose(const GameSproutRuntime::ResourceReference&,std::string&);
    bool CanDrop(const QMimeData*,std::string&) const;
    Ui::AnimatorClipPreviewEditorForm* _Ui=nullptr;
    ComponentEditContext _Context;
    GameSproutRuntime::ResourceReference _SelectedClip;
    std::string _RelatedGuid;
    std::string _RelatedSourcePath;
    std::uint64_t _RelatedRevision=0;
    ResourcePicker* _Picker=nullptr;
    std::uint64_t _PickerGeneration=0;
    bool _Loading=false,_ResumeAfterScrub=false,_HadPlayback=false;
};
