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
    m_rawData.fill(0, DeviceManager::ADC_SAMPLES_PER_CH);
}

void Probe::setHardwareChannel(int channel)
{
    m_hwChannel = channel;
}

<<<<<<< HEAD
/**
 * @brief 设置 DA 激励参数
 * @param freq  激励频率 (Hz)
 * @param phase 激励相位 (度)
 * @param amp   激励幅度 (%)
 */
=======
>>>>>>> 3244408ef6c9a84723214752b612c722ab5eba91
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
 * @brief 获取最近一次写入的原始 ADC 数据
 */
QVector<quint16> Probe::rawData() const
{
    return m_rawData;
}

/**
 * @brief 写入原始 ADC 数据并触发后续计算
 * @param data 来自 DeviceManager 的原始采样值
 *
 * 若数据长度异常，则标记故障状态；否则更新数据并通知界面刷新。
 */
void Probe::setRawData(const QVector<quint16> &data)
{
    if (data.size() != DeviceManager::ADC_SAMPLES_PER_CH) {
        if (!m_hasFault) {
            m_hasFault = true;
            emit faultStateChanged(true);
        }
        return;
    }

    if (m_hasFault) {
        m_hasFault = false;
        emit faultStateChanged(false);
    }

    m_rawData = data;
    m_lastUpdateTime = QDateTime::currentDateTime();
    emit dataUpdated();
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
 * 将当前计算出的 m_vpp 保存为 baseline，用于后续灵敏度计算。
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
 * 对原始数据排序后，各取最小的 5 个和最大的 5 个做平均，
 * 以消除毛刺和极端噪声，再乘以缩放系数得到实际电压。
 */
float Probe::computeVppInternal() const
{
    if (m_rawData.isEmpty()) {
        return 0.0f;
    }

    QVector<quint16> sorted = m_rawData;
    std::sort(sorted.begin(), sorted.end());

    const int sampleCount = std::min(5, static_cast<int>(sorted.size()));
    quint64 bottomSum = 0;
    quint64 topSum = 0;

    for (int i = 0; i < sampleCount; ++i) {
        bottomSum += sorted[i];
        topSum += sorted[sorted.size() - 1 - i];
    }

    const float bottomAvg = static_cast<float>(bottomSum) / sampleCount;
    const float topAvg = static_cast<float>(topSum) / sampleCount;
    return (topAvg - bottomAvg) * kVppScale;
}
