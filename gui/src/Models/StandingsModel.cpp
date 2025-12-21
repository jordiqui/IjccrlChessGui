#include "Models/StandingsModel.h"

StandingsModel::StandingsModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int StandingsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(standings_.size());
}

int StandingsModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return 7;
}

QVariant StandingsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(standings_.size())) {
        return {};
    }

    const auto& row = standings_[static_cast<size_t>(index.row())];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0:
                return QString::fromStdString(row.name);
            case 1:
                return row.games;
            case 2:
                return row.wins;
            case 3:
                return row.draws;
            case 4:
                return row.losses;
            case 5:
                return QString::number(row.points, 'f', 1);
            case 6:
                return QString::number(row.scorePercent, 'f', 1) + "%";
            default:
                break;
        }
    }
    return {};
}

QVariant StandingsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
        case 0:
            return "Engine";
        case 1:
            return "G";
        case 2:
            return "W";
        case 3:
            return "D";
        case 4:
            return "L";
        case 5:
            return "Pts";
        case 6:
            return "Score";
        default:
            break;
    }
    return {};
}

void StandingsModel::setStandings(const std::vector<ijccrl::core::api::StandingRow>& standings) {
    beginResetModel();
    standings_ = standings;
    endResetModel();
}
