#include "MainWindow.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QByteArray>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointF>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <sstream>
#include <string>
#include <utility>

#include "FrameTableModel.hpp"
#include "PlotWidget.hpp"
#include "canlog/decoder.hpp"
#include "canlog/logreader.hpp"
#include "canlog/series.hpp"
#include "canlog/source.hpp"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("canlog"));
    resize(1200, 760);

    // ---- menu
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    QAction* openDbcAction = fileMenu->addAction(QStringLiteral("Open &DBC..."));
    QAction* openLogAction = fileMenu->addAction(QStringLiteral("Open &Log..."));
    openLogAction->setShortcut(QKeySequence::Open);
    fileMenu->addSeparator();
    QAction* quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);

    connect(openDbcAction, &QAction::triggered, this, &MainWindow::openDbc);
    connect(openLogAction, &QAction::triggered, this, &MainWindow::openLog);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    // ---- trace table (all frames)
    model_ = new FrameTableModel(this);
    model_->setDatabase(&db_);

    traceView_ = new QTableView;
    traceView_->setModel(model_);
    traceView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    traceView_->setSelectionMode(QAbstractItemView::SingleSelection);
    traceView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    traceView_->setAlternatingRowColors(true);
    traceView_->verticalHeader()->setDefaultSectionSize(22);
    traceView_->horizontalHeader()->setStretchLastSection(true);

    connect(traceView_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                showFrameSignals(current.isValid() ? current.row() : -1);
            });

    // ---- decoded signals of the selected frame
    signalsTable_ = new QTableWidget(0, 4);
    signalsTable_->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("Signal") << QStringLiteral("Value")
                      << QStringLiteral("Unit") << QStringLiteral("Raw"));
    signalsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    signalsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    signalsTable_->verticalHeader()->setVisible(false);
    signalsTable_->horizontalHeader()->setStretchLastSection(true);

    // ---- plot of one signal over the whole capture
    signalCombo_ = new QComboBox;
    signalCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    plot_ = new PlotWidget;

    QWidget* plotBox = new QWidget;
    QVBoxLayout* plotLayout = new QVBoxLayout(plotBox);
    plotLayout->setContentsMargins(4, 4, 4, 4);
    plotLayout->addWidget(signalCombo_, 0, Qt::AlignLeft);
    plotLayout->addWidget(plot_, 1);

    connect(signalCombo_, &QComboBox::currentIndexChanged, this, [this](int) { updatePlot(); });

    // ---- layout
    QSplitter* topSplit = new QSplitter(Qt::Horizontal);
    topSplit->addWidget(traceView_);
    topSplit->addWidget(signalsTable_);
    topSplit->setStretchFactor(0, 3);
    topSplit->setStretchFactor(1, 2);

    QSplitter* mainSplit = new QSplitter(Qt::Vertical);
    mainSplit->addWidget(topSplit);
    mainSplit->addWidget(plotBox);
    mainSplit->setStretchFactor(0, 3);
    mainSplit->setStretchFactor(1, 2);
    setCentralWidget(mainSplit);

    updateStatus();
}

void MainWindow::openDbc() {
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Open DBC file"), QString(),
                                     QStringLiteral("DBC files (*.dbc);;All files (*)"));
    if (!path.isEmpty()) loadDbcFile(path);
}

void MainWindow::openLog() {
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Open candump log"), QString(),
                                     QStringLiteral("Log files (*.log *.txt);;All files (*)"));
    if (!path.isEmpty()) loadLogFile(path);
}

void MainWindow::loadDbcFile(const QString& path) {
    // Read through QFile so non-ASCII Windows paths work, then parse from memory.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("canlog"),
                             QStringLiteral("Cannot open %1").arg(path));
        return;
    }
    const QByteArray bytes = file.readAll();

    try {
        db_ = canlog::Database::parse_string(bytes.toStdString());
    } catch (const canlog::ParseError& e) {
        QMessageBox::warning(this, QStringLiteral("canlog"), QString::fromUtf8(e.what()));
        return;
    }

    dbcName_ = QFileInfo(path).fileName();
    model_->setDatabase(&db_);   // refresh message names in the trace table
    rebuildSignalList();
    showFrameSignals(-1);
    updateStatus();
}

void MainWindow::loadLogFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("canlog"),
                             QStringLiteral("Cannot open %1").arg(path));
        return;
    }
    const QByteArray bytes = file.readAll();

    std::istringstream in(bytes.toStdString());
    canlog::LogReader reader(in);
    std::vector<canlog::CanFrame> frames = canlog::read_all(reader);

    logName_ = QFileInfo(path).fileName();
    skippedLines_ = static_cast<int>(reader.skipped_lines());
    const bool hasFrames = !frames.empty();
    model_->setFrames(std::move(frames));

    showFrameSignals(-1);
    updatePlot();
    updateStatus();

    if (hasFrames) traceView_->setCurrentIndex(model_->index(0, 0));   // select first frame
}

void MainWindow::rebuildSignalList() {
    {
        const QSignalBlocker blocker(signalCombo_);   // no plot refresh per item
        signalCombo_->clear();
        for (const canlog::Message& msg : db_.messages()) {
            for (const canlog::Signal& sig : msg.signal_defs) {
                const QString messageName = QString::fromStdString(msg.name);
                const QString signalName = QString::fromStdString(sig.name);
                signalCombo_->addItem(messageName + QStringLiteral(".") + signalName,
                                      QStringList() << messageName << signalName);
            }
        }
        signalCombo_->setCurrentIndex(signalCombo_->count() > 0 ? 0 : -1);
    }
    updatePlot();
}

void MainWindow::showFrameSignals(int row) {
    signalsTable_->setRowCount(0);

    const std::vector<canlog::CanFrame>& frames = model_->frames();
    if (row < 0 || static_cast<size_t>(row) >= frames.size()) return;

    const auto decoded = canlog::decode_frame(db_, frames[static_cast<size_t>(row)]);
    if (!decoded) return;   // unknown id or remote frame

    signalsTable_->setRowCount(static_cast<int>(decoded->signal_values.size()));
    int r = 0;
    for (const canlog::DecodedSignal& s : decoded->signal_values) {
        const QString value = s.label.empty() ? QString::number(s.physical, 'g', 8)
                                              : QString::fromStdString(s.label);
        signalsTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(s.name)));
        signalsTable_->setItem(r, 1, new QTableWidgetItem(value));
        signalsTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(s.unit)));
        signalsTable_->setItem(r, 3, new QTableWidgetItem(QString::number(s.raw)));
        ++r;
    }
}

void MainWindow::updatePlot() {
    const QStringList key = signalCombo_->currentData().toStringList();
    if (key.size() != 2 || model_->frames().empty()) {
        plot_->clear();
        return;
    }

    const std::string message = key.at(0).toStdString();
    const std::string signal = key.at(1).toStdString();

    const std::vector<canlog::SeriesPoint> series =
        canlog::extract_series(model_->frames(), db_, message, signal);

    QVector<QPointF> points;
    points.reserve(static_cast<qsizetype>(series.size()));
    for (const canlog::SeriesPoint& p : series) points.append(QPointF(p.t, p.value));

    const canlog::Signal* def = canlog::find_signal(db_, message, signal);
    plot_->setSeries(key.at(0) + QStringLiteral(".") + key.at(1),
                     def ? QString::fromStdString(def->unit) : QString(), points);
}

void MainWindow::updateStatus() {
    const QString dbc =
        dbcName_.isEmpty()
            ? QStringLiteral("none loaded")
            : QStringLiteral("%1 (%2 messages)")
                  .arg(dbcName_)
                  .arg(static_cast<int>(db_.messages().size()));

    QString log;
    if (logName_.isEmpty()) {
        log = QStringLiteral("none loaded");
    } else {
        log = QStringLiteral("%1 (%2 frames")
                  .arg(logName_)
                  .arg(static_cast<int>(model_->frames().size()));
        if (skippedLines_ > 0) log += QStringLiteral(", %1 unparsable lines skipped").arg(skippedLines_);
        log += QStringLiteral(")");
    }

    statusBar()->showMessage(QStringLiteral("DBC: %1   |   Log: %2").arg(dbc, log));
}
