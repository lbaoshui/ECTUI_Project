/*
 * @Descripttion: 探头管理器实现
 * @version: 2.0.0
 */
#include "probemanager.h"

#include <QDebug>

/**
 * @brief 构造探头管理器
 * @param parent 父 QObject
 */
ProbeManager::ProbeManager(QObject *parent)
    : QObject(parent)
{
}

/**
 * @brief 设置当前探头数量
 * @param count 目标数量（0 ~ ADC_CHANNELS）
 *
 * 增加时默认顺序分配硬件通道；减少时从尾部移除。
 * 操作完成后会自动重建通道索引并发送 probeCountChanged。
 */
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

/**
 * @brief 按逻辑索引获取探头
 * @param index 探头索引
 * @return Probe 指针；越界时返回 nullptr
 */
Probe *ProbeManager::probeAt(int index) const
{
    if (index < 0 || index >= m_probes.size()) {
        return nullptr;
    }
    return m_probes[index].data();
}

/**
 * @brief 按硬件通道号查找探头
 * @param hwChannel 硬件通道号（1-16）
 * @return 对应的 Probe 指针；未找到时返回 nullptr
 */
Probe *ProbeManager::probeByHardwareChannel(int hwChannel) const
{
    if (!m_hwChannelToProbeIndex.contains(hwChannel)) {
        return nullptr;
    }
    return m_probes[m_hwChannelToProbeIndex[hwChannel]].data();
}

/**
 * @brief 获取所有探头的裸指针列表
 * @return QVector<Probe *>
 *
 * 主要用于 QML 或界面层遍历，生命周期仍由内部 QSharedPointer 管理。
 */
QVector<Probe *> ProbeManager::allProbes() const
{
    QVector<Probe *> result;
    result.reserve(m_probes.size());
    for (const auto &probe : m_probes) {
        result.append(probe.data());
    }
    return result;
}

/**
 * @brief 为指定探头设置硬件通道
 * @param probeIndex 探头逻辑索引
 * @param hwChannel  硬件通道号（1-16）
 *
 * 修改后会自动重建通道索引，确保数据分发正确。
 */
void ProbeManager::setProbeHardwareChannel(int probeIndex, int hwChannel)
{
    if (probeIndex < 0 || probeIndex >= m_probes.size()) {
        return;
    }
    if (hwChannel < 1 || hwChannel > DeviceManager::ADC_CHANNELS) {
        qWarning() << "硬件通道号非法:" << hwChannel;
        return;
    }
   m_probes[probeIndex]->setHardwareChannel(hwChannel);
    rebuildChannelIndex();
}

/**
 * @brief 批量设置探头与硬件通道的映射关系
 * @param mapping 长度需与当前探头数量一致，每个元素为硬件通道号
 *
 * 非法通道号会被跳过并输出警告，合法值生效后会重建索引。
 */
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
        m_probes[i]->setHardwareChannel(hwChannel);
    }

    rebuildChannelIndex();
}

/**
 * @brief 获取当前所有探头对应的硬件通道号列表
 * @return 通道号 QVector<int>
 */
QVector<int> ProbeManager::channelMapping() const
{
    QVector<int> mapping;
    mapping.reserve(m_probes.size());
    for (const auto &probe : m_probes) {
        mapping.append(probe->hardwareChannel());
    }
    return mapping;
}

/**
 * @brief 将 ADC 原始数据分发给各探头
 * @param adcData 来自 DeviceManager 的多通道数据
 *
 * 根据硬件通道号匹配 Probe，写入数据后依次更新 Vpp 与灵敏度。
 */
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

/**
 * @brief 构建 16 通道 DA 配置帧
 * @return QVector<DaChannelConfig>，长度固定为 DA_CHANNELS
 *
 * 默认所有通道关闭（幅度 0）；仅将已启用探头的配置覆盖到对应通道。
 */
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

/**
 * @brief 一键采集所有已启用探头的基线
 */
void ProbeManager::captureAllBaselines()
{
    for (const auto &probe : m_probes) {
        if (probe->isEnabled()) {
            probe->captureBaseline();
        }
    }
}

/**
 * @brief 一键清除所有探头的基线
 */
void ProbeManager::clearAllBaselines()
{
    for (const auto &probe : m_probes) {
        probe->clearBaseline();
    }
}

/**
 * @brief 批量启用或禁用所有探头
 * @param enabled true 启用，false 禁用
 */
void ProbeManager::setAllEnabled(bool enabled)
{
    for (const auto &probe : m_probes) {
        probe->setEnabled(enabled);
    }
}

/**
 * @brief 强制所有探头重新计算 Vpp
 *
 * 通常在批量导入历史数据后调用，确保各探头计算结果同步。
 */
void ProbeManager::updateAllVpp()
{
    for (const auto &probe : m_probes) {
        probe->updateVpp();
    }
}

/**
 * @brief 强制所有探头重新计算灵敏度
 *
 * 通常在基线批量变更后调用。
 */
void ProbeManager::updateAllSensitivity()
{
    for (const auto &probe : m_probes) {
        probe->updateSensitivity();
    }
}

/**
 * @brief 重建硬件通道到探头索引的映射表
 *
 * 任何修改探头数量或通道分配的操作后都应调用，以保证 dispatchAdcData 正确路由。
 */
void ProbeManager::rebuildChannelIndex()
{
    m_hwChannelToProbeIndex.clear();
    for (int i = 0; i < m_probes.size(); ++i) {
        m_hwChannelToProbeIndex[m_probes[i]->hardwareChannel()] = i;
    }
}
