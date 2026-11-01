#include "AnimationClipPreviewInspector.h"
#include "ui_AnimationClipPreviewInspector.h"
#include <QHeaderView>
AnimationClipPreviewInspector::AnimationClipPreviewInspector(QWidget* parent):QWidget(parent),_Ui(new Ui::AnimationClipPreviewInspectorForm()) {
    _Ui->setupUi(this);
    _Ui->importedEventsTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    _Ui->importedEventsTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    _Ui->importedEventsTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    QObject::connect(_Ui->editClipSourceSettingsButton,&QPushButton::clicked,this,[this]{if(EditSource)EditSource();});
}
AnimationClipPreviewInspector::~AnimationClipPreviewInspector(){EditSource={};delete _Ui;}
void AnimationClipPreviewInspector::Load(const GameSproutRuntime::AnimationClipData& data) {
    _Ui->durationLabel->setText(tr("%1 s").arg(data.Duration,0,'g',7));_Ui->sampleRateLabel->setText(tr("%1 Hz").arg(data.SampleRate,0,'g',7));
    _Ui->rootMotionNodeLabel->setText(data.RootMotion.empty()?tr("未提取"):QString::fromStdString(data.RootMotionNodePath));
    _Ui->importedEventsTable->setRowCount(0);
    for(const auto& event:data.Events) {
        const int row=_Ui->importedEventsTable->rowCount();_Ui->importedEventsTable->insertRow(row);
        _Ui->importedEventsTable->setItem(row,0,new QTableWidgetItem(QString::number(event.Time,'g',7)));
        _Ui->importedEventsTable->setItem(row,1,new QTableWidgetItem(QString::fromStdString(event.Name)));
        _Ui->importedEventsTable->setItem(row,2,new QTableWidgetItem(QString::fromStdString(event.Payload)));
    }
}
