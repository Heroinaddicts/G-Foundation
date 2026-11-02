#include "SceneViewChrome.h"
#include "IIDEWindow.h"
#include "ui_SceneViewUI.h"
#include "ui_SceneTitleActions.h"
#include "ui_SceneSnapPopup.h"
#include "ui_SceneStatisticsPopup.h"
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QStyle>
#include <QToolTip>

SceneViewChrome::SceneViewChrome(QWidget* owner,Ui::SceneViewUIWindow& window):_Window(window) {
    // The IDE may destroy its host before the View receives Release. Do not
    // retain a borrowed Docker interface past the content widget's lifetime.
    QObject::connect(owner,&QObject::destroyed,this,[this]{_Docker=nullptr;});
    _Strip=_Window.toolScrollArea;_StripViewport=_Strip->viewport();_ToolContents=_Window.toolContents;
    _Title=new QWidget(owner);_TitleUi=new Ui::SceneTitleActions();_TitleUi->setupUi(_Title);
    _Snap=new QWidget(owner,Qt::Popup);_SnapUi=new Ui::SceneSnapPopup();_SnapUi->setupUi(_Snap);_Snap->hide();
    _Statistics=new QWidget(owner,Qt::Popup);_StatisticsUi=new Ui::SceneStatisticsPopup();_StatisticsUi->setupUi(_Statistics);_Statistics->hide();
    _Snap->installEventFilter(this);_Statistics->installEventFilter(this);
    for(auto* child:_Snap->findChildren<QWidget*>())child->installEventFilter(this);
    for(auto* child:_Statistics->findChildren<QWidget*>())child->installEventFilter(this);
    _Window.toolScrollArea->installEventFilter(this);
    _Window.toolScrollArea->viewport()->installEventFilter(this);
    QObject::connect(_TitleUi->sceneHelpButton,&QToolButton::clicked,this,[this] {
        QToolTip::showText(QCursor::pos(),_TitleUi->sceneHelpButton->toolTip(),_TitleUi->sceneHelpButton);
    });
    QObject::connect(_TitleUi->statisticsButton,&QToolButton::clicked,this,[this] {
        if(_Statistics->isVisible()){_Statistics->hide();return;}
        RefreshStatistics();ShowPopup(_Statistics,_TitleUi->statisticsButton);
    });
    QObject::connect(_Window.snapSettingsButton,&QToolButton::clicked,this,[this] {
        if(_Snap->isVisible()){_Snap->hide();return;}
        ShowPopup(_Snap,_Window.snapSettingsButton);
    });
    const auto changed=[this]{if(SnapChanged)SnapChanged();};
    QObject::connect(_SnapUi->snapCheck,&QCheckBox::toggled,this,[changed](bool){changed();});
    for(auto* spin:{_SnapUi->moveSnapSpin,_SnapUi->rotateSnapSpin,_SnapUi->scaleSnapSpin})
        QObject::connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[changed](double){changed();});
    _Title->hide();UpdateToolbarHeight();
}

SceneViewChrome::~SceneViewChrome() {
    Detach();
    delete _Title;delete _Snap;delete _Statistics;
    delete _TitleUi;delete _SnapUi;delete _StatisticsUi;
}
bool SceneViewChrome::Attach(IIDEDocker* docker) {
    if(_Docker)return _Docker==docker;
    if(!docker||!_Title||!docker->AttachTitleBarWidget(_Title))return false;
    _Docker=docker;return true;
}
void SceneViewChrome::Detach() {
    SnapChanged={};
    if(_Snap)_Snap->hide();if(_Statistics)_Statistics->hide();
    if(_Docker&&_Title)_Docker->RemoveTitleBarWidget(_Title);
    _Docker=nullptr;
}
bool SceneViewChrome::SnapEnabled()const{return _SnapUi->snapCheck->isChecked();}
float SceneViewChrome::MoveSnap()const{return float(_SnapUi->moveSnapSpin->value());}
float SceneViewChrome::RotateSnap()const{return float(_SnapUi->rotateSnapSpin->value());}
float SceneViewChrome::ScaleSnap()const{return float(_SnapUi->scaleSnapSpin->value());}
void SceneViewChrome::SetEditingEnabled(bool enabled) {
    if(_Snap)_Snap->setEnabled(enabled);
    _Window.snapSettingsButton->setEnabled(enabled);
}
void SceneViewChrome::SetStatus(const std::string& status,const std::string& diagnostic) {
    _Status=QString::fromStdString(status);
    const QString previous=_Diagnostic;_Diagnostic=QString::fromStdString(diagnostic);
    if(previous!=_Diagnostic&&_Title) {
        auto* button=_TitleUi->statisticsButton;
        button->setProperty("diagnostic",!_Diagnostic.isEmpty());
        button->setText(_Diagnostic.isEmpty()?QStringLiteral("统计"):QStringLiteral("!"));
        button->setToolTip(_Diagnostic.isEmpty()?QStringLiteral("查看当前视口渲染统计与诊断"):_Diagnostic);
        button->style()->unpolish(button);button->style()->polish(button);button->update();
    }
    if(_Statistics&&_Statistics->isVisible())RefreshStatistics();
}
void SceneViewChrome::RefreshStatistics() {
    if(!_Statistics)return;
    QString stats=_Status;stats.replace(QStringLiteral(" · "),QStringLiteral("\n"));
    _StatisticsUi->statisticsLabel->setText(stats.isEmpty()?QStringLiteral("等待渲染…"):stats);
    _StatisticsUi->statisticsLabel->setVisible(_Diagnostic.isEmpty()||_Status!=_Diagnostic);
    _StatisticsUi->diagnosticsLabel->setText(_Diagnostic);
    _StatisticsUi->diagnosticsLabel->setVisible(!_Diagnostic.isEmpty());
}
void SceneViewChrome::ShowPopup(QWidget* popup,QWidget* anchor) {
    popup->adjustSize();
    QPoint position=anchor->mapToGlobal(QPoint(0,anchor->height()));
    if(auto* screen=anchor->screen()) {
        const QRect area=screen->availableGeometry();
        position.setX(qBound(area.left(),position.x(),qMax(area.left(),area.right()-popup->width()+1)));
        position.setY(qBound(area.top(),position.y(),qMax(area.top(),area.bottom()-popup->height()+1)));
    }
    popup->move(position);popup->show();popup->raise();popup->setFocus();
}
void SceneViewChrome::UpdateToolbarHeight() {
    if(!_Strip||!_StripViewport||!_ToolContents)return;
    const bool overflow=_ToolContents->minimumSizeHint().width()>_StripViewport->width();
    const int height=_Strip->property(overflow?"overflowHeight":"compactHeight").toInt();
    if(height>0&&_Strip->height()!=height)_Strip->setFixedHeight(height);
}
bool SceneViewChrome::eventFilter(QObject* watched,QEvent* event) {
    if(event->type()==QEvent::Resize&&(watched==_Strip||watched==_StripViewport))UpdateToolbarHeight();
    if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) {
        auto* widget=qobject_cast<QWidget*>(watched);
        if(_Snap&&(widget==_Snap||_Snap->isAncestorOf(widget))){_Snap->hide();return true;}
        if(_Statistics&&(widget==_Statistics||_Statistics->isAncestorOf(widget))){_Statistics->hide();return true;}
    }
    return QObject::eventFilter(watched,event);
}
