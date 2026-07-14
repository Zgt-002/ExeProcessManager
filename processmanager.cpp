#include "processmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QSet>

#include <iterator>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace {

#ifdef Q_OS_WIN
struct RunningProcess
{
    QString path;
    DWORD processId;
};

QString windowsError(const char *functionName, DWORD errorCode)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        0,
        reinterpret_cast<wchar_t *>(&buffer),
        0,
        nullptr);

    QString message = length > 0 ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
                                 : QStringLiteral("Unknown error");
    if (buffer) {
        LocalFree(buffer);
    }

    return QStringLiteral("%1 failed (error %2): %3")
        .arg(QString::fromLatin1(functionName))
        .arg(errorCode)
        .arg(message);
}

QList<RunningProcess> enumerateProcesses()
{
    QList<RunningProcess> processes;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return processes;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == static_cast<DWORD>(QCoreApplication::applicationPid())) {
                continue;
            }

            const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                               FALSE,
                                               entry.th32ProcessID);
            if (!process) {
                continue;
            }

            wchar_t pathBuffer[32768];
            DWORD pathLength = static_cast<DWORD>(std::size(pathBuffer));
            if (QueryFullProcessImageNameW(process, 0, pathBuffer, &pathLength)) {
                processes.append({QString::fromWCharArray(pathBuffer, static_cast<int>(pathLength)),
                                  entry.th32ProcessID});
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return processes;
}
#endif

} // namespace

QString ProcessManager::pathKey(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath())).toCaseFolded();
}

QHash<QString, int> ProcessManager::runningInstances(const QStringList &paths)
{
    QHash<QString, int> counts;
    QSet<QString> targetKeys;
    for (const QString &path : paths) {
        const QString key = pathKey(path);
        counts.insert(key, 0);
        targetKeys.insert(key);
    }

#ifdef Q_OS_WIN
    const QList<RunningProcess> processes = enumerateProcesses();
    for (const RunningProcess &process : processes) {
        const QString key = pathKey(process.path);
        if (targetKeys.contains(key)) {
            counts[key] = counts.value(key) + 1;
        }
    }
#endif

    return counts;
}

QList<ProcessManager::OperationResult> ProcessManager::startPrograms(const QStringList &paths)
{
    QList<OperationResult> results;

#ifndef Q_OS_WIN
    for (const QString &path : paths) {
        results.append({path, false, QStringLiteral("仅支持 Windows")});
    }
    return results;
#else
    const QHash<QString, int> counts = runningInstances(paths);
    for (const QString &path : paths) {
        if (counts.value(pathKey(path)) > 0) {
            results.append({path, true, QStringLiteral("已在运行，跳过启动")});
            continue;
        }

        qint64 processId = 0;
        const bool started = QProcess::startDetached(path,
                                                     QStringList(),
                                                     QFileInfo(path).absolutePath(),
                                                     &processId);
        results.append({path,
                        started,
                        started ? QStringLiteral("启动成功，PID=%1").arg(processId)
                                : QStringLiteral("QProcess::startDetached failed")});
    }
    return results;
#endif
}

QList<ProcessManager::OperationResult> ProcessManager::stopPrograms(const QStringList &paths)
{
    QList<OperationResult> results;

#ifndef Q_OS_WIN
    for (const QString &path : paths) {
        results.append({path, false, QStringLiteral("仅支持 Windows")});
    }
    return results;
#else
    struct PendingProcess
    {
        QString key;
        HANDLE handle;
    };

    QHash<QString, QString> originalPaths;
    QHash<QString, int> matchedCounts;
    QHash<QString, int> stoppedCounts;
    QHash<QString, QStringList> errors;
    for (const QString &path : paths) {
        const QString key = pathKey(path);
        originalPaths.insert(key, path);
        matchedCounts.insert(key, 0);
        stoppedCounts.insert(key, 0);
    }

    QList<PendingProcess> pending;
    const QList<RunningProcess> processes = enumerateProcesses();
    for (const RunningProcess &process : processes) {
        const QString key = pathKey(process.path);
        if (!originalPaths.contains(key)) {
            continue;
        }

        matchedCounts[key] = matchedCounts.value(key) + 1;
        const HANDLE handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE,
                                          FALSE,
                                          process.processId);
        if (!handle) {
            errors[key].append(windowsError("OpenProcess", GetLastError()));
            continue;
        }

        if (!TerminateProcess(handle, 0)) {
            errors[key].append(windowsError("TerminateProcess", GetLastError()));
            CloseHandle(handle);
            continue;
        }

        pending.append({key, handle});
    }

    QElapsedTimer timer;
    timer.start();
    constexpr qint64 timeoutMs = 5000;
    for (const PendingProcess &process : pending) {
        const qint64 remaining = qMax<qint64>(0, timeoutMs - timer.elapsed());
        const DWORD waitResult = WaitForSingleObject(process.handle, static_cast<DWORD>(remaining));
        if (waitResult == WAIT_OBJECT_0) {
            stoppedCounts[process.key] = stoppedCounts.value(process.key) + 1;
        } else if (waitResult == WAIT_TIMEOUT) {
            errors[process.key].append(QStringLiteral("WaitForSingleObject timed out"));
        } else {
            errors[process.key].append(windowsError("WaitForSingleObject", GetLastError()));
        }
        CloseHandle(process.handle);
    }

    for (const QString &path : paths) {
        const QString key = pathKey(path);
        const int matched = matchedCounts.value(key);
        const QStringList pathErrors = errors.value(key);

        if (matched == 0) {
            results.append({path, true, QStringLiteral("未运行，无需结束")});
        } else if (pathErrors.isEmpty()) {
            results.append({path,
                            true,
                            QStringLiteral("已结束 %1 个实例").arg(stoppedCounts.value(key))});
        } else {
            results.append({path,
                            false,
                            QStringLiteral("结束失败：%1").arg(pathErrors.join(QStringLiteral("; ")))});
        }
    }

    return results;
#endif
}

QList<ProcessManager::OperationResult> ProcessManager::restartPrograms(const QStringList &paths)
{
    QList<OperationResult> results = stopPrograms(paths);
    results.append(startPrograms(paths));
    return results;
}
