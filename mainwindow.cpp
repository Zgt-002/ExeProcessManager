#include "mainwindow.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <functional>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , programTable_(new QTableWidget(this))
    , logEdit_(new QTextEdit(this))
    , chooseButton_(new QPushButton(QStringLiteral("选择 EXE"), this))
    , removeButton_(new QPushButton(QStringLiteral("移除选中"), this))
    , clearButton_(new QPushButton(QStringLiteral("清空列表"), this))
    , startButton_(new QPushButton(QStringLiteral("启动"), this))
    , stopButton_(new QPushButton(QStringLiteral("结束"), this))
    , restartButton_(new QPushButton(QStringLiteral("一键重启"), this))
    , monitorTimer_(new QTimer(this))
{
    setWindowTitle(QStringLiteral("EXE 进程管理器"));
    resize(900, 600);

    programTable_->setColumnCount(2);
    programTable_->setHorizontalHeaderLabels({QStringLiteral("EXE 路径"), QStringLiteral("运行状态")});
    programTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    programTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    programTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    programTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    programTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    logEdit_->setReadOnly(true);

    auto *listButtons = new QHBoxLayout;
    listButtons->addWidget(chooseButton_);
    listButtons->addWidget(removeButton_);
    listButtons->addWidget(clearButton_);
    listButtons->addStretch();

    auto *operationButtons = new QHBoxLayout;
    operationButtons->addStretch();
    operationButtons->addWidget(startButton_);
    operationButtons->addWidget(stopButton_);
    operationButtons->addWidget(restartButton_);

    auto *centralWidget = new QWidget(this);
    auto *layout = new QVBoxLayout(centralWidget);
    layout->addLayout(listButtons);
    layout->addWidget(programTable_, 3);
    layout->addLayout(operationButtons);
    layout->addWidget(new QLabel(QStringLiteral("执行日志"), this));
    layout->addWidget(logEdit_, 2);
    setCentralWidget(centralWidget);

    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::choosePrograms);
    connect(removeButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedPrograms);
    connect(clearButton_, &QPushButton::clicked, this, &MainWindow::clearPrograms);
    connect(startButton_, &QPushButton::clicked, this, [this] { runOperation(Operation::Start); });
    connect(stopButton_, &QPushButton::clicked, this, [this] { runOperation(Operation::Stop); });
    connect(restartButton_, &QPushButton::clicked, this, [this] { runOperation(Operation::Restart); });
    connect(monitorTimer_, &QTimer::timeout, this, &MainWindow::refreshStatuses);
    connect(&operationWatcher_, &QFutureWatcherBase::finished, this, &MainWindow::operationFinished);

    monitorTimer_->start(1000);
    setBusy(false);
}

void MainWindow::choosePrograms()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("选择一个或多个 EXE"),
        QString(),
        QStringLiteral("Windows 程序 (*.exe)"));

    QSet<QString> existingPaths;
    for (const QString &path : selectedPaths()) {
        existingPaths.insert(QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded());
    }

    for (const QString &file : files) {
        const QString absolutePath = QFileInfo(file).absoluteFilePath();
        const QString key = QDir::cleanPath(QDir::fromNativeSeparators(absolutePath)).toCaseFolded();
        if (existingPaths.contains(key)) {
            continue;
        }

        const int row = programTable_->rowCount();
        programTable_->insertRow(row);
        auto *pathItem = new QTableWidgetItem(QDir::toNativeSeparators(absolutePath));
        pathItem->setData(Qt::UserRole, absolutePath);
        programTable_->setItem(row, 0, pathItem);
        programTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("检测中")));
        existingPaths.insert(key);
    }

    refreshStatuses();
    setBusy(false);
}

void MainWindow::removeSelectedPrograms()
{
    const QModelIndexList selectedRows = programTable_->selectionModel()->selectedRows();
    QList<int> rows;
    for (const QModelIndex &index : selectedRows) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) {
        programTable_->removeRow(row);
    }
    setBusy(false);
}

void MainWindow::clearPrograms()
{
    programTable_->setRowCount(0);
    setBusy(false);
}

void MainWindow::runOperation(Operation operation)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择 EXE 程序。"));
        return;
    }

    QString status;
    switch (operation) {
    case Operation::Start:
        status = QStringLiteral("正在启动");
        break;
    case Operation::Stop:
        status = QStringLiteral("正在结束");
        break;
    case Operation::Restart:
        status = QStringLiteral("正在重启");
        break;
    }
    setBusy(true, status);

    operationWatcher_.setFuture(QtConcurrent::run([paths, operation] {
        switch (operation) {
        case Operation::Start:
            return ProcessManager::startPrograms(paths);
        case Operation::Stop:
            return ProcessManager::stopPrograms(paths);
        case Operation::Restart:
            return ProcessManager::restartPrograms(paths);
        }
        return QList<ProcessManager::OperationResult>();
    }));
}

void MainWindow::operationFinished()
{
    const QList<ProcessManager::OperationResult> results = operationWatcher_.result();
    for (const ProcessManager::OperationResult &result : results) {
        appendLog(result);
    }

    setBusy(false);
    refreshStatuses();
}

void MainWindow::refreshStatuses()
{
    if (busy_ || programTable_->rowCount() == 0) {
        return;
    }

    const QStringList paths = selectedPaths();
    const QHash<QString, int> counts = ProcessManager::runningInstances(paths);
    for (int row = 0; row < programTable_->rowCount(); ++row) {
        const QString path = programTable_->item(row, 0)->data(Qt::UserRole).toString();
        const QString key = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath())).toCaseFolded();
#ifdef Q_OS_WIN
        const int count = counts.value(key);
        const QString status = count == 0
            ? QStringLiteral("未运行")
            : count == 1 ? QStringLiteral("运行中")
                         : QStringLiteral("运行中（%1 个实例）").arg(count);
#else
        const QString status = QStringLiteral("仅支持 Windows");
#endif
        programTable_->item(row, 1)->setText(status);
    }
}

void MainWindow::setBusy(bool busy, const QString &status)
{
    busy_ = busy;
    chooseButton_->setEnabled(!busy);
    removeButton_->setEnabled(!busy && programTable_->rowCount() > 0);
    clearButton_->setEnabled(!busy && programTable_->rowCount() > 0);
    startButton_->setEnabled(!busy && programTable_->rowCount() > 0);
    stopButton_->setEnabled(!busy && programTable_->rowCount() > 0);
    restartButton_->setEnabled(!busy && programTable_->rowCount() > 0);

    if (!status.isEmpty()) {
        for (int row = 0; row < programTable_->rowCount(); ++row) {
            programTable_->item(row, 1)->setText(status);
        }
    }
}

QStringList MainWindow::selectedPaths() const
{
    QStringList paths;
    for (int row = 0; row < programTable_->rowCount(); ++row) {
        paths.append(programTable_->item(row, 0)->data(Qt::UserRole).toString());
    }
    return paths;
}

void MainWindow::appendLog(const ProcessManager::OperationResult &result)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString state = result.success ? QStringLiteral("成功") : QStringLiteral("失败");
    logEdit_->append(QStringLiteral("[%1] [%2] %3 - %4")
                         .arg(timestamp, state, QDir::toNativeSeparators(result.path), result.message));
}
