/*
 * @Descripttion: 数据采集线程（继承 QThread）
 * @version: 1.0.0
 * @Author: June0821
 * @Date: 2026-05-31
 *
 * 负责从 DeviceManager 的环形缓冲区中消费数据，
 * 分发到 Probe 的乒乓缓冲区及曲线数据容器中。
 */
#ifndef DATAACQUISITIONTHREAD_H
#define DATAACQUISITIONTHREAD_H

#include <QThread>
#include <QMutex>
#include <QVector>
#include <QAtomicInt>
#include "qcustomplot.h"

class DeviceManager;
class ProbeManager;

class DataAcquisitionThread : public QThread
{
    Q_OBJECT

public:
    explicit DataAcquisitionThread(DeviceManager *deviceManager,
                                   ProbeManager *probeManager,
                                   QObject *parent = nullptr);
    ~DataAcquisitionThread() override;

    void stop();
    bool isAcquiring() const;

    /**
     * @brief 注册某个探头对应的曲线数据指针
     * @param probeIndex 探头逻辑索引
     * @param ampCurve   幅值曲线容器指针（key=采样序号, value=幅值mV）
     * @param phaseCurve 相位曲线容器指针（key=采样序号, value=相位deg）
     *
     * 曲线容器由外部（如 MainWindow / ProbObject）创建并持有，
     * 本线程仅负责写入，每条曲线最多保留 MAX_CURVE_POINTS 个数据点。
     */
    void registerCurveData(int probeIndex,
                           QVector<QCPGraphData> *ampCurve,
                           QVector<QCPGraphData> *phaseCurve);

    /** @brief 获取曲线数据互斥锁，主线程读取曲线数据时需加锁 */
    QMutex *curveMutex() { return &m_curveMutex; }

signals:
    /** @brief 某个探头的新数据已写入 saveData，主线程可读取并更新 UI */
    void dataReady(int probeIndex);
    /** @brief 采集线程已完全停止 */
    void acquisitionStopped();

protected:
    void run() override;

private:
    void appendCurvePoint(QVector<QCPGraphData> *curve, double key, double value);

    DeviceManager *m_deviceManager;
    ProbeManager *m_probeManager;
    QAtomicInt m_running;

    struct CurveRef {
        QVector<QCPGraphData> *ampCurve = nullptr;
        QVector<QCPGraphData> *phaseCurve = nullptr;
    };
    QVector<CurveRef> m_curveRefs;
    QMutex m_curveMutex;

    quint64 m_sampleCounter = 0;

    static constexpr int MAX_CURVE_POINTS = 10000;
    static constexpr int MAX_BATCH_SIZE = 50;
    static constexpr int IDLE_SLEEP_MS = 1;
};

#endif // DATAACQUISITIONTHREAD_H
