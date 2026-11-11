#pragma once

#include <QAbstractTableModel>
#include <vector>

#include "canlog/dbc.hpp"
#include "canlog/frame.hpp"

// Table model for the trace window: one row per captured frame.
// Columns: time, interface, id, message name (from the DBC), DLC, data bytes.
class FrameTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit FrameTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setFrames(std::vector<canlog::CanFrame> frames);

    // The database is owned by the caller and must outlive its use by the model.
    void setDatabase(const canlog::Database* db);

    const std::vector<canlog::CanFrame>& frames() const { return frames_; }

private:
    std::vector<canlog::CanFrame> frames_;
    const canlog::Database* db_ = nullptr;
    double t0_ = 0.0;   // timestamp of the first frame; shown times are relative to it
};
