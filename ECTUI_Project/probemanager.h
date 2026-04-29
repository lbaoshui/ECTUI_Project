/*
 * @Descripttion: 探头管理器
 * @version: 2.0.0
 */
#ifndef PROBEMANAGER_H
#define PROBEMANAGER_H

#include <QObject>
#include <QVector>
#include <QMap>
#include <QSharedPointer>
#include "probe.h"
#include "devicemanager.h"

/**
 * @brief 探头管理器
 *
 * 负责：
 * 1. 动态创建/销毁 Probe 对象
 * 2. 将 DeviceManager 的 ADC 数据按通道分发给各 Probe
 * 3. 生成 DA 配置帧（按硬件通道排序）
 * 4. 提供批量操作接口（一键采基线、一键清除等）
 */
class ProbeManager : public QObject
{
    Q_OBJECT
public:
    explicit ProbeManager(QObject *parent = nullptr);

    // ── 探头数量管理 ────────────────────────
    int probeCount() const { return m_probes.size(); }
    void setProbeCount(int count);

    Probe *probeAt(int index) const;
    Probe *probeByHardwareChannel(int hwChannel) const;
    QVector<Probe *> allProbes() const;

    // ── 通道映射 ────────────────────────────
    /**
     * @brief 设置探头使用的硬件通道
     * @param probeIndex 探头逻辑索引
     * @param hwChannel 硬件通道号 (1-16)
     */
    void setProbeHardwareChannel(int probeIndex, int hwChannel);

    /**
     * @brief 批量设置通道映射
     * @param mapping 索引 i = 探头 i 对应的硬件通道号
     */
    void setChannelMapping(const QVector<int> &mapping);
    QVector<int> channelMapping() const;

    // ── 数据分发 ────────────────────────────
    /**
     * @brief 从 DeviceManager 接收原始 ADC 数据，分发给各 Probe
     */
    void dispatchAdcData(const QVector<AdcChannelData> &adcData);

    // ── DA 配置生成 ─────────────────────────
    /**
     * @brief 生成 DeviceManager::sendDaConfig() 需要的 16 通道配置
     *
     * 未使用的通道幅度设为 0，已启用的 Probe 按其实际配置填充。
     */
    QVector<DaChannelConfig> buildDaConfig() const;

    // ── 批量操作 ────────────────────────────
    void captureAllBaselines();
    void clearAllBaselines();
    void setAllEnabled(bool enabled);
    void updateAllVpp();
    void updateAllSensitivity();

signals:
    void probeCountChanged(int count);
    void probeAdded(Probe *probe);
    void probeRemoved(int index);

private:
    void rebuildChannelIndex();

    QVector<QSharedPointer<Probe>> m_probes;
    QMap<int, int> m_hwChannelToProbeIndex;  // 硬件通道 -> Probe 索引
};

#endif // PROBEMANAGER_H
