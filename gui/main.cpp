// canlog_gui: Qt 6 front end for the canlog library.
//
//   canlog_gui                                  start empty, use File > Open
//   canlog_gui data/sample.dbc data/sample.log  load a DBC and a candump -L log

#include <QApplication>
#include <QString>
#include <QStringList>

#include "MainWindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    // app.arguments() is Unicode-safe on Windows, unlike argv.
    QString dbcPath;
    QString logPath;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i).endsWith(QStringLiteral(".dbc"), Qt::CaseInsensitive)) {
            dbcPath = args.at(i);
        } else {
            logPath = args.at(i);
        }
    }
    if (!dbcPath.isEmpty()) window.loadDbcFile(dbcPath);   // DBC first, so names resolve
    if (!logPath.isEmpty()) window.loadLogFile(logPath);

    return app.exec();
}
