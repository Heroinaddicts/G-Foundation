#pragma once
#include <QObject>
#include <QPointer>
#include <functional>
#include <string>

class IIDEDocker;
class QWidget;
class QEvent;
class QScrollArea;
namespace Ui {class SceneViewUIWindow;class SceneTitleActions;class SceneSnapPopup;class SceneStatisticsPopup;}

// View-only presentation: compact toolbar, title actions and transient panels.
// Never queries Module/Runtime state or installs a timer.
class SceneViewChrome final : public QObject {
public:
    SceneViewChrome(QWidget* owner,Ui::SceneViewUIWindow& window);
    ~SceneViewChrome() override;
    bool Attach(IIDEDocker* docker);
    void Detach();
    void SetStatus(const std::string& status,const std::string& diagnostic);
    void SetEditingEnabled(bool enabled);
    bool SnapEnabled() const;
    float MoveSnap() const;
    float RotateSnap() const;
    float ScaleSnap() const;
    std::function<void()> SnapChanged;
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    void ShowPopup(QWidget*,QWidget* anchor);
    void RefreshStatistics();
    void UpdateToolbarHeight();
    Ui::SceneViewUIWindow& _Window;
    Ui::SceneTitleActions* _TitleUi=nullptr;
    Ui::SceneSnapPopup* _SnapUi=nullptr;
    Ui::SceneStatisticsPopup* _StatisticsUi=nullptr;
    QPointer<QWidget> _Title,_Snap,_Statistics;
    QPointer<QScrollArea> _Strip;
    QPointer<QWidget> _StripViewport,_ToolContents;
    IIDEDocker* _Docker=nullptr;
    QString _Status,_Diagnostic;
};
