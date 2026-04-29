/*
 * @Descripttion: 探头管理器实现
 * @version: 2.0.0
 */
#include "probemanager.h"

#include <QDebug>

ProbeManager::ProbeManager(QObject *parent)
    : QObject(parent)
{
}

void ProbeManager::setProbeCount(int count)
{
    if (count < 0 || count > DeviceManager::ADC_CHANNELS) {
        qWarning() << "探头数量非法:" << count;
        return;
    }

    const int oldCount = m_probes.size();
    if (count == oldCount) {
        return;
    }

    if (count > oldCount) {
        // 增加探头
        for (int i = oldCount; i < count; ++i) {
            int hwChannel = (i < DeviceManager::ADC_CHANNELS) ? (i + 1) : (i + 1);
            auto probe = QSharedPointer<Probe>::create(i, hwChannel, this);
            m_probes.append(probe);
            emit probeAdded(probe.data());
        }
    } else {
        // 减少探头，从尾部移除
        for (int i = oldCount - 1; i >= count; --i) {
            emit probeRemoved(i);
            m_probes.removeAt(i);
        }
    }

    rebuildChannelIndex();
    emit probeCountChanged(count);
}

Probe *ProbeManager::probeAt(int index) const
{
    if (index < 0 || index >= m_probes.size()) {
        return nullptr;
    }
    return m_probes[index].data();
}

Probe *ProbeManager::probeByHardwareChannel(int hwChannel) const
{
    if (!m_hwChannelToProbeIndex.contains(hwChannel)) {
        return nullptr;
    }
    return m_probes[m_hwChannelToProbeIndex[hwChannel]].data();
}

QVector<Probe *> ProbeManager::allProbes() const
{
    QVector<Probe *> result;
    result.reserve(m_probes.size());
    for (const auto &probe : m_probes) {
        result.append(probe.data());
    }
    return result;
}

void ProbeManager::setProbeHardwareChannel(int probeIndex, int hwChannel)
{
    if (probeIndex < 0 || probeIndex >= m_probes.size()) {
        return;
    }
    if (hwChannel < 1 || hwChannel > DeviceManager::ADC_CHANNELS) {
        qWarning() << "硬件通道号非法:" << hwChannel;
        return;
    }
    m_probes[probeIndex]->m_hwChannel = hwChannel;
    rebuildChannelIndex();
}

void ProbeManager::setChannelMapping(const QVector<int> &mapping)
{
    if (mapping.size() != m_probes.size()) {
        qWarning() << "通道映射数量与探头数量不匹配";
        return;
    }

    for (int i = 0; i < mapping.size(); ++i) {
        int hwChannel = mapping[i];
        if (hwChannel < 1 || hwChannel > DeviceManager::ADC_CHANNELS) {
            qWarning() << "非法硬件通道号:" << hwChannel;
            continue;
        }
        m_probes[i]->m_hwChannel = hwChannel;
    }

    rebuildChannelIndex();
}

QVector<int> ProbeManager::channelMapping() const
{
    QVector<int> mapping;
    mapping.reserve(m_probes.size());
    for (const auto &probe : m_probes) {
        mapping.append(probe->hardwareChannel());
    }
    return mapping;
}

void ProbeManager::dispatchAdcData(const QVector<AdcChannelData> &adcData)
{
    for (const AdcChannelData &chData : adcData) {
        Probe *probe = probeByHardwareChannel(chData.ch);
        if (!probe) {
            continue;
        }

        probe->setRawData(chData.data);
        probe->updateVpp();
        probe->updateSensitivity();
    }
}

QVector<DaChannelConfig> ProbeManager::buildDaConfig() const
{
    // DeviceManager 要求固定 16 个通道
    QVector<DaChannelConfig> result(DeviceManager::DA_CHANNELS);

    // 默认全部关闭（幅度为 0）
    for (int i = 0; i < DeviceManager::DA_CHANNELS; ++i) {
        result[i] = {i + 1, 1, 10000, 0, 0};
    }

    // 用实际启用的 Probe 配置覆盖
    for (const auto &probe : m_probes) {
        if (!probe->isEnabled()) {
            continue;
        }
        int hwCh = probe->hardwareChannel();
        if (hwCh >= 1 && hwCh <= DeviceManager::DA_CHANNELS) {
            result[hwCh - 1] = probe->toDaChannelConfig();
        }
    }

    return result;
}

void ProbeManager::captureAllBaselines()
{
    for (const auto &probe : m_probes) {
        if (probe->isEnabled()) {
            probe->captureBaseline();
        }
    }
}

void ProbeManager::clearAllBaselines()
{
    for (const auto &probe : m_probes) {
        probe->clearBaseline();
    }
}

void ProbeManager::setAllEnabled(bool enabled)
{
    for (const auto &probe : m_probes) {
        probe->setEnabled(enabled);
    }
}

void ProbeManager::updateAllVpp()
{
    for (const auto &probe : m_probes) {
        probe->updateVpp();
    }
}

void ProbeManager::updateAllSensitivity()
{
    for (const auto &probe : m_probes) {
        probe->updateSensitivity();
    }
}

void ProbeManager::rebuildChannelIndex()
{
    m_hwChannelToProbeIndex.clear();
    for (int i = 0; i < m_probes.size(); ++i) {
        m_hwChannelToProbeIndex[m_probes[i]->hardwareChannel()] = i;
    }
}
