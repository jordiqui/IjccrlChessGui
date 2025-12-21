#pragma once

#include "Models/EnginesModel.h"
#include "Models/StandingsModel.h"
#include "Widgets/LiveGamePanel.h"
#include "Widgets/LogView.h"
#include "ijccrl/core/api/RunnerService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QSpinBox>
#include <QTableView>
#include <QTimer>

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void newProfile();
    void openProfile();
    void saveProfile();
    void openRecentProfile();
    void startRunner();
    void pauseRunner();
    void resumeRunner();
    void stopRunner();
    void resumeTournament();
    void exportResults();
    void openOutputFolder();
    void updateLiveView();
    void updateLogInterval(int value);
    void handleOpenLivePgn(const QString& path);

private:
    void createUi();
    void createMenus();
    void refreshRecentProfiles();
    void addRecentProfile(const QString& path);
    bool validateConfig(const ijccrl::core::api::RunnerConfig& config);
    ijccrl::core::api::RunnerConfig buildConfigFromUi() const;
    void applyConfigToUi(const ijccrl::core::api::RunnerConfig& config);
    void updateTournamentOptions();

    QString current_profile_path_;
    QString output_dir_ = "out";

    EnginesModel* engines_model_ = nullptr;
    StandingsModel* standings_model_ = nullptr;

    QComboBox* tournament_mode_ = nullptr;
    QCheckBox* double_rr_ = nullptr;
    QSpinBox* rounds_spin_ = nullptr;
    QSpinBox* games_per_pairing_ = nullptr;
    QSpinBox* concurrency_spin_ = nullptr;
    QCheckBox* avoid_repeats_ = nullptr;
    QCheckBox* bye_points_ = nullptr;

    QSpinBox* base_seconds_spin_ = nullptr;
    QSpinBox* increment_seconds_spin_ = nullptr;

    QComboBox* openings_type_ = nullptr;
    QLineEdit* openings_path_ = nullptr;
    QComboBox* openings_policy_ = nullptr;
    QSpinBox* openings_seed_ = nullptr;

    QLineEdit* server_ini_path_ = nullptr;
    QLineEdit* output_dir_edit_ = nullptr;

    LiveGamePanel* live_panel_ = nullptr;
    QTableView* standings_view_ = nullptr;
    LogView* log_view_ = nullptr;

    QSpinBox* log_refresh_spin_ = nullptr;

    QTimer* refresh_timer_ = nullptr;
    QMenu* recent_menu_ = nullptr;

    QAction* start_action_ = nullptr;
    QAction* pause_action_ = nullptr;
    QAction* resume_action_ = nullptr;
    QAction* stop_action_ = nullptr;
    QAction* resume_tournament_action_ = nullptr;
    QAction* export_results_action_ = nullptr;

    ijccrl::core::api::RunnerService runner_service_;
};
