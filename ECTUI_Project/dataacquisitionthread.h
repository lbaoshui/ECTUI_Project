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
#include <QVector>
#include <QAtomicInt>
#include <atomic>
#include "filter.h"
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

    /** @brief 设置相位旋转角度（度），主线程可随时调用 */
    void setRotationAngle(float angleDeg);

    /**
     * @brief 注册某个探头对应的曲线数据指针
     * @param probeIndex      探头逻辑索引
     * @param impedanceCurve  阻抗图曲线容器指针
     * @param ampCurve        幅值曲线容器指针
     * @param phaseCurve      相位曲线容器指针
     *
     * 调用方需将容器 capacity 预分配至 CURVE_CAPACITY，
     * 确保 append 操作永不触发 QVector 内部 reallocate。
     */
    void registerCurveData(int probeIndex,
                           QVector<QCPCurveData> *impedanceCurve,
                           QVector<QCPGraphData> *ampCurve,
                           QVector<QCPGraphData> *phaseCurve);

    /** @brief 为指定探头配置单级滤波器（会先清除已有滤波链） */
    void configureFilter(int probeIndex, FilterType type,
                         float cutoffHz, float sampleRateHz, float q = 0.7071f);
    /** @brief 在已有滤波链末尾追加一级（HP+LP 级联即带通） */
    void addFilterStage(int probeIndex, FilterType type,
                        float cutoffHz, float sampleRateHz, float q = 0.7071f);
    /** @brief 获取当前设备采样率 (Hz)，用于滤波器系数计算 */
    float sampleRateHz() const;
    /** @brief 清除指定探头的滤波链（恢复直通） */
    void removeFilter(int probeIndex);

signals:
    /** @brief 某个探头的新数据已写入 saveData，主线程可读取并更新 UI */
    void dataReady(int probeIndex);
    /** @brief active 缓冲区达到保存阈值，saveData 中已有完整数据待落盘 */
    void saveDataReady(int probeIndex);
    /** @brief 采集线程已完全停止 */
    void acquisitionStopped();

protected:
    void run() override;

private:
    DeviceManager *m_deviceManager;
    ProbeManager *m_probeManager;
    // std::atomic<unsigned int> m_running;
    QAtomicInt m_running;
    std::atomic<float> m_rotationAngleDeg{0.0f};  // 相位旋转角度，主线程写，采集线程读

    struct CurveRef {
        QVector<QCPCurveData> *impedanceCurve = nullptr;
        QVector<QCPGraphData> *ampCurve       = nullptr;
        QVector<QCPGraphData> *phaseCurve     = nullptr;
    };
    QVector<CurveRef> m_curveRefs;

    QVector<FilterChain> m_ampFilters;    // 每探头幅值滤波链(即可在现有滤波链路的后面继续添加滤波节点)
    QVector<FilterChain> m_phaseFilters;  // 每探头相位滤波链

    quint64 m_sampleCounter = 0;

    static constexpr int SAVE_THRESHOLD   = 100000; // active 缓冲区达到此点数时触发 swap 和保存
    static constexpr int CURVE_CAPACITY   = 310000; // 预分配容量，大于清除阈值确保 append 不触发扩容
    static constexpr int CURVE_CLEAR_SIZE = 300000; // 超过此大小时 clear
    static constexpr int MAX_BATCH_SIZE   = 50;
    static constexpr int IDLE_SLEEP_MS    = 1;
};

#endif // DATAACQUISITIONTHREAD_H
