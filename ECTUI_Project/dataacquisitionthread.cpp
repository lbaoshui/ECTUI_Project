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

void DataAcquisitionThread::setRotationAngle(float angleDeg)
{
    m_rotationAngleDeg.store(angleDeg, std::memory_order_relaxed);
}

void DataAcquisitionThread::registerCurveData(int probeIndex,
                                               QVector<QCPCurveData> *impedanceCurve,
                                               QVector<QCPGraphData> *ampCurve,
                                               QVector<QCPGraphData> *phaseCurve)
{
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
        // 每轮循环读一次旋转角，预计算三角函数
        const float angleDeg = m_rotationAngleDeg.load(std::memory_order_relaxed);
        const bool needRotate = std::fabs(angleDeg) > 1e-6f;
        float cosA = 1.0f, sinA = 0.0f;
        if (needRotate) {
            const float rad = angleDeg * static_cast<float>(M_PI) / 180.0f;
            cosA = std::cos(rad);
            sinA = std::sin(rad);
        }

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

            for (const auto &packet : packets) {
                const int nPoints = packet.ampMv.size();
                if (nPoints <= 0)
                    continue;

                // 1. 写入 Probe 的活跃乒乓缓冲区 
                if (active) {
                    active->append(packet.ampMv, packet.phaseDeg);  // 存储的数据为原始数据
                }

                // 2. 同步写入曲线数据容器（应用相位旋转）
                for (int j = 0; j < nPoints; ++j) {
                    const double key = static_cast<double>(m_sampleCounter++);

                    // 将数据进行相位旋转
                    float ampVal   = packet.ampMv[j];
                    float phaseVal = packet.phaseDeg[j];
                    if (needRotate) {
                        const float x = ampVal;
                        const float y = phaseVal;
                        ampVal   = x * cosA - y * sinA;
                        phaseVal = x * sinA + y * cosA;
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

            emit dataReady(i);
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
