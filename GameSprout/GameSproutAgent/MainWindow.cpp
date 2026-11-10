
#include "MainWindow.h"
#include "./ui_MainWindow.h"

#include <QEvent>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <QVariant>

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow) {
    ui->setupUi(this);
    setWindowFlag(Qt::WindowMaximizeButtonHint, false);

    ui->SplitterMain->setSizes({950, 190});
    ui->SplitterConversation->setSizes({430, 225});

    _Http = new QNetworkAccessManager(this);
    // 长输入预填充时，首个流式片段可能需要较久才返回。
    _Http->setTransferTimeout(1800000);

    ui->EditMessage->installEventFilter(this);

    connect(ui->BtnSend, &QPushButton::clicked,
            this, &MainWindow::SendMessage);

    ui->EditMessage->setFocus();
}

MainWindow::~MainWindow() {
    delete ui;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched != ui->EditMessage)
        return QMainWindow::eventFilter(watched, event);

    if (event->type() == QEvent::InputMethod) {
        const auto* inputEvent =
            static_cast<QInputMethodEvent*>(event);

        _IsComposing = !inputEvent->preeditString().isEmpty();
    }
    else if (event->type() == QEvent::FocusOut) {
        _IsComposing = false;
    }
    else if (event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);

        const bool isEnter =
            keyEvent->key() == Qt::Key_Return ||
            keyEvent->key() == Qt::Key_Enter;

        const Qt::KeyboardModifiers modifiedKeys =
            Qt::ShiftModifier | Qt::ControlModifier |
            Qt::AltModifier | Qt::MetaModifier;

        // 普通 Enter 发送；Shift+Enter 等组合键交给编辑器。
        // 输入法正在组合文字时，也交给编辑器处理。
        if (isEnter && !_IsComposing &&
            !(keyEvent->modifiers() & modifiedKeys)) {
            if (!keyEvent->isAutoRepeat())
                ui->BtnSend->click();

            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::SendMessage() {
    if (_IsSending)
        return;

    // 鼠标点击发送时，先提交输入法中尚未确认的文字。
    QGuiApplication::inputMethod()->commit();

    const QString text = ui->EditMessage->toPlainText();

    if (text.trimmed().isEmpty())
        return;

    QJsonObject body{
        {"model", "local-gpt-oss:20b"},
        {"stream", true},
        {"messages", QJsonArray{
                         QJsonObject{
                             {"role", "user"},
                             {"content", text}
                         }
                     }}
    };

    QNetworkRequest request{
        QUrl("http://127.0.0.1:9931/api/chat")
    };

    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));

    _IsSending = true;
    ui->BtnSend->setEnabled(false);
    ui->TextThinking->clear();
    ui->LabelThinkingState->setText(QStringLiteral("等待中"));
    ui->LabelChatState->setText(QStringLiteral("生成中"));

    // 每条消息显式创建段落，避免 Agent 继承用户消息的右对齐。
    QTextBlockFormat userFormat;
    userFormat.setAlignment(Qt::AlignRight);

    QTextBlockFormat answerFormat;
    answerFormat.setAlignment(Qt::AlignLeft);

    QTextCursor outputCursor(ui->TextOutput->document());
    outputCursor.movePosition(QTextCursor::End);

    if (!ui->TextOutput->document()->isEmpty())
        outputCursor.insertBlock(userFormat, QTextCharFormat());
    else {
        outputCursor.setBlockFormat(userFormat);
        outputCursor.setCharFormat(QTextCharFormat());
    }

    outputCursor.insertHtml(QStringLiteral("<b>我</b>"));
    outputCursor.insertBlock(userFormat, QTextCharFormat());
    outputCursor.insertText(text);
    outputCursor.insertBlock(answerFormat, QTextCharFormat());
    outputCursor.insertHtml(QStringLiteral("<b>Agent</b>"));
    outputCursor.insertBlock(answerFormat, QTextCharFormat());

    const int answerStart = outputCursor.position();
    ui->TextOutput->setTextCursor(outputCursor);
    ui->TextOutput->ensureCursorVisible();
    ui->EditMessage->clear();
    ui->EditMessage->setFocus();

    QNetworkReply* reply = _Http->post(
        request,
        QJsonDocument(body).toJson(QJsonDocument::Compact)
        );

    // 状态属于当前请求，供 readyRead 和 finished 共用。
    reply->setProperty("OllamaDone", false);
    reply->setProperty("StreamError", QString());
    reply->setProperty("AnswerStart", answerStart);
    reply->setProperty("AnswerContent", QString());

    auto readData = [this, reply](bool isFinished) {
        // 运行中只读完整行；结束时也处理没有末尾换行的最后一行。
        while (reply->canReadLine() ||
               (isFinished && reply->bytesAvailable() > 0)) {
            const QByteArray rawLine = reply->readLine();

            if (rawLine.isEmpty())
                break;

            const QByteArray line = rawLine.trimmed();

            if (line.isEmpty())
                continue;

            // 记录首个协议错误，后续数据只消费，不继续显示。
            if (!reply->property("StreamError").toString().isEmpty())
                continue;

            QJsonParseError parseError;
            const QJsonDocument document =
                QJsonDocument::fromJson(line, &parseError);

            if (parseError.error != QJsonParseError::NoError ||
                !document.isObject()) {
                reply->setProperty(
                    "StreamError",
                    QStringLiteral("Ollama 返回了无效的 JSON 对象。")
                    );
                continue;
            }

            const QJsonObject object = document.object();

            if (object.contains("error")) {
                QString error = object.value("error").toString();
                if (error.isEmpty())
                    error = QStringLiteral("Ollama 返回了错误。");

                reply->setProperty("StreamError", error);
                continue;
            }

            const QJsonObject message = object.value("message").toObject();
            const QString thinking = message.value("thinking").toString();
            const QString content = message.value("content").toString();

            if (!thinking.isEmpty()) {
                ui->LabelThinkingState->setText(QStringLiteral("思考中"));
                ui->LabelChatState->setText(QStringLiteral("思考中"));
                ui->TextThinking->moveCursor(QTextCursor::End);
                ui->TextThinking->insertPlainText(thinking);
                ui->TextThinking->ensureCursorVisible();
            }

            if (!content.isEmpty()) {
                // Thinking 和正式回答按 API 字段分流。
                // 正式回答也会流式到达，不能按是否逐字显示来判断阶段。
                ui->LabelThinkingState->setText(
                    ui->TextThinking->toPlainText().isEmpty()
                        ? QStringLiteral("无返回")
                        : QStringLiteral("完成")
                    );
                ui->LabelChatState->setText(QStringLiteral("回答中"));

                QString answer = reply->property("AnswerContent").toString();
                answer += content;
                reply->setProperty("AnswerContent", answer);

                ui->TextOutput->moveCursor(QTextCursor::End);
                ui->TextOutput->insertPlainText(content);
                ui->TextOutput->ensureCursorVisible();
            }

            // 最后一行也可能带文字，因此处理文字后再记录完成。
            if (object.value("done").toBool())
                reply->setProperty("OllamaDone", true);
        }
    };

    connect(reply, &QNetworkReply::readyRead, this, [readData]() {
        readData(false);
    });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, readData]() {
                readData(true);

                QString error = reply->property("StreamError").toString();
                const int statusCode =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (reply->error() != QNetworkReply::NoError) {
                    if (error.isEmpty())
                        error = reply->errorString();
                    else
                        error += QStringLiteral("（%1）").arg(reply->errorString());
                }

                if (error.isEmpty() && (statusCode < 200 || statusCode >= 300))
                    error = QStringLiteral("HTTP 状态码：%1").arg(statusCode);

                if (error.isEmpty() && !reply->property("OllamaDone").toBool())
                    error = QStringLiteral("响应中断，没有收到完成标记。");

                _IsSending = false;
                ui->BtnSend->setEnabled(true);

                if (!error.isEmpty()) {
                    ui->LabelChatState->setText(QStringLiteral("请求失败"));
                    ui->LabelThinkingState->setText(QStringLiteral("中断"));

                    ui->TextOutput->append(
                        QStringLiteral("<p align=\"left\">请求失败：%1</p>")
                            .arg(error.toHtmlEscaped())
                        );
                }
                else {
                    // 流式阶段逐字显示；完成后只把本轮回答渲染为 Markdown。
                    // 不调用 setMarkdown()，避免清掉此前的聊天记录。
                    const QString answer = reply->property("AnswerContent").toString();

                    if (!answer.isEmpty()) {
                        QTextCursor cursor(ui->TextOutput->document());
                        cursor.setPosition(reply->property("AnswerStart").toInt());
                        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
                        cursor.removeSelectedText();

                        QTextBlockFormat format;
                        format.setAlignment(Qt::AlignLeft);
                        cursor.setBlockFormat(format);
                        cursor.setCharFormat(QTextCharFormat());
                        cursor.insertMarkdown(
                            answer,
                            QTextDocument::MarkdownDialectGitHub
                        );

                        ui->TextOutput->setTextCursor(cursor);
                        ui->TextOutput->ensureCursorVisible();
                    }

                    ui->LabelChatState->setText(QStringLiteral("待发送"));
                    ui->LabelThinkingState->setText(
                        ui->TextThinking->toPlainText().isEmpty()
                            ? QStringLiteral("无返回")
                            : QStringLiteral("完成")
                        );
                }

                reply->deleteLater();
            });
}
