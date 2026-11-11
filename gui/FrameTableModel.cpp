#include "FrameTableModel.hpp"

#include <utility>

#include "canlog/format.hpp"

namespace {

enum Column { ColTime, ColIface, ColId, ColMessage, ColDlc, ColData, ColCount };

}  // namespace

FrameTableModel::FrameTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int FrameTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(frames_.size());
}

int FrameTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(ColCount);
}

QVariant FrameTableModel::data(const QModelIndex& index, int role) const {
    if (role != Qt::DisplayRole || !index.isValid()) return QVariant();
    if (index.row() < 0 || index.row() >= static_cast<int>(frames_.size())) return QVariant();

    const canlog::CanFrame& f = frames_[static_cast<size_t>(index.row())];

    switch (index.column()) {
    case ColTime:
        return QString::number(f.timestamp - t0_, 'f', 6);
    case ColIface:
        return QString::fromStdString(f.iface);
    case ColId: {
        const int width = f.extended ? 8 : 3;
        return QStringLiteral("0x") +
               QString::number(f.id, 16).toUpper().rightJustified(width, QLatin1Char('0'));
    }
    case ColMessage: {
        if (!db_) return QString();
        if (const canlog::Message* m = db_->find(f.id, f.extended)) {
            return QString::fromStdString(m->name);
        }
        return QStringLiteral("(unknown)");
    }
    case ColDlc:
        return static_cast<int>(f.dlc);
    case ColData:
        if (f.rtr) return QStringLiteral("RTR");
        return QString::fromStdString(canlog::frame_data_hex(f));
    default:
        return QVariant();
    }
}

QVariant FrameTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) return QVariant();
    if (orientation == Qt::Vertical) return section + 1;

    switch (section) {
    case ColTime:
        return QStringLiteral("Time (s)");
    case ColIface:
        return QStringLiteral("Iface");
    case ColId:
        return QStringLiteral("ID");
    case ColMessage:
        return QStringLiteral("Message");
    case ColDlc:
        return QStringLiteral("DLC");
    case ColData:
        return QStringLiteral("Data");
    default:
        return QVariant();
    }
}

void FrameTableModel::setFrames(std::vector<canlog::CanFrame> frames) {
    beginResetModel();
    frames_ = std::move(frames);
    t0_ = frames_.empty() ? 0.0 : frames_.front().timestamp;
    endResetModel();
}

void FrameTableModel::setDatabase(const canlog::Database* db) {
    beginResetModel();
    db_ = db;
    endResetModel();
}
