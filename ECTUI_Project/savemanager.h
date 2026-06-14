/*
 * @Descripttion: 数据保存模块 —— 信号驱动的异步落盘
 * @version: 1.0.0
 * @Author: June0821
 * @Date: 2026-05-31
 *
 * SaveManager 工作在 main thread，负责接收采集线程的保存信号，
 * 管理文件生命周期，将数据异步写入磁盘。
 * SaveWorker 在独立 QThread 中执行实际 I/O，避免阻塞 UI。
 */
#ifndef SAVEMANAGER_H
#define SAVEMANAGER_H

#include <QObject>
#include <QThread>
#include <QFile>
#include <QTextStream>
#include <QVector>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QDebug>

class ProbeManager;
class Probe;

// 每个探头文件的元数据（写入 CSV 头部注释行）
struct ProbeFileMeta {
    float balanceAmp = 0.0f;
    float balancePhase = 0.0f;
    bool  balanceSet = false;
    float rotationAngleDeg = 0.0f;
};

// ─────────────────────────────────────────────
//  SaveWorker: 运行在工作线程，仅负责磁盘 I/O
// ─────────────────────────────────────────────
class SaveWorker : public QObject
{
    Q_OBJECT
public:
    explicit SaveWorker(QObject *parent = nullptr);
    ~SaveWorker() override;

public slots:
    /**
     * @brief 为新的采集会话打开文件（Append 模式）并写入元数据头
     * @param folder   数据存储目录
     * @param fileMap  {probeIndex: fileName} 映射
     * @param metaMap  {probeIndex: ProbeFileMeta} 元数据映射
     */
    void openFiles(const QString &folder,
                   const QHash<int, QString> &fileMap,
                   const QHash<int, ProbeFileMeta> &metaMap);

    /**
     * @brief 追加数据到指定探头的文件
     * @param probeIndex  探头逻辑索引
     * @param amp         幅值数据
     * @param phase       相位数据
     */
    void appendData(int probeIndex, const QVector<float> &amp, const QVector<float> &phase);

    /** @brief 关闭所有已打开的文件 */
    void closeAllFiles();

    /**
     * @brief 将缓冲区中剩余数据刷盘并关闭文件
     * @note  在采集停止时由 SaveManager 调用
     */
    void flushAndClose();

signals:
    void fileOpened(int probeIndex, const QString &path);
    void fileClosed(int probeIndex);
    void writeError(int probeIndex, const QString &msg);
    void allFilesClosed();

private:
    struct FileHandle {
        QFile *file = nullptr;
        QTextStream *stream = nullptr;
    };
    QHash<int, FileHandle> m_files;
};


// ─────────────────────────────────────────────
//  SaveManager: 主线程控制器
// ─────────────────────────────────────────────
class SaveManager : public QObject
{
    Q_OBJECT
public:
    explicit SaveManager(ProbeManager *probeManager, QObject *parent = nullptr);
    ~SaveManager() override;

    /** @brief 设置数据存储根目录 */
    void setDataFolder(const QString &folder);

    /** @brief 当前是否处于采集会话中 */
    bool isSaving() const { return m_isAcquiring; }

public slots:
    /** @brief 采集开始：创建新文件（带时间戳命名） */
    void onAcquisitionStarted();

    /** @brief 采集停止：刷盘并关闭文件 */
    void onAcquisitionStopped();

    /**
     * @brief 探头数据就绪（连接自 DataAcquisitionThread::saveDataReady）
     * @param probeIndex  探头逻辑索引
     *
     * 读取 probe->saveData() 中的数据，转发到工作线程写入文件。
     */
    void onSaveDataReady(int probeIndex);

signals:
    /** @brief 新文件已创建 */
    void newFileCreated(int probeIndex, const QString &filePath);
    /** @brief 保存出错 */
    void saveError(const QString &msg);

private:
    /** @brief 生成带时间戳的文件名 */
    QString generateFileName(int probeIndex) const;

    ProbeManager *m_probeManager = nullptr;
    QString m_dataFolder;
    bool m_isAcquiring = false;

    // 工作线程
    QThread *m_workerThread = nullptr;
    SaveWorker *m_worker = nullptr;

    // 当前会话的文件名缓存
    QHash<int, QString> m_currentFiles;
};

#endif // SAVEMANAGER_H
