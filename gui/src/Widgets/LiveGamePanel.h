#pragma once

#include "ijccrl/core/api/RunnerService.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QWidget>

class LiveGamePanel final : public QWidget {
    Q_OBJECT

public:
    explicit LiveGamePanel(QWidget* parent = nullptr);

    void updateState(const ijccrl::core::api::RunnerState& state, bool tlcs_enabled);

signals:
    void openLivePgnRequested(const QString& path);

private slots:
    void handleOpenLivePgn();

private:
    QLabel* status_label_ = nullptr;
    QLabel* white_label_ = nullptr;
    QLabel* black_label_ = nullptr;
    QLabel* opening_label_ = nullptr;
    QLabel* last_move_label_ = nullptr;
    QLabel* termination_label_ = nullptr;
    QLabel* tablebase_label_ = nullptr;
    QLabel* live_pgn_label_ = nullptr;
    QLabel* tlcs_label_ = nullptr;
    QListWidget* pairings_list_ = nullptr;
    QPushButton* open_live_pgn_button_ = nullptr;
    QString live_pgn_path_;
};
