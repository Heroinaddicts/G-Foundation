#include "ConsoleUI.h"
#include "ConsoleLogDelegate.h"
#include "ConsoleFilterPopup.h"
#include "IIDEWindow.h"
#include "ui_ConsoleUI.h"
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QHeaderView>
#include <QMainWindow>
#include <QScrollBar>
#include <QStringList>
#include <QTime>
#include <QToolButton>

ConsoleUI::~ConsoleUI() {
    Release(nullptr);
}

void ConsoleUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) {
    GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
    GSRegInitliazedCallback(g_IIDEWindow, [&](GameSproutEditor::GameSproutViewApi * const editor) {
        if (_Dispatch) {
            return _Window != nullptr;
        }
        if (!qobject_cast<QApplication*>(QCoreApplication::instance())) {
            return false;
        }
        g_GameSproutEditor = editor;
        try {
            _Dispatch = new QObject();
            _Window = new QMainWindow();
            _Ui = new Ui::ConsoleUIWindow();
            _Ui->setupUi(_Window);
            _Filters = new ConsoleFilterPopup(_Window);
            QObject::connect(_Window, &QObject::destroyed, _Dispatch, [this]() {
                _Window = nullptr;
                _Filters = nullptr;
                _Docker = nullptr;
                _Attached = false;
            });
            _Ui->logList->setItemDelegate(new ConsoleLogDelegate(_Ui->logList));
            _Ui->logList->header()->resizeSection(0, _Window->property("timeColumnWidth").toInt());
            _Ui->logList->header()->resizeSection(1, _Window->property("levelColumnWidth").toInt());
            QObject::connect(_Ui->clearButton, &QToolButton::clicked, _Dispatch,
                             [this]() { ClearLogs(); });
            QObject::connect(_Ui->searchEdit, &QLineEdit::textChanged, _Dispatch,
                             [this]() { ApplyFilters(); });
            QObject::connect(_Ui->filterButton, &QToolButton::clicked, _Dispatch,
                             [this]() { _Filters->ShowBelow(_Ui->filterButton); });
            for (int index = 0; index < 4; ++index) {
                QObject::connect(LevelCheckBox(static_cast<eConsoleLog>(index)),
                                 &QCheckBox::toggled, _Dispatch, [this]() {
                                     ApplyFilters();
                                     UpdateFilterSummary();
                                 });
            }
            QObject::connect(_Ui->logList, &QTreeWidget::currentItemChanged, _Dispatch,
                             [this]() { UpdateDetails(); });
            UpdateCounters();
            UpdateFilterSummary();
        } catch (...) {
            Release(nullptr);
            throw;
            return false;
        }

        return true;
    });

    GSRegLaunchedCallback(g_IIDEWindow, [&](GameSproutEditor::GameSproutViewApi * const editor) {
        if (_Attached) {
            return true;
        }
        _Docker = g_IIDEWindow->CreateDocker(QStringLiteral("Console"));
        if (!_Docker || !_Docker->AttachWidget(_Window)) {
            return false;
        }
        _Attached = true;
        return true;
    });
}

bool ConsoleUI::Initialize(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

bool ConsoleUI::Launch(GameSproutEditor::GameSproutViewApi * const editor) {
    return true;
}

void ConsoleUI::LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) {
    Q_UNUSED(editor);
}

void ConsoleUI::ConsoleLog(const char * file, const int line, const eConsoleLog e, const std::string & log) {
    const QString message = QString::fromUtf8(log.data(), static_cast<int>(log.size()));
    const QString sourceFile = QString::fromUtf8(file);
    const QString location = sourceFile.isEmpty() ? QString()
        : QStringLiteral("%1:%2").arg(sourceFile).arg(line);
    qCritical().noquote() << (location.isEmpty() ? message
        : QStringLiteral("%1 >> %2").arg(location, message));
    if (!_Dispatch) {
        return;
    }
    QMetaObject::invokeMethod(_Dispatch, [this, e, message, location]() {
        AppendLog(e, message, location);
    }, Qt::AutoConnection);
}

void ConsoleUI::Show() {
    if (_Docker) {
        _Docker->Activate();
    }
}

void ConsoleUI::Hide() {
    if (_Filters) {
        _Filters->hide();
    }
    if (_Window) {
        if (auto* dock = qobject_cast<QDockWidget*>(_Window->parentWidget())) {
            dock->hide();
        } else {
            _Window->hide();
        }
    }
}

void ConsoleUI::Update(GameSproutEditor::GameSproutViewApi * const editor) {
}

void ConsoleUI::Release(GameSproutEditor::GameSproutViewApi * const editor) {
    Q_UNUSED(editor);
    // Cancel queued logs and UI callbacks before releasing their state.
    delete _Dispatch;
    _Dispatch = nullptr;
    delete _Filters;
    _Filters = nullptr;
    if (_Window) {
        if (_Attached) {
            _Window->hide(); // The dock owns the attached widget.
        } else {
            delete _Window;
        }
    }
    _Window = nullptr;
    _Docker = nullptr;
    _Attached = false;
    delete _Ui;
    _Ui = nullptr;
    _Counts.fill(0);
}

QCheckBox* ConsoleUI::LevelCheckBox(eConsoleLog level) const {
    return _Filters ? _Filters->LevelCheckBox(level) : nullptr;
}

bool ConsoleUI::MatchesFilters(eConsoleLog level, const QString& message, const QString& location) const {
    const QCheckBox* button = LevelCheckBox(level);
    return button && button->isChecked() &&
        (message.contains(_Ui->searchEdit->text(), Qt::CaseInsensitive) ||
         location.contains(_Ui->searchEdit->text(), Qt::CaseInsensitive));
}

void ConsoleUI::AppendLog(eConsoleLog level, const QString& message, const QString& location) {
    QCheckBox* button = LevelCheckBox(level);
    if (!_Window || !button) {
        return;
    }
    QTreeWidget* list = _Ui->logList;
    const QScrollBar* scroll = list->verticalScrollBar();
    const bool follow = _Ui->followButton->isChecked() && scroll->value() >= scroll->maximum();
    const int maximum = qMax(1, _Window->property("maximumLogEntries").toInt());
    while (list->topLevelItemCount() >= maximum) {
        QTreeWidgetItem* oldest = list->takeTopLevelItem(0);
        --_Counts[oldest->data(0, Qt::UserRole).toInt()];
        delete oldest;
    }

    auto* item = new QTreeWidgetItem(list);
    item->setText(0, QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")));
    item->setText(1, button->property("levelName").toString());
    item->setText(2, message.section(QLatin1Char('\n'), 0, 0).remove(QLatin1Char('\r')));
    item->setToolTip(2, message);
    item->setData(0, Qt::UserRole, static_cast<int>(level));
    item->setData(0, Qt::UserRole + 1, message);
    item->setData(0, Qt::UserRole + 2, location);
    const QColor color = button->property("logColor").value<QColor>();
    for (int column = 0; column < list->columnCount(); ++column) {
        item->setForeground(column, color);
    }
    item->setHidden(!MatchesFilters(level, message, location));
    ++_Counts[static_cast<int>(level)];
    UpdateCounters();
    if (follow && !item->isHidden()) {
        list->scrollToItem(item, QAbstractItemView::PositionAtBottom);
    }
}

void ConsoleUI::ApplyFilters() {
    if (!_Window) {
        return;
    }
    for (int index = 0; index < _Ui->logList->topLevelItemCount(); ++index) {
        QTreeWidgetItem* item = _Ui->logList->topLevelItem(index);
        item->setHidden(!MatchesFilters(
            static_cast<eConsoleLog>(item->data(0, Qt::UserRole).toInt()),
            item->data(0, Qt::UserRole + 1).toString(),
            item->data(0, Qt::UserRole + 2).toString()));
    }
    if (_Ui->logList->currentItem() && _Ui->logList->currentItem()->isHidden()) {
        _Ui->logList->setCurrentItem(nullptr);
    }
}

void ConsoleUI::UpdateCounters() {
    for (int index = 0; index < 4; ++index) {
        QCheckBox* button = LevelCheckBox(static_cast<eConsoleLog>(index));
        button->setText(QStringLiteral("%1 %2")
            .arg(button->property("levelName").toString()).arg(_Counts[index]));
    }
    _Ui->summaryLabel->setText(QStringLiteral("%1 条日志")
        .arg(_Ui->logList->topLevelItemCount()));
}

void ConsoleUI::UpdateFilterSummary() {
    int selected = 0;
    QStringList names;
    for (int index = 0; index < 4; ++index) {
        const QCheckBox* button = LevelCheckBox(static_cast<eConsoleLog>(index));
        if (button->isChecked()) {
            ++selected;
            names.append(button->property("levelName").toString());
        }
    }
    _Ui->filterButton->setText(selected == 4 ? QStringLiteral("全部级别") : QStringLiteral("级别 %1/4").arg(selected));
    _Ui->filterButton->setToolTip(selected == 0 ? QStringLiteral("未选择日志级别，点击勾选（可多选）")
        : QStringLiteral("显示：%1（可多选）").arg(names.join(QStringLiteral("、"))));
}

void ConsoleUI::UpdateDetails() {
    const QTreeWidgetItem* item = _Ui->logList->currentItem();
    _Ui->detailText->setPlainText(item
        ? item->data(0, Qt::UserRole + 1).toString() : QString());
    _Ui->detailLabel->setText(item
        ? QStringLiteral("[%1] %2").arg(item->text(1), item->text(0))
        : _Ui->detailLabel->property("emptyText").toString());
    const QString location = item ? item->data(0, Qt::UserRole + 2).toString() : QString();
    _Ui->sourceLocationLabel->setText(location);
    _Ui->sourceLocationLabel->setToolTip(location);
}

void ConsoleUI::ClearLogs() {
    _Ui->logList->clear();
    _Counts.fill(0);
    UpdateDetails();
    UpdateCounters();
}
