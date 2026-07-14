#ifndef PROCESSMANAGER_H
#define PROCESSMANAGER_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class ProcessManager
{
public:
    struct OperationResult
    {
        QString path;
        bool success;
        QString message;
    };

    static QHash<QString, int> runningInstances(const QStringList &paths);
    static QList<OperationResult> startPrograms(const QStringList &paths);
    static QList<OperationResult> stopPrograms(const QStringList &paths);
    static QList<OperationResult> restartPrograms(const QStringList &paths);

private:
    static QString pathKey(const QString &path);
};

#endif
