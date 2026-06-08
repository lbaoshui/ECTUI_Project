/*
 * @Descripttion: 数据保存模块实现
 * @version: 1.0.0
 */
#include "savemanager.h"
#include "probemanager.h"
#include "probe.h"

// ═══════════════════════════════════════════════
//  SaveWorker 实现
// ═══════════════════════════════════════════════

SaveWorker::SaveWorker(QObject *parent)
    : QObject(parent)
{
}

SaveWorker::~SaveWorker()
{
    // 确保所有文件已关闭
    for (auto &handle : m_files) {
        if (handle.stream) {
            handle.stream->flush();
            delete handle.stream;
            handle.stream = nullptr;
        }
        if (handle.file) {
            if (handle.file->isOpen()) {
                handle.file->close();
            }
            delete handle.file;
            handle.file = nullptr;
        }
    }
    m_files.clear();
}

void SaveWorker::openFiles(const QString &folder, const QHash<int, QString> &fileMap)
{
    // 确保目录存在
    QDir dir(folder);
    if (!dir.exists()) {
        dir.mkpath(".");
        qDebug() << "[SaveWorker] created directory:" << folder;
    }

    for (auto it = fileMap.constBegin(); it != fileMap.constEnd(); ++it) {
        const int probeIndex = it.key();
        const QString &fileName = it.value();
        const QString fullPath = folder + "/" + fileName;

        // 先关闭该探头已有的文件（如果存在）
        if (m_files.contains(probeIndex)) {
            auto &old = m_files[probeIndex];
            if (old.stream) {
                old.stream->flush();
                delete old.stream;
                old.stream = nullptr;
            }
            if (old.file) {
                if (old.file->isOpen()) old.file->close();
                delete old.file;
                old.file = nullptr;
            }
        }

        // 以 Append 模式打开，确保持续采集时数据写入同一文件
        FileHandle handle;
        handle.file = new QFile(fullPath);
        if (handle.file->open(QIODevice::WriteOnly | QIODevice::Append)) {
            handle.stream = new QTextStream(handle.file);
            m_files[probeIndex] = handle;
            qDebug() << "[SaveWorker] file opened:" << fullPath;
            emit fileOpened(probeIndex, fullPath);
        } else {
            qWarning() << "[SaveWorker] failed to open:" << fullPath;
            delete handle.file;
            emit writeError(probeIndex, QStringLiteral("无法打开文件: %1").arg(fullPath));
        }
    }
}

void SaveWorker::appendData(int probeIndex, const QVector<float> &amp, const QVector<float> &phase)
{
    if (!m_files.contains(probeIndex)) {
        emit writeError(probeIndex, QStringLiteral("探头 %1 的文件未打开").arg(probeIndex));
        return;
    }

    const auto &handle = m_files[probeIndex];
    if (!handle.file || !handle.file->isOpen() || !handle.stream) {
        emit writeError(probeIndex, QStringLiteral("探头 %1 的文件句柄无效").arg(probeIndex));
        return;
    }

    QTextStream &out = *handle.stream;
    const int count = qMin(amp.size(), phase.size());
    for (int i = 0; i < count; ++i) {
        out << amp[i] << "," << phase[i] << "\n";
    }

    // 定期刷盘，避免进程崩溃时丢失过多数据
    if (count > 0) {
        handle.stream->flush();
    }
}

void SaveWorker::closeAllFiles()
{
    for (auto it = m_files.begin(); it != m_files.end(); ++it) {
        if (it.value().stream) {
            it.value().stream->flush();
            delete it.value().stream;
            it.value().stream = nullptr;
        }
        if (it.value().file) {
            if (it.value().file->isOpen()) {
                it.value().file->close();
            }
            delete it.value().file;
            it.value().file = nullptr;
        }
        emit fileClosed(it.key());
    }
    m_files.clear();
    emit allFilesClosed();
}

void SaveWorker::flushAndClose()
{
    closeAllFiles();
}


// ═══════════════════════════════════════════════
//  SaveManager 实现
// ═══════════════════════════════════════════════

SaveManager::SaveManager(ProbeManager *probeManager, QObject *parent)
    : QObject(parent)
    , m_probeManager(probeManager)
{
    // 创建专用工作线程
    m_workerThread = new QThread(this);
    m_worker = new SaveWorker();          // 无 parent，由 moveToThread 管理
    m_worker->moveToThread(m_workerThread);

    // 跨线程信号连接（QueuedConnection）
    connect(this, &SaveManager::destroyed, m_worker, &SaveWorker::deleteLater);
    connect(m_workerThread, &QThread::finished, m_worker, &SaveWorker::deleteLater);

    m_workerThread->start();
}

SaveManager::~SaveManager()
{
    // 如果正在采集，先停止
    if (m_isAcquiring) {
        onAcquisitionStopped();
    }

    m_workerThread->quit();
    m_workerThread->wait();
}

void SaveManager::setDataFolder(const QString &folder)
{
    m_dataFolder = folder;
}

void SaveManager::onAcquisitionStarted()
{
    if (m_isAcquiring) {
        qWarning() << "[SaveManager] 采集已在进行中";
        return;
    }

    if (m_dataFolder.isEmpty()) {
        m_dataFolder = QDir::currentPath() + "/data";
        qDebug() << "[SaveManager] 使用默认数据目录:" << m_dataFolder;
    }

    m_isAcquiring = true;
    m_currentFiles.clear();

    // 为所有已启用的探头生成带时间戳的文件名
    const auto probes = m_probeManager->allProbes();
    QHash<int, QString> fileMap;
    for (int i = 0; i < probes.size(); ++i) {
        if (probes[i] && probes[i]->isEnabled()) {
            const QString fileName = generateFileName(i);
            m_currentFiles[i] = fileName;
            fileMap[i] = fileName;
            emit newFileCreated(i, m_dataFolder + "/" + fileName);
        }
    }

    // 通过函数对象跨线程调用（编译期类型检查，无需 qRegisterMetaType）
    const QString folder = m_dataFolder;
    QMetaObject::invokeMethod(m_worker, [this, folder, fileMap]() {
        m_worker->openFiles(folder, fileMap);
    });

    qDebug() << "[SaveManager] 采集会话开始, 探头数:" << fileMap.size();
}

void SaveManager::onAcquisitionStopped()
{
    if (!m_isAcquiring) {
        return;
    }

    m_isAcquiring = false;

    // 通知工作线程刷盘并关闭所有文件
    QMetaObject::invokeMethod(m_worker, [this]() {
        m_worker->flushAndClose();
    });

    m_currentFiles.clear();
    qDebug() << "[SaveManager] 采集会话结束";
}

void SaveManager::onSaveDataReady(int probeIndex)
{
    if (!m_isAcquiring) {
        return;
    }

    Probe *probe = m_probeManager->probeAt(probeIndex);
    if (!probe) {
        return;
    }

    ProbeData *saveData = probe->saveData();
    if (!saveData || saveData->isEmpty()) {
        return;
    }

    // 从 saveData 拷贝数据，发送到工作线程写入
    QVector<float> amp;
    QVector<float> phase;
    if (saveData->m_rawData_amp) {
        amp = *saveData->m_rawData_amp;      // 深拷贝
    }
    if (saveData->m_rawData_phase) {
        phase = *saveData->m_rawData_phase;  // 深拷贝
    }

    if (amp.isEmpty()) {
        return;
    }

    // 通过函数对象跨线程传递数据（amp/phase 由 lambda 捕获带走）
    QMetaObject::invokeMethod(m_worker, [this, probeIndex, amp = std::move(amp), phase = std::move(phase)]() {
        m_worker->appendData(probeIndex, amp, phase);
    });

    qDebug() << "[SaveManager] saved" << amp.size() << "points from probe" << probeIndex;
}

QString SaveManager::generateFileName(int probeIndex) const
{
    // 文件名格式: probe_{N}_{yyyyMMdd_HHmmss}.csv
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    return QStringLiteral("probe_%1_%2.csv").arg(probeIndex).arg(timestamp);
}
