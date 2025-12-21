#pragma once

#include <QAbstractTableModel>
#include <QString>
#include <vector>

class EnginesModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    struct EngineEntry {
        QString name;
        QString path;
        int threads = 1;
        int hash = 256;
        QString extraOptions;
    };

    explicit EnginesModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void addEmptyRow();
    void removeRowAt(int row);
    void setEntries(const std::vector<EngineEntry>& entries);
    const std::vector<EngineEntry>& entries() const { return entries_; }

private:
    std::vector<EngineEntry> entries_{};
};
