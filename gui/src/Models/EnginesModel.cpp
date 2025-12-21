#include "Models/EnginesModel.h"

EnginesModel::EnginesModel(QObject* parent)
    : QAbstractTableModel(parent) {
    entries_.push_back({"", "", 1, 256, ""});
}

int EnginesModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(entries_.size());
}

int EnginesModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return 5;
}

QVariant EnginesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(entries_.size())) {
        return {};
    }

    const auto& entry = entries_[static_cast<size_t>(index.row())];
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case 0:
                return entry.name;
            case 1:
                return entry.path;
            case 2:
                return entry.threads;
            case 3:
                return entry.hash;
            case 4:
                return entry.extraOptions;
            default:
                break;
        }
    }
    return {};
}

bool EnginesModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() >= static_cast<int>(entries_.size()) || role != Qt::EditRole) {
        return false;
    }

    auto& entry = entries_[static_cast<size_t>(index.row())];
    switch (index.column()) {
        case 0:
            entry.name = value.toString();
            break;
        case 1:
            entry.path = value.toString();
            break;
        case 2:
            entry.threads = std::max(1, value.toInt());
            break;
        case 3:
            entry.hash = std::max(1, value.toInt());
            break;
        case 4:
            entry.extraOptions = value.toString();
            break;
        default:
            return false;
    }

    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

Qt::ItemFlags EnginesModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
}

QVariant EnginesModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
        case 0:
            return "Name";
        case 1:
            return "Path";
        case 2:
            return "Threads";
        case 3:
            return "Hash";
        case 4:
            return "Extra options";
        default:
            break;
    }
    return {};
}

void EnginesModel::addEmptyRow() {
    beginInsertRows(QModelIndex(), static_cast<int>(entries_.size()), static_cast<int>(entries_.size()));
    entries_.push_back({"", "", 1, 256, ""});
    endInsertRows();
}

void EnginesModel::removeRowAt(int row) {
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    beginRemoveRows(QModelIndex(), row, row);
    entries_.erase(entries_.begin() + row);
    endRemoveRows();
}

void EnginesModel::setEntries(const std::vector<EngineEntry>& entries) {
    beginResetModel();
    entries_ = entries;
    if (entries_.empty()) {
        entries_.push_back({"", "", 1, 256, ""});
    }
    endResetModel();
}
