#include "Widgets/LogView.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>

LogView::LogView(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    log_edit_ = new QTextEdit(this);
    log_edit_->setReadOnly(true);

    auto* copy_button = new QPushButton("Copy logs", this);
    connect(copy_button, &QPushButton::clicked, this, &LogView::copyLogs);

    layout->addWidget(log_edit_, 1);
    layout->addWidget(copy_button, 0, Qt::AlignRight);
}

void LogView::setLogText(const QString& text) {
    log_edit_->setPlainText(text);
    log_edit_->moveCursor(QTextCursor::End);
}

void LogView::copyLogs() {
    auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return;
    }
    clipboard->setText(log_edit_->toPlainText());
}
