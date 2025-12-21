#pragma once

#include "ijccrl/core/api/RunnerService.h"

#include <QAbstractTableModel>
#include <vector>

class StandingsModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit StandingsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void setStandings(const std::vector<ijccrl::core::api::StandingRow>& standings);

private:
    std::vector<ijccrl::core::api::StandingRow> standings_{};
};
