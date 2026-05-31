/*
 * @Descripttion: 数据采集线程实现
 * @version: 1.0.0
 */
#include "dataacquisitionthread.h"
#include "devicemanager.h"
#include "probe.h"
#include "probemanager.h"

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
    m_running.store(0, std::memory_order_relaxed);
}

bool DataAcquisitionThread::isAcquiring() const
{
    return m_running.load(std::memory_order_relaxed) != 0;
}

// 注册探头对应的实时曲线容器（由主线程在启动采集前调用）
// probeIndex 与 ProbeManager::allProbes() 的下标一致，run() 中按同一索引写入
void DataAcquisitionThread::registerCurveData(int probeIndex, QVector<QCPCurveData> *phaseCurve, QVector<QCPGraphData> *ampCurve, QVector<QCPGraphData> *phaseCurve)
{
    // 与 run() 内曲线写入共用互斥锁，避免注册与追加并发冲突
    QMutexLocker lock(&m_curveMutex);
    // 按需扩展引用表，保证 probeIndex 下标有效
    if (probeIndex >= m_curveRefs.size()) {
        m_curveRefs.resize(probeIndex + 1);
    }
    // 仅保存指针，容器生命周期由调用方（如 MainWindow）管理
    m_curveRefs[probeIndex].ampCurve = ampCurve;
    m_curveRefs[probeIndex].phaseCurve = phaseCurve;
}

void DataAcquisitionThread::run()
{
    m_running.store(1, std::memory_order_relaxed);
    m_sampleCounter = 0;

    while (m_running.load(std::memory_order_relaxed)) {
        bool gotData = false;
        const auto probes = m_probeManager->allProbes();

        for (int i = 0; i < probes.size(); ++i) {
            Probe *probe = probes[i];
            if (!probe || !probe->isEnabled())
                continue;

            // 硬件通道号 (1-16) → 环形缓冲区索引 (0-7)
            const int hwChannel = probe->hardwareChannel();
            const int bufferIndex = hwChannel - 1;
            if (bufferIndex < 0 || bufferIndex >= DeviceManager::AD7768_CHANNELS)
                continue;

            // 从无锁环形缓冲区批量取帧
            auto packets = m_deviceManager->takeLockinPackets(bufferIndex, MAX_BATCH_SIZE);
            if (packets.isEmpty())
                continue;
            gotData = true;

            ProbeData *active = probe->activeData();

            for (const auto &packet : packets) {
                const int nPoints = packet.ampMv.size();
                if (nPoints <= 0)
                    continue;

                // 1. 写入 Probe 的活跃乒乓缓冲区（批量追加）
                if (active) {
                    active->append(packet.ampMv, packet.phaseDeg);
                }

                // 2. 同步写入曲线数据容器
                {
                    QMutexLocker lock(&m_curveMutex);
                    if (i < m_curveRefs.size()) {
                        auto *ampCurve = m_curveRefs[i].ampCurve;
                        auto *phaseCurve = m_curveRefs[i].phaseCurve;

                        for (int j = 0; j < nPoints; ++j) {
                            const double key = static_cast<double>(m_sampleCounter++);
                            if (ampCurve) {
                                appendCurvePoint(ampCurve, key,
                                                 static_cast<double>(packet.ampMv[j]));
                            }
                            if (phaseCurve) {
                                appendCurvePoint(phaseCurve, key,
                                                 static_cast<double>(packet.phaseDeg[j]));
                            }
                        }
                    }
                }
            }

            // 乒乓缓冲区切换：将写满的 activeData 与 saveData 交换
            // 主线程收到 dataReady 信号后即可安全读取 saveData
            if (active && !active->isEmpty()) {
                probe->swapBuffers();
            }

            emit dataReady(i);
        }

        if (!gotData) {
            msleep(IDLE_SLEEP_MS);
        }
    }

    emit acquisitionStopped();
}

void DataAcquisitionThread::appendCurvePoint(QVector<QCPGraphData> *curve,
                                              double key, double value)
{
    curve->append(QCPGraphData(key, value));
    if (curve->size() > MAX_CURVE_POINTS) {
        curve->remove(0, curve->size() - MAX_CURVE_POINTS);
    }
}
