#include "MainWindow.h"
#include "TournamentWizard.h"

#include "ijccrl/core/persist/CheckpointState.h"

#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {

QStringList SplitOptions(const QString& text) {
    return text.split(QRegularExpression("[;\\n]"), Qt::SkipEmptyParts);
}

bool ContainsTourneyPgn(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    while (!file.atEnd()) {
        const auto line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith('#') || line.startsWith(';') || line.isEmpty()) {
            continue;
        }
        const auto parts = line.split('=');
        if (parts.size() < 2) {
            continue;
        }
        if (parts[0].trimmed() == "TOURNEYPGN" && !parts[1].trimmed().isEmpty()) {
            return true;
        }
    }
    return false;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    createUi();
    createMenus();

    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(750);
    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::updateLiveView);
    refresh_timer_->start();

    updateLiveView();
}

void MainWindow::createUi() {
    setWindowTitle("Ijccrl Chess GUI");

    auto* toolbar = addToolBar("Main");
    toolbar->setMovable(false);

    auto* new_action = toolbar->addAction("New");
    auto* open_action = toolbar->addAction("Open");
    auto* save_action = toolbar->addAction("Save Profile");
    toolbar->addSeparator();
    start_action_ = toolbar->addAction("Start");
    resume_tournament_action_ = toolbar->addAction("Resume Tournament");
    pause_action_ = toolbar->addAction("Pause");
    resume_action_ = toolbar->addAction("Resume");
    stop_action_ = toolbar->addAction("Stop");
    toolbar->addSeparator();
    export_results_action_ = toolbar->addAction("Export Results");
    auto* open_output_action = toolbar->addAction("Open Output Folder");

    connect(new_action, &QAction::triggered, this, &MainWindow::newProfile);
    connect(open_action, &QAction::triggered, this, &MainWindow::openProfile);
    connect(save_action, &QAction::triggered, this, &MainWindow::saveProfile);
    connect(start_action_, &QAction::triggered, this, &MainWindow::startRunner);
    connect(resume_tournament_action_, &QAction::triggered, this, &MainWindow::resumeTournament);
    connect(pause_action_, &QAction::triggered, this, &MainWindow::pauseRunner);
    connect(resume_action_, &QAction::triggered, this, &MainWindow::resumeRunner);
    connect(stop_action_, &QAction::triggered, this, &MainWindow::stopRunner);
    connect(export_results_action_, &QAction::triggered, this, &MainWindow::exportResults);
    connect(open_output_action, &QAction::triggered, this, &MainWindow::openOutputFolder);

    auto* tabs = new QTabWidget(this);

    // Setup tab
    auto* setup_tab = new QWidget(this);
    auto* setup_layout = new QVBoxLayout(setup_tab);

    engines_model_ = new EnginesModel(this);
    auto* engines_view = new QTableView(setup_tab);
    engines_view->setModel(engines_model_);
    engines_view->horizontalHeader()->setStretchLastSection(true);
    engines_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    setup_layout->addWidget(engines_view);

    auto* engine_buttons = new QHBoxLayout();
    auto* add_engine = new QPushButton("Add Engine", setup_tab);
    auto* remove_engine = new QPushButton("Remove Selected", setup_tab);
    engine_buttons->addWidget(add_engine);
    engine_buttons->addWidget(remove_engine);
    engine_buttons->addStretch(1);
    setup_layout->addLayout(engine_buttons);

    connect(add_engine, &QPushButton::clicked, engines_model_, &EnginesModel::addEmptyRow);
    connect(remove_engine, &QPushButton::clicked, [this, engines_view]() {
        const auto index = engines_view->currentIndex();
        if (index.isValid()) {
            engines_model_->removeRowAt(index.row());
        }
    });

    auto* options_layout = new QFormLayout();

    tournament_mode_ = new QComboBox(setup_tab);
    tournament_mode_->addItem("Round Robin (RR)", "round_robin");
    tournament_mode_->addItem("Gauntlet", "gauntlet");
    tournament_mode_->addItem("Swiss", "swiss");
    tournament_mode_->addItem("H2H", "h2h");
    tournament_mode_->setCurrentIndex(0);
    tournament_mode_->setItemData(1, 0, Qt::UserRole - 1);
    tournament_mode_->setItemData(3, 0, Qt::UserRole - 1);
    connect(tournament_mode_, qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &MainWindow::updateTournamentOptions);

    double_rr_ = new QCheckBox("Double round robin", setup_tab);
    rounds_spin_ = new QSpinBox(setup_tab);
    rounds_spin_->setRange(1, 200);
    rounds_spin_->setValue(1);
    games_per_pairing_ = new QSpinBox(setup_tab);
    games_per_pairing_->setRange(1, 1000);
    games_per_pairing_->setValue(1);
    avoid_repeats_ = new QCheckBox("Avoid repeats", setup_tab);
    avoid_repeats_->setChecked(true);
    bye_points_ = new QCheckBox("Bye = 1 point", setup_tab);
    bye_points_->setChecked(true);

    concurrency_spin_ = new QSpinBox(setup_tab);
    concurrency_spin_->setRange(1, 128);
    concurrency_spin_->setValue(1);

    base_seconds_spin_ = new QSpinBox(setup_tab);
    base_seconds_spin_->setRange(1, 36000);
    base_seconds_spin_->setValue(60);

    increment_seconds_spin_ = new QSpinBox(setup_tab);
    increment_seconds_spin_->setRange(0, 36000);
    increment_seconds_spin_->setValue(0);

    openings_type_ = new QComboBox(setup_tab);
    openings_type_->addItem("EPD", "epd");
    openings_type_->addItem("PGN", "pgn");

    openings_path_ = new QLineEdit(setup_tab);
    auto* openings_browse = new QPushButton("Browse", setup_tab);
    connect(openings_browse, &QPushButton::clicked, [this]() {
        const auto path = QFileDialog::getOpenFileName(this, "Select openings suite", "", "Suites (*.epd *.pgn);;All files (*)");
        if (!path.isEmpty()) {
            openings_path_->setText(path);
        }
    });

    openings_policy_ = new QComboBox(setup_tab);
    openings_policy_->addItem("Round Robin", "round_robin");
    openings_policy_->addItem("Random", "random");

    openings_seed_ = new QSpinBox(setup_tab);
    openings_seed_->setRange(0, 9999999);

    server_ini_path_ = new QLineEdit(setup_tab);
    auto* server_browse = new QPushButton("Browse", setup_tab);
    connect(server_browse, &QPushButton::clicked, [this]() {
        const auto path = QFileDialog::getOpenFileName(this, "Select server.ini", "", "INI files (*.ini);;All files (*)");
        if (!path.isEmpty()) {
            server_ini_path_->setText(path);
        }
    });

    output_dir_edit_ = new QLineEdit(output_dir_, setup_tab);
    auto* output_browse = new QPushButton("Browse", setup_tab);
    connect(output_browse, &QPushButton::clicked, [this]() {
        const auto path = QFileDialog::getExistingDirectory(this, "Select output directory", output_dir_);
        if (!path.isEmpty()) {
            output_dir_edit_->setText(path);
        }
    });

    auto* openings_path_row = new QHBoxLayout();
    openings_path_row->addWidget(openings_path_, 1);
    openings_path_row->addWidget(openings_browse);

    auto* server_row = new QHBoxLayout();
    server_row->addWidget(server_ini_path_, 1);
    server_row->addWidget(server_browse);

    auto* output_row = new QHBoxLayout();
    output_row->addWidget(output_dir_edit_, 1);
    output_row->addWidget(output_browse);

    options_layout->addRow("Tournament mode", tournament_mode_);
    options_layout->addRow("Double RR", double_rr_);
    options_layout->addRow("Rounds", rounds_spin_);
    options_layout->addRow("Games per pairing", games_per_pairing_);
    options_layout->addRow("Concurrency", concurrency_spin_);
    options_layout->addRow("", avoid_repeats_);
    options_layout->addRow("", bye_points_);
    options_layout->addRow("TC base (sec)", base_seconds_spin_);
    options_layout->addRow("TC increment (sec)", increment_seconds_spin_);
    options_layout->addRow("Openings type", openings_type_);
    options_layout->addRow("Openings suite", openings_path_row);
    options_layout->addRow("Openings policy", openings_policy_);
    options_layout->addRow("Openings seed", openings_seed_);
    options_layout->addRow("TLCS server.ini", server_row);
    options_layout->addRow("Output dir", output_row);

    setup_layout->addLayout(options_layout);
    setup_layout->addStretch(1);
    updateTournamentOptions();

    // Live tab
    auto* live_tab = new QWidget(this);
    auto* live_layout = new QVBoxLayout(live_tab);

    live_panel_ = new LiveGamePanel(live_tab);
    connect(live_panel_, &LiveGamePanel::openLivePgnRequested, this, &MainWindow::handleOpenLivePgn);
    live_layout->addWidget(live_panel_);

    standings_model_ = new StandingsModel(this);
    standings_view_ = new QTableView(live_tab);
    standings_view_->setModel(standings_model_);
    standings_view_->horizontalHeader()->setStretchLastSection(true);
    standings_view_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    live_layout->addWidget(standings_view_);

    // Logs tab
    auto* logs_tab = new QWidget(this);
    auto* logs_layout = new QVBoxLayout(logs_tab);
    log_view_ = new LogView(logs_tab);
    logs_layout->addWidget(log_view_, 1);

    auto* log_controls = new QHBoxLayout();
    log_controls->addWidget(new QLabel("Refresh (ms)", logs_tab));
    log_refresh_spin_ = new QSpinBox(logs_tab);
    log_refresh_spin_->setRange(250, 5000);
    log_refresh_spin_->setValue(750);
    log_controls->addWidget(log_refresh_spin_);
    log_controls->addStretch(1);
    logs_layout->addLayout(log_controls);

    connect(log_refresh_spin_, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::updateLogInterval);

    tabs->addTab(setup_tab, "Setup");
    tabs->addTab(live_tab, "Live");
    tabs->addTab(logs_tab, "Logs");

    setCentralWidget(tabs);
}

void MainWindow::createMenus() {
    auto* file_menu = menuBar()->addMenu("File");
    auto* new_action = file_menu->addAction("New Profile");
    auto* open_action = file_menu->addAction("Open Profile");
    auto* save_action = file_menu->addAction("Save Profile");
    recent_menu_ = file_menu->addMenu("Recent Profiles");
    file_menu->addSeparator();
    auto* exit_action = file_menu->addAction("Exit");

    connect(new_action, &QAction::triggered, this, &MainWindow::newProfile);
    connect(open_action, &QAction::triggered, this, &MainWindow::openProfile);
    connect(save_action, &QAction::triggered, this, &MainWindow::saveProfile);
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    refreshRecentProfiles();
}

void MainWindow::updateTournamentOptions() {
    const bool is_swiss = tournament_mode_->currentData().toString() == "swiss";
    double_rr_->setEnabled(!is_swiss);
    rounds_spin_->setEnabled(is_swiss);
    avoid_repeats_->setEnabled(is_swiss);
    bye_points_->setEnabled(is_swiss);
}

void MainWindow::refreshRecentProfiles() {
    recent_menu_->clear();
    QSettings settings;
    const auto list = settings.value("recentProfiles").toStringList();
    for (const auto& entry : list) {
        auto* action = recent_menu_->addAction(entry);
        connect(action, &QAction::triggered, this, &MainWindow::openRecentProfile);
    }
    if (list.isEmpty()) {
        recent_menu_->addAction("(None)")->setEnabled(false);
    }
}

void MainWindow::addRecentProfile(const QString& path) {
    QSettings settings;
    auto list = settings.value("recentProfiles").toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 8) {
        list.removeLast();
    }
    settings.setValue("recentProfiles", list);
    refreshRecentProfiles();
}

void MainWindow::newProfile() {
    TournamentWizard wizard(this);
    wizard.exec();

    ijccrl::core::api::RunnerConfig config;
    applyConfigToUi(config);
    current_profile_path_.clear();
}

void MainWindow::openProfile() {
    const auto path = QFileDialog::getOpenFileName(this,
                                                   "Open Profile",
                                                   "",
                                                   "IJCCRL profiles (*.ijccrl.json);;JSON files (*.json);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    if (!runner_service_.loadConfig(path.toStdString())) {
        QMessageBox::warning(this, "Load failed", "Failed to load profile.");
        return;
    }

    applyConfigToUi(runner_service_.getConfigSnapshot());
    current_profile_path_ = path;
    addRecentProfile(path);
}

void MainWindow::saveProfile() {
    if (current_profile_path_.isEmpty()) {
        current_profile_path_ = QFileDialog::getSaveFileName(this,
                                                             "Save Profile",
                                                             "profile.ijccrl.json",
                                                             "IJCCRL profiles (*.ijccrl.json)");
    }
    if (current_profile_path_.isEmpty()) {
        return;
    }

    auto config = buildConfigFromUi();
    runner_service_.setConfig(config);
    if (!runner_service_.saveConfig(current_profile_path_.toStdString())) {
        QMessageBox::warning(this, "Save failed", "Failed to save profile.");
        return;
    }
    addRecentProfile(current_profile_path_);
}

void MainWindow::openRecentProfile() {
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }
    const auto path = action->text();
    if (path == "(None)") {
        return;
    }

    if (!runner_service_.loadConfig(path.toStdString())) {
        QMessageBox::warning(this, "Load failed", "Failed to load profile.");
        return;
    }

    applyConfigToUi(runner_service_.getConfigSnapshot());
    current_profile_path_ = path;
}

void MainWindow::startRunner() {
    auto config = buildConfigFromUi();
    runner_service_.setConfig(config);

    if (!validateConfig(config)) {
        return;
    }

    if (!runner_service_.start()) {
        QMessageBox::information(this, "Runner", "Runner already active.");
    }
}

void MainWindow::pauseRunner() {
    runner_service_.pause();
}

void MainWindow::resumeRunner() {
    runner_service_.resume();
}

void MainWindow::stopRunner() {
    runner_service_.requestStop();
}

void MainWindow::resumeTournament() {
    auto config = buildConfigFromUi();
    runner_service_.setConfig(config);

    const QString checkpoint_path = QString::fromStdString(config.output.checkpoint_json);
    const QFileInfo checkpoint_info(checkpoint_path);
    if (!checkpoint_info.exists()) {
        QMessageBox::information(this, "Resume", "No checkpoint.json found in the output directory.");
        return;
    }

    ijccrl::core::persist::CheckpointState checkpoint;
    std::string error;
    if (!ijccrl::core::persist::LoadCheckpoint(checkpoint_path.toStdString(), checkpoint, &error)) {
        QMessageBox::warning(this, "Resume", QString::fromStdString(error));
        return;
    }

    const std::string config_hash =
        ijccrl::core::persist::ComputeConfigHash(ijccrl::core::api::RunnerConfig::ToJsonString(config));
    if (checkpoint.config_hash != config_hash) {
        QMessageBox::warning(this,
                             "Resume",
                             "Checkpoint does not match current configuration.");
        return;
    }

    QString details;
    details += QString("Resume existing tournament from %1\n\n").arg(checkpoint_path);
    details += QString("Games played: %1\n").arg(static_cast<int>(checkpoint.completed_games.size()));
    if (checkpoint.next_game.fixture_index >= 0) {
        details += QString("Next game: %1 vs %2 (opening %3)\n")
                       .arg(QString::fromStdString(checkpoint.next_game.white))
                       .arg(QString::fromStdString(checkpoint.next_game.black))
                       .arg(QString::fromStdString(checkpoint.next_game.opening_id));
    } else {
        details += "Next game: -\n";
    }

    auto standings = checkpoint.standings;
    std::sort(standings.begin(), standings.end(), [](const auto& a, const auto& b) {
        if (a.points != b.points) {
            return a.points > b.points;
        }
        return a.games > b.games;
    });
    details += "\nTop 3 standings:\n";
    for (size_t i = 0; i < std::min<size_t>(3, standings.size()); ++i) {
        const auto& row = standings[i];
        details += QString("%1. %2 (%3 pts)\n")
                       .arg(static_cast<int>(i + 1))
                       .arg(QString::fromStdString(row.name))
                       .arg(row.points);
    }

    const auto reply = QMessageBox::question(this,
                                             "Resume Tournament",
                                             details,
                                             QMessageBox::Ok | QMessageBox::Cancel);
    if (reply != QMessageBox::Ok) {
        return;
    }

    if (!runner_service_.startWithResume(true)) {
        QMessageBox::information(this, "Runner", "Runner already active.");
    }
}

void MainWindow::exportResults() {
    const auto directory = QFileDialog::getExistingDirectory(this, "Export results");
    if (directory.isEmpty()) {
        return;
    }
    std::string error;
    if (!runner_service_.exportResults(directory.toStdString(), &error)) {
        QMessageBox::warning(this, "Export", QString::fromStdString(error));
        return;
    }
    QMessageBox::information(this, "Export", "Results exported successfully.");
}

void MainWindow::openOutputFolder() {
    output_dir_ = output_dir_edit_->text();
    if (output_dir_.isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(output_dir_));
}

void MainWindow::updateLiveView() {
    const auto state = runner_service_.getStateSnapshot();
    const bool tlcs_enabled = !server_ini_path_->text().isEmpty();
    live_panel_->updateState(state, tlcs_enabled);

    standings_model_->setStandings(runner_service_.getStandingsSnapshot());
    log_view_->setLogText(QString::fromStdString(runner_service_.getLastLogLines(400)));

    pause_action_->setEnabled(state.running && !state.paused);
    resume_action_->setEnabled(state.running && state.paused);
    stop_action_->setEnabled(state.running);
    start_action_->setEnabled(!state.running);
    export_results_action_->setEnabled(!state.running);

    const auto output_dir = output_dir_edit_->text().isEmpty() ? "out" : output_dir_edit_->text();
    const QString checkpoint_path = QDir(output_dir).filePath("checkpoint.json");
    resume_tournament_action_->setEnabled(!state.running && QFileInfo::exists(checkpoint_path));
}

void MainWindow::updateLogInterval(int value) {
    if (refresh_timer_) {
        refresh_timer_->setInterval(value);
    }
}

void MainWindow::handleOpenLivePgn(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool MainWindow::validateConfig(const ijccrl::core::api::RunnerConfig& config) {
    if (config.engines.size() < 2) {
        QMessageBox::warning(this, "Validation", "Please configure at least two engines.");
        return false;
    }

    for (const auto& engine : config.engines) {
        const QFileInfo info(QString::fromStdString(engine.cmd));
        if (!info.exists()) {
            QMessageBox::warning(this,
                                 "Validation",
                                 QString("Engine path not found: %1").arg(info.filePath()));
            return false;
        }
    }

    if (!config.openings.path.empty()) {
        const QFileInfo suite_info(QString::fromStdString(config.openings.path));
        if (!suite_info.exists()) {
            QMessageBox::warning(this,
                                 "Validation",
                                 QString("Openings suite not found: %1").arg(suite_info.filePath()));
            return false;
        }
    }

    if (!config.broadcast.server_ini.empty()) {
        const QFileInfo ini_info(QString::fromStdString(config.broadcast.server_ini));
        if (!ini_info.exists()) {
            QMessageBox::warning(this,
                                 "Validation",
                                 QString("server.ini not found: %1").arg(ini_info.filePath()));
            return false;
        }
        if (!ContainsTourneyPgn(ini_info.filePath())) {
            QMessageBox::warning(this,
                                 "Validation",
                                 "server.ini missing TOURNEYPGN entry.");
            return false;
        }
    }

    if (config.tournament.concurrency > static_cast<int>(config.engines.size() / 2)) {
        const auto reply = QMessageBox::warning(this,
                                                "Validation",
                                                "Concurrency exceeds engines/2. Continue?",
                                                QMessageBox::Ok | QMessageBox::Cancel);
        if (reply != QMessageBox::Ok) {
            return false;
        }
    }

    return true;
}

ijccrl::core::api::RunnerConfig MainWindow::buildConfigFromUi() const {
    ijccrl::core::api::RunnerConfig config;

    std::vector<EnginesModel::EngineEntry> entries = engines_model_->entries();
    for (const auto& entry : entries) {
        if (entry.path.trimmed().isEmpty()) {
            continue;
        }
        ijccrl::core::api::EngineConfig engine;
        engine.name = entry.name.trimmed().isEmpty() ? "UCI" : entry.name.toStdString();
        engine.cmd = entry.path.toStdString();
        engine.uci_options["Threads"] = QString::number(entry.threads).toStdString();
        engine.uci_options["Hash"] = QString::number(entry.hash).toStdString();

        for (const auto& item : SplitOptions(entry.extraOptions)) {
            const auto parts = item.split('=');
            if (parts.size() >= 2) {
                engine.uci_options[parts[0].trimmed().toStdString()] = parts.mid(1).join("=").trimmed().toStdString();
            }
        }

        config.engines.push_back(std::move(engine));
    }

    config.tournament.mode = tournament_mode_->currentData().toString().toStdString();
    const bool is_swiss = config.tournament.mode == "swiss";
    config.tournament.double_round_robin = is_swiss ? false : double_rr_->isChecked();
    config.tournament.rounds = rounds_spin_->value();
    config.tournament.games_per_pairing = games_per_pairing_->value();
    config.tournament.concurrency = concurrency_spin_->value();
    config.tournament.avoid_repeats = avoid_repeats_->isChecked();
    config.tournament.bye_points = bye_points_->isChecked() ? 1.0 : 0.0;

    config.time_control.base_seconds = base_seconds_spin_->value();
    config.time_control.increment_seconds = increment_seconds_spin_->value();

    config.openings.type = openings_type_->currentData().toString().toStdString();
    config.openings.path = openings_path_->text().toStdString();
    config.openings.policy = openings_policy_->currentData().toString().toStdString();
    config.openings.seed = openings_seed_->value();

    output_dir_ = output_dir_edit_->text().isEmpty() ? "out" : output_dir_edit_->text();
    const auto output_base = output_dir_.toStdString();
    config.output.tournament_pgn = output_base + "/tournament.pgn";
    config.output.live_pgn = output_base + "/live.pgn";
    config.output.results_json = output_base + "/results.json";
    config.output.pairings_csv = output_base + "/pairings.csv";
    config.output.progress_log = output_base + "/progress.log";
    config.output.checkpoint_json = output_base + "/checkpoint.json";
    config.output.standings_csv = output_base + "/standings.csv";
    config.output.standings_html = output_base + "/standings.html";
    config.output.summary_json = output_base + "/summary.json";
    config.output.metrics_json = output_base + "/metrics.json";
    config.output.games_dir = output_base + "/games";

    if (!server_ini_path_->text().isEmpty()) {
        config.broadcast.adapter = "tlcs_ini";
        config.broadcast.server_ini = server_ini_path_->text().toStdString();
    }

    return config;
}

void MainWindow::applyConfigToUi(const ijccrl::core::api::RunnerConfig& config) {
    std::vector<EnginesModel::EngineEntry> entries;
    entries.reserve(config.engines.size());
    for (const auto& engine : config.engines) {
        EnginesModel::EngineEntry entry;
        entry.name = QString::fromStdString(engine.name);
        entry.path = QString::fromStdString(engine.cmd);
        auto threads = engine.uci_options.find("Threads");
        if (threads != engine.uci_options.end()) {
            entry.threads = QString::fromStdString(threads->second).toInt();
        }
        auto hash = engine.uci_options.find("Hash");
        if (hash != engine.uci_options.end()) {
            entry.hash = QString::fromStdString(hash->second).toInt();
        }
        QStringList extra;
        for (const auto& option : engine.uci_options) {
            if (option.first == "Threads" || option.first == "Hash") {
                continue;
            }
            extra << QString::fromStdString(option.first + "=" + option.second);
        }
        entry.extraOptions = extra.join("; ");
        entries.push_back(entry);
    }
    engines_model_->setEntries(entries);

    const auto mode_index = tournament_mode_->findData(QString::fromStdString(config.tournament.mode));
    if (mode_index >= 0) {
        tournament_mode_->setCurrentIndex(mode_index);
    }

    double_rr_->setChecked(config.tournament.double_round_robin);
    rounds_spin_->setValue(config.tournament.rounds);
    games_per_pairing_->setValue(config.tournament.games_per_pairing);
    concurrency_spin_->setValue(config.tournament.concurrency);
    avoid_repeats_->setChecked(config.tournament.avoid_repeats);
    bye_points_->setChecked(config.tournament.bye_points > 0.0);

    base_seconds_spin_->setValue(config.time_control.base_seconds);
    increment_seconds_spin_->setValue(config.time_control.increment_seconds);

    const auto type_index = openings_type_->findData(QString::fromStdString(config.openings.type));
    if (type_index >= 0) {
        openings_type_->setCurrentIndex(type_index);
    }
    openings_path_->setText(QString::fromStdString(config.openings.path));
    const auto policy_index = openings_policy_->findData(QString::fromStdString(config.openings.policy));
    if (policy_index >= 0) {
        openings_policy_->setCurrentIndex(policy_index);
    }
    openings_seed_->setValue(config.openings.seed);

    server_ini_path_->setText(QString::fromStdString(config.broadcast.server_ini));

    const QFileInfo out_info(QString::fromStdString(config.output.tournament_pgn));
    output_dir_ = out_info.dir().path();
    if (output_dir_.isEmpty()) {
        output_dir_ = "out";
    }
    output_dir_edit_->setText(output_dir_);

    updateTournamentOptions();
}
