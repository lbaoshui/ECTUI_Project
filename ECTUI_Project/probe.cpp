/*
 * @Descripttion: 单个探头数据封装实现
 * @version: 2.0.0
 */
#include "probe.h"

#include <QMutexLocker>
#include <algorithm>

/**
 * @brief 内部常量：Vpp 计算缩放系数
 *
 * 由硬件增益与 ADC 分辨率换算得到，用于将原始采样值转换为实际电压。
 */
/*
 * @Descripttion: 单个探头数据封装实现
 * @version: 2.0.0
 */
#include "probe.h"

#include <QMutexLocker>
#include <algorithm>

/**
 * @brief 内部常量：Vpp 计算缩放系数
 *
 * 由硬件增益与 ADC 分辨率换算得到，用于将原始采样值转换为实际电压。
 */
namespace {

constexpr float kVppScale = 1.03f * 0.122f;

}

ProbeData::ProbeData() {}

ProbeData::~ProbeData()
{
    release();
}

// 深拷贝构造函数
ProbeData::ProbeData(const ProbeData& other)
{
    if (other.m_rawData_amp != nullptr) {
        m_rawData_amp = new QVector<float>(*other.m_rawData_amp);
    }
    if (other.m_rawData_phase != nullptr) {
        m_rawData_phase = new QVector<float>(*other.m_rawData_phase);
    }
}

// 深拷贝赋值运算符
ProbeData& ProbeData::operator=(const ProbeData& other)
{
    if (this != &other) {
        // 智能重用已有内存，仅在大小不一致时才重新分配
        if (m_rawData_amp != nullptr && other.m_rawData_amp != nullptr && m_rawData_amp->size() == other.m_rawData_amp->size()) {
            *m_rawData_amp = *other.m_rawData_amp;
        } else {
            delete m_rawData_amp;
            m_rawData_amp = other.m_rawData_amp ? new QVector<float>(*other.m_rawData_amp) : nullptr;
        }

        if (m_rawData_phase != nullptr && other.m_rawData_phase != nullptr && m_rawData_phase->size() == other.m_rawData_phase->size()) {
            *m_rawData_phase = *other.m_rawData_phase;
        } else {
            delete m_rawData_phase;
            m_rawData_phase = other.m_rawData_phase ? new QVector<float>(*other.m_rawData_phase) : nullptr;
        }
    }
    return *this;
}

// 移动构造函数 (轻量化转移所有权，常用于乒乓缓冲区的快速指针切换)
ProbeData::ProbeData(ProbeData&& other) noexcept
    : m_rawData_amp(other.m_rawData_amp),
      m_rawData_phase(other.m_rawData_phase)
{
    other.m_rawData_amp = nullptr;
    other.m_rawData_phase = nullptr;
}

// 移动赋值运算符
ProbeData& ProbeData::operator=(ProbeData&& other) noexcept
{
    if (this != &other) {
        release();
        m_rawData_amp = other.m_rawData_amp;
        m_rawData_phase = other.m_rawData_phase;
        other.m_rawData_amp = nullptr;
        other.m_rawData_phase = nullptr;
    }
    return *this;
}

// 统一分配内存大小 (智能重用：大小匹配则跳过 delete/new，避免内存碎片和耗时)
void ProbeData::AssignedMemoryForProbeData(int ampSize, int phaseSize)
{
    if (m_rawData_amp != nullptr && m_rawData_amp->size() == ampSize &&
        m_rawData_phase != nullptr && m_rawData_phase->size() == phaseSize) {
        return;
    }

    if (m_rawData_amp != nullptr) {
        delete m_rawData_amp;
    }
    if (m_rawData_phase != nullptr) {
        delete m_rawData_phase;
    }
    m_rawData_amp = new QVector<float>(ampSize, 0.0f);
    m_rawData_phase = new QVector<float>(phaseSize, 0.0f);
}

// 释放底层 QVector 指针内存并置空
void ProbeData::release()
{
    if (m_rawData_amp != nullptr) {
        delete m_rawData_amp;
        m_rawData_amp = nullptr;
    }
    if (m_rawData_phase != nullptr) {
        delete m_rawData_phase;
        m_rawData_phase = nullptr;
    }
}

// 清空数据（大小设为 0，但保留已分配的内存容量）
void ProbeData::clear()
{
    if (m_rawData_amp != nullptr) {
        m_rawData_amp->clear();
    }
    if (m_rawData_phase != nullptr) {
        m_rawData_phase->clear();
    }
}

// 分配内存并以 0 填充
void ProbeData::fillZero(int ampSize, int phaseSize)
{
    AssignedMemoryForProbeData(ampSize, phaseSize);
    if (m_rawData_amp != nullptr) {
        m_rawData_amp->fill(0.0f);
    }
    if (m_rawData_phase != nullptr) {
        m_rawData_phase->fill(0.0f);
    }
}

// 批量追加数据（可用于乒乓缓冲区的数据累积暂存）
void ProbeData::append(const QVector<float>& amp, const QVector<float>& phase)
{
    if (m_rawData_amp == nullptr) {
        m_rawData_amp = new QVector<float>();
    }
    if (m_rawData_phase == nullptr) {
        m_rawData_phase = new QVector<float>();
    }
    m_rawData_amp->append(amp);
    m_rawData_phase->append(phase);
}

// 追加单个数据点
void ProbeData::appendPoint(float ampVal, float phaseVal)
{
    if (m_rawData_amp == nullptr) {
        m_rawData_amp = new QVector<float>();
    }
    if (m_rawData_phase == nullptr) {
        m_rawData_phase = new QVector<float>();
    }
    m_rawData_amp->append(ampVal);
    m_rawData_phase->append(phaseVal);
}

// 快速指针交换操作，O(1) 复杂度，常用于乒乓缓冲区的快速无锁轮转
void ProbeData::swap(ProbeData& other) noexcept
{
    std::swap(m_rawData_amp, other.m_rawData_amp);
    std::swap(m_rawData_phase, other.m_rawData_phase);
}

// 判断数据是否为空
bool ProbeData::isEmpty() const
{
    return (m_rawData_amp == nullptr || m_rawData_amp->isEmpty());
}

// 获取幅值数据大小
int ProbeData::ampSize() const
{
    return m_rawData_amp ? m_rawData_amp->size() : 0;
}

// 获取相位数据大小
int ProbeData::phaseSize() const
{
    return m_rawData_phase ? m_rawData_phase->size() : 0;
}

/**
 * @brief 构造一个 Probe 实例
 * @param probeId        探头逻辑编号（从 0 开始）
 * @param hardwareChannel 对应的硬件通道号（1-16）
 * @param parent         父 QObject
 */
Probe::Probe(int probeId, int hardwareChannel, QObject *parent)
    : QObject(parent),
      m_id(probeId),
      m_hwChannel(hardwareChannel),
      m_name(QStringLiteral("探头-%1").arg(probeId + 1))
{
    m_activeData = new ProbeData();
    m_activeData->AssignedMemoryForProbeData(DeviceManager::ADC_SAMPLES_buffer_PER_CH, DeviceManager::ADC_SAMPLES_buffer_PER_CH);
    m_saveData = new ProbeData();
}

Probe::~Probe()
{
    delete m_activeData;
    m_activeData = nullptr;
    delete m_saveData;
    m_saveData = nullptr;
}

void Probe::swapBuffers()
{
    QMutexLocker lock(&m_bufferMutex);
    std::swap(m_activeData, m_saveData);
}

// 设置对应的硬件通道，按照该硬件通道读取数据
void Probe::setHardwareChannel(int channel)
{
    m_hwChannel = channel;   
}

/**
 * @brief 设置 DA 激励参数
 * @param freq  激励频率 (Hz)
 * @param phase 激励相位 (度)
 * @param amp   激励幅度 (%)
 */

void Probe::setExcitation(int freq, int phase, int amp)
{
    m_excitationFreq = freq;
    m_excitationPhase = phase;
    m_excitationAmp = amp;
}

/**
 * @brief 将当前激励配置转换为 DA 通道配置结构体
 * @return DaChannelConfig，可直接传给 DeviceManager 发送
 */
DaChannelConfig Probe::toDaChannelConfig() const
{
    return {
        m_hwChannel,
        1,
        m_excitationFreq,
        m_excitationPhase,
        m_excitationAmp
    };
}

/**
 * @brief 启用或禁用该探头
 * @param enabled true 为启用，false 为禁用
 */
void Probe::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    emit enabledChanged(enabled);
}

/**
 * @brief 采集当前 Vpp 作为基线
 *
 * 将当前计算出的 m_vpp保存为 baseline，用于后续灵敏度计算。
 */
void Probe::captureBaseline()
{
    m_baselineVpp = m_vpp;
    m_baselineSet = true;
    emit baselineCaptured(m_baselineVpp);
}

/**
 * @brief 清除已保存的基线
 *
 * 复位 baseline 与灵敏度，避免旧数据影响后续测量。
 */
void Probe::clearBaseline()
{
    m_baselineVpp = 0.0f;
    m_baselineSet = false;
    m_sensitivity = 0.0f;
    emit sensitivityChanged(0.0f);
}

/**
 * @brief 根据当前 rawData 重新计算 Vpp
 *
 * 若新值与旧值差异极小（浮点比较），则跳过更新以减少信号发射。
 */
void Probe::updateVpp()
{
    const float newVpp = computeVppInternal();
    if (qFuzzyCompare(newVpp, m_vpp)) {
        return;
    }
    m_vpp = newVpp;
    emit vppChanged(m_vpp);
}

/**
 * @brief 基于当前 Vpp 与基线计算灵敏度
 *
 * 公式：((Vpp - baseline) / baseline) * 100%
 * 若变化幅度在 ±2% 以内，视为无变化，灵敏度归零。
 */
void Probe::updateSensitivity()
{
    if (!m_baselineSet || qFuzzyIsNull(m_baselineVpp)) {
        m_sensitivity = 0.0f;
    } else {
        m_sensitivity = ((m_vpp - m_baselineVpp) / m_baselineVpp) * 100.0f;
        if (m_sensitivity >= -2.0f && m_sensitivity <= 2.0f) {
            m_sensitivity = 0.0f;
        }
    }
    emit sensitivityChanged(m_sensitivity);
}

/**
 * @brief 内部 Vpp 计算：去极值平均法
 * @return 计算得到的峰峰值电压
 *
 * 对实时幅值数据排序后，各取最小的 5 个和最大的 5 个做平均，
 * 以消除毛刺和极端噪声，再乘以缩放系数得到实际电压。
 */
float Probe::computeVppInternal() const
{
    if (m_saveData == nullptr || m_saveData->m_rawData_amp == nullptr || m_saveData->m_rawData_amp->isEmpty()) {
        return 0.0f;
    }

    QVector<float> sorted = *(m_saveData->m_rawData_amp);
    std::sort(sorted.begin(), sorted.end());

    const int sampleCount = std::min(5, static_cast<int>(sorted.size()));
    float bottomSum = 0;
    float topSum = 0;

    for (int i = 0; i < sampleCount; ++i) {
        bottomSum += sorted[i];
        topSum += sorted[sorted.size() - 1 - i];
    }

    const float bottomAvg = bottomSum / sampleCount;
    const float topAvg = topSum / sampleCount;
    return (topAvg - bottomAvg) * kVppScale;
}
