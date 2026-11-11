#pragma once

#include <QMainWindow>
#include <QString>

#include "canlog/dbc.hpp"

class FrameTableModel;
class PlotWidget;
class QComboBox;
class QTableView;
class QTableWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Also used for command-line arguments: canlog_gui sample.dbc sample.log
    void loadDbcFile(const QString& path);
    void loadLogFile(const QString& path);

private:
    void openDbc();
    void openLog();
    void rebuildSignalList();
    void showFrameSignals(int row);
    void updatePlot();
    void updateStatus();

    canlog::Database db_;
    FrameTableModel* model_ = nullptr;
    QTableView* traceView_ = nullptr;
    QTableWidget* signalsTable_ = nullptr;
    QComboBox* signalCombo_ = nullptr;
    PlotWidget* plot_ = nullptr;

    QString dbcName_;
    QString logName_;
    int skippedLines_ = 0;
};
