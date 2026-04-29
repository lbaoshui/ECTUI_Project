/*
 * @Descripttion: 单个探头数据封装实现
 * @version: 2.0.0
 */
#include "probe.h"

#include <QMutexLocker>
#include <algorithm>

namespace {

constexpr float kVppScale = 1.03f * 0.122f;

}

Probe::Probe(int probeId, int hardwareChannel, QObject *parent)
    : QObject(parent),
      m_id(probeId),
      m_hwChannel(hardwareChannel),
      m_name(QStringLiteral("探头-%1").arg(probeId + 1))
{
    m_rawData.fill(0, DeviceManager::ADC_SAMPLES_PER_CH);
}

void Probe::setExcitation(int freq, int phase, int amp)
{
    m_excitationFreq = freq;
    m_excitationPhase = phase;
    m_excitationAmp = amp;
}

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

QVector<quint16> Probe::rawData() const
{
    return m_rawData;
}

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

void Probe::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    emit enabledChanged(enabled);
}

void Probe::captureBaseline()
{
    m_baselineVpp = m_vpp;
    m_baselineSet = true;
    emit baselineCaptured(m_baselineVpp);
}

void Probe::clearBaseline()
{
    m_baselineVpp = 0.0f;
    m_baselineSet = false;
    m_sensitivity = 0.0f;
    emit sensitivityChanged(0.0f);
}

void Probe::updateVpp()
{
    const float newVpp = computeVppInternal();
    if (qFuzzyCompare(newVpp, m_vpp)) {
        return;
    }
    m_vpp = newVpp;
    emit vppChanged(m_vpp);
}

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

float Probe::computeVppInternal() const
{
    if (m_rawData.isEmpty()) {
        return 0.0f;
    }

    QVector<quint16> sorted = m_rawData;
    std::sort(sorted.begin(), sorted.end());

    const int sampleCount = std::min(5, sorted.size());
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
