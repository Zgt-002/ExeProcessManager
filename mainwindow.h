#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "processmanager.h"

#include <QFutureWatcher>
#include <QMainWindow>

class QPushButton;
class QTableWidget;
class QTextEdit;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    enum class Operation
    {
        Start,
        Stop,
        Restart
    };

    void choosePrograms();
    void chooseDirectory();
    void directoryScanFinished();
    int addPrograms(const QStringList &paths);
    QString configFilePath() const;
    void loadPrograms();
    void savePrograms();
    void removeSelectedPrograms();
    void clearPrograms();
    void runOperation(Operation operation);
    void operationFinished();
    void refreshStatuses();
    void setBusy(bool busy, const QString &status = QString());
    QStringList selectedPaths() const;
    void appendLog(const ProcessManager::OperationResult &result);

    QTableWidget *programTable_;
    QTextEdit *logEdit_;
    QPushButton *chooseButton_;
    QPushButton *chooseDirectoryButton_;
    QPushButton *removeButton_;
    QPushButton *clearButton_;
    QPushButton *startButton_;
    QPushButton *stopButton_;
    QPushButton *restartButton_;
    QTimer *monitorTimer_;
    QFutureWatcher<QList<ProcessManager::OperationResult>> operationWatcher_;
    QFutureWatcher<QStringList> directoryScanWatcher_;
    bool busy_ = false;
};

#endif
