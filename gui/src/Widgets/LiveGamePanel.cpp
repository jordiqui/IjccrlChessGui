#include "Widgets/LiveGamePanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

LiveGamePanel::LiveGamePanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    status_label_ = new QLabel("Game: -", this);
    white_label_ = new QLabel("White: -", this);
    black_label_ = new QLabel("Black: -", this);
    opening_label_ = new QLabel("Opening: -", this);
    last_move_label_ = new QLabel("Last move: -", this);
    live_pgn_label_ = new QLabel("Live PGN: -", this);
    tlcs_label_ = new QLabel("TLCS mode: -", this);

    open_live_pgn_button_ = new QPushButton("Open live.pgn", this);
    connect(open_live_pgn_button_, &QPushButton::clicked, this, &LiveGamePanel::handleOpenLivePgn);

    layout->addWidget(status_label_);
    layout->addWidget(white_label_);
    layout->addWidget(black_label_);
    layout->addWidget(opening_label_);
    layout->addWidget(last_move_label_);

    auto* live_layout = new QHBoxLayout();
    live_layout->addWidget(live_pgn_label_, 1);
    live_layout->addWidget(open_live_pgn_button_, 0);
    layout->addLayout(live_layout);

    layout->addWidget(tlcs_label_);
    layout->addStretch(1);
}

void LiveGamePanel::updateState(const ijccrl::core::api::RunnerState& state, bool tlcs_enabled) {
    live_pgn_path_ = QString::fromStdString(state.livePgnPath);

    status_label_->setText(QString("Game: %1 | Round: %2 | Active: %3")
                               .arg(state.gameNo)
                               .arg(state.roundNo)
                               .arg(state.activeGames));
    white_label_->setText(QString("White: %1").arg(QString::fromStdString(state.whiteName)));
    black_label_->setText(QString("Black: %1").arg(QString::fromStdString(state.blackName)));
    opening_label_->setText(QString("Opening: %1").arg(QString::fromStdString(state.openingId)));
    last_move_label_->setText(QString("Last move: %1").arg(QString::fromStdString(state.lastMove)));
    live_pgn_label_->setText(QString("Live PGN: %1").arg(live_pgn_path_));
    if (tlcs_enabled) {
        tlcs_label_->setText("TLCS mode: writing TOURNEYPGN");
    } else {
        tlcs_label_->setText("TLCS mode: -");
    }
}

void LiveGamePanel::handleOpenLivePgn() {
    emit openLivePgnRequested(live_pgn_path_);
}
