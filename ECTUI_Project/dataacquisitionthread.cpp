/*
 * @Descripttion: 数据采集线程实现
 * @version: 1.0.0
 */
#include "dataacquisitionthread.h"
#include "devicemanager.h"
#include "probe.h"
#include "probemanager.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

DataAcquisitionThread::DataAcquisitionThread(DeviceManager *deviceManager,
                                             ProbeManager *probeManager,
                                             QObject *parent)
    : QThread(parent)
    , m_deviceManager(deviceManager)
    , m_probeManager(probeManager)
{
}

DataAcquisitionThread::~DataAcquisitionThread()
{
    stop();
    wait();
}

void DataAcquisitionThread::stop()
{
    // m_running.store(0, std::memory_order_relaxed);
    m_running.storeRelaxed(0);
}

bool DataAcquisitionThread::isAcquiring() const
{
    // return m_running.load(std::memory_order_relaxed) != 0;
    return m_running.loadRelaxed() != 0;
}

/**
 * @brief 注册某探头的曲线数据容器指针（采集前由主线程一次性调用）
 *
 * 采集线程 run() 拿到指针后直接 append，零拷贝、无锁。
 * 容器预分配至 CURVE_CAPACITY，保证运行期间永不触发 QVector reallocate，
 * 从而指针地址始终有效，主线程可安全读取。
 *
 * @param probeIndex      探头逻辑索引 (0-based)
 * @param impedanceCurve  阻抗平面图数据容器（t, real, imag），可为 nullptr
 * @param ampCurve        幅值时序图数据容器（key, amp），可为 nullptr
 * @param phaseCurve      相位时序图数据容器（key, phase），可为 nullptr
 */
void DataAcquisitionThread::registerCurveData(int probeIndex,
                                               QVector<QCPCurveData> *impedanceCurve,
                                               QVector<QCPGraphData> *ampCurve,
                                               QVector<QCPGraphData> *phaseCurve)
{
    // 确保 m_curveRefs 容量覆盖到该探头索引
    if (probeIndex >= m_curveRefs.size()) {
        m_curveRefs.resize(probeIndex + 1);
    }

    // 预分配容量，保证 append 永不触发 reallocate
    if (impedanceCurve) {
        impedanceCurve->reserve(CURVE_CAPACITY);
    }
    if (ampCurve) {
        ampCurve->reserve(CURVE_CAPACITY);
    }
    if (phaseCurve) {
        phaseCurve->reserve(CURVE_CAPACITY);
    }

    // 存储指针，run() 中直接 append 到这些容器
    m_curveRefs[probeIndex].impedanceCurve = impedanceCurve;
    m_curveRefs[probeIndex].ampCurve       = ampCurve;
    m_curveRefs[probeIndex].phaseCurve     = phaseCurve;
}

void DataAcquisitionThread::run()
{
    // m_running.store(1, std::memory_order_relaxed);
    m_running.storeRelaxed(1);
    m_sampleCounter = 0;

    while (m_running.loadRelaxed()) {

        bool gotData = false;
        const auto probes = m_probeManager->allProbes();

        for (int i = 0; i < probes.size(); ++i) {   // 遍历所有探头
            Probe *probe = probes[i];
            if (!probe || !probe->isEnabled())
                continue;

            const int hwChannel = probe->hardwareChannel();
            const int bufferIndex = hwChannel - 1;
            if (bufferIndex < 0 || bufferIndex >= DeviceManager::AD7768_CHANNELS)
                continue;

            auto packets = m_deviceManager->takeLockinPackets(bufferIndex, MAX_BATCH_SIZE);
            if (packets.isEmpty())
                continue;
            gotData = true;

            ProbeData *active = probe->activeData();

            // 提前取出曲线指针，避免每次 packet 都去 vector 里索引
            QVector<QCPCurveData> *impCurve = nullptr;
            QVector<QCPGraphData> *ampCurve = nullptr;
            QVector<QCPGraphData> *phaseCurve = nullptr;
            if (i < m_curveRefs.size()) {
                impCurve   = m_curveRefs[i].impedanceCurve;
                ampCurve   = m_curveRefs[i].ampCurve;
                phaseCurve = m_curveRefs[i].phaseCurve;
            }

            // 读取该探头的平衡点和旋转角，预计算三角函数
            const float balAmp   = probe->balanceAmp();
            const float balPhase = probe->balancePhase();
            const bool  hasBalance = probe->isBalanceSet();

            const float angleDeg = probe->rotationAngle();
            const bool needRotate = std::fabs(angleDeg) > 1e-6f;
            float cosA = 1.0f, sinA = 0.0f;
            if (needRotate) {
                const float rad = angleDeg * static_cast<float>(M_PI) / 180.0f;
                cosA = std::cos(rad);
                sinA = std::sin(rad);
            }

            for (const auto &packet : packets) {
                const int nPoints = packet.ampMv.size();
                if (nPoints <= 0)
                    continue;

                // 1. 写入 Probe 的活跃乒乓缓冲区（原始数据，不变换）
                if (active) {
                    active->append(packet.ampMv, packet.phaseDeg);
                }

                // 2. 同步写入曲线数据容器（减平衡点 → 旋转 → 滤波）
                for (int j = 0; j < nPoints; ++j) {
                    const double key = static_cast<double>(m_sampleCounter++);

                    float ampVal   = packet.ampMv[j];
                    float phaseVal = packet.phaseDeg[j];

                    //  减去平衡点（将图像居中）
                    if (hasBalance) {
                        ampVal   -= balAmp;
                        phaseVal -= balPhase;
                    }

                    //  相位旋转
                    if (needRotate) {
                        const float x = ampVal;
                        const float y = phaseVal;
                        ampVal   = x * cosA - y * sinA;
                        phaseVal = x * sinA + y * cosA;
                    }

                    //  滤波（仅作用于曲线显示数据）
                    if (i < m_ampFilters.size() && !m_ampFilters[i].isEmpty()) {
                        ampVal = m_ampFilters[i].process(ampVal);
                    }
                    if (i < m_phaseFilters.size() && !m_phaseFilters[i].isEmpty()) {
                        phaseVal = m_phaseFilters[i].process(phaseVal);
                    }

                    if (ampCurve) {
                        ampCurve->append(QCPGraphData(
                            key, static_cast<double>(ampVal)));
                    }
                    if (phaseCurve) {
                        phaseCurve->append(QCPGraphData(
                            key, static_cast<double>(phaseVal)));
                    }
                    if (impCurve) {
                        impCurve->append(QCPCurveData(
                            key, // t: 排序键
                            static_cast<double>(ampVal),   // key: 实部
                            static_cast<double>(phaseVal)  // value: 虚部
                        ));
                    }
                }
            }

            // 乒乓缓冲区切换 —— 仅当 active 达到保存阈值时才 swap
            if (active && active->ampSize() >= SAVE_THRESHOLD) {
                probe->swapBuffers();
                emit saveDataReady(i);
            }
        }

        // ── for 循环结束后：检查曲线数据容器是否超过阈值 ──
        for (int i = 0; i < m_curveRefs.size(); ++i) {
            auto &ref = m_curveRefs[i];
            if (ref.ampCurve && ref.ampCurve->size() > CURVE_CLEAR_SIZE) {
                ref.ampCurve->clear();
            }
            if (ref.phaseCurve && ref.phaseCurve->size() > CURVE_CLEAR_SIZE) {
                ref.phaseCurve->clear();
            }
            if (ref.impedanceCurve && ref.impedanceCurve->size() > CURVE_CLEAR_SIZE) {
                ref.impedanceCurve->clear();
            }
        }

        if (!gotData) {
            msleep(IDLE_SLEEP_MS);
        }
    }

    emit acquisitionStopped();
}

// 配置单级滤波器：先清空该探头已有的滤波链，再添加一级新滤波器。
// 幅值和相位各自独立维护一条链，但配置时保持同步（同类型、同参数）。
// 示例：thread->configureFilter(0, FilterType::LowPass, 5000, 100000);
void DataAcquisitionThread::configureFilter(int probeIndex, FilterType type,
                                            float cutoffHz, float sampleRateHz, float q)
{
    // 确保 amp/phase 的 QVector 长度覆盖到该探头索引
    if (probeIndex >= m_ampFilters.size()) {
        m_ampFilters.resize(probeIndex + 1);
    }
    if (probeIndex >= m_phaseFilters.size()) {
        m_phaseFilters.resize(probeIndex + 1);
    }

    // 清空旧链 → 添加新的一级
    m_ampFilters[probeIndex].clear();
    m_ampFilters[probeIndex].addStage(type, cutoffHz, sampleRateHz, q);

    m_phaseFilters[probeIndex].clear();
    m_phaseFilters[probeIndex].addStage(type, cutoffHz, sampleRateHz, q);
}

// 在现有滤波链末尾追加一级（不清除已有的）。
// 典型用法：先 configureFilter 设高通，再 addFilterStage 追加低通 → 形成带通。
// 示例：
//   thread->configureFilter(0, FilterType::HighPass, 100, 100000);   // 第1级: HP 100Hz
//   thread->addFilterStage(0, FilterType::LowPass, 5000, 100000);     // 第2级: LP 5000Hz
//   数据流: 原始数据 → HP(100Hz) → LP(5000Hz) → 曲线
void DataAcquisitionThread::addFilterStage(int probeIndex, FilterType type,
                                           float cutoffHz, float sampleRateHz, float q)
{
    if (probeIndex >= m_ampFilters.size()) {
        m_ampFilters.resize(probeIndex + 1);
    }
    if (probeIndex >= m_phaseFilters.size()) {
        m_phaseFilters.resize(probeIndex + 1);
    }

    m_ampFilters[probeIndex].addStage(type, cutoffHz, sampleRateHz, q);
    m_phaseFilters[probeIndex].addStage(type, cutoffHz, sampleRateHz, q);
}

// 清除指定探头的全部滤波级，恢复原始数据直通（不过滤）。
// 注意：clear() 后 FilterChain::isEmpty() 为 true，run() 中会跳过滤波分支。
void DataAcquisitionThread::removeFilter(int probeIndex)
{
    if (probeIndex < m_ampFilters.size()) {
        m_ampFilters[probeIndex].clear();
    }
    if (probeIndex < m_phaseFilters.size()) {
        m_phaseFilters[probeIndex].clear();
    }
}

float DataAcquisitionThread::sampleRateHz() const
{
    return static_cast<float>(static_cast<quint32>(m_deviceManager->currentSampleRate()));
}
