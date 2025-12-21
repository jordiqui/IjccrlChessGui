#pragma once

#include <QTextEdit>
#include <QWidget>

class LogView final : public QWidget {
    Q_OBJECT

public:
    explicit LogView(QWidget* parent = nullptr);

    void setLogText(const QString& text);

private slots:
    void copyLogs();

private:
    QTextEdit* log_edit_ = nullptr;
};
