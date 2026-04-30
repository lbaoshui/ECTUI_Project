/*
 * @Descripttion: 单个探头数据封装
 * @version: 2.0.0
 */
#ifndef PROBE_H
#define PROBE_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QDateTime>
#include "devicemanager.h"

/**
 * @brief 单个探头的完整数据封装
 *
 * 每个探头独立维护自己的配置、实时数据和计算结果。
 * ProbeManager 负责将 DeviceManager 的原始数据分发到各 Probe。
 */
class Probe : public QObject
{
    Q_OBJECT
public:
    explicit Probe(int probeId, int hardwareChannel, QObject *parent = nullptr);

    // ── 1. 身份信息 ─────────────────────────
    int id() const { return m_id; }
    int hardwareChannel() const { return m_hwChannel; }
    void setHardwareChannel(int channel);
    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    // ── 2. DA 激励配置 ──────────────────────
    int excitationFreq() const { return m_excitationFreq; }
    int excitationPhase() const { return m_excitationPhase; }
    int excitationAmp() const { return m_excitationAmp; }

    void setExcitation(int freq, int phase, int amp);

    /**
     * @brief 生成 DaChannelConfig，供 DeviceManager 发送使用
     */
    DaChannelConfig toDaChannelConfig() const;

    // ── 3. 实时采集数据 ─────────────────────
    QVector<quint16> rawData() const;
    void setRawData(const QVector<quint16> &data);
    QDateTime lastUpdateTime() const { return m_lastUpdateTime; }

    // ── 4. 计算结果 ─────────────────────────
    float vpp() const { return m_vpp; }
    float baselineVpp() const { return m_baselineVpp; }
    float sensitivity() const { return m_sensitivity; }

    // ── 5. 状态 ─────────────────────────────
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool hasFault() const { return m_hasFault; }
    bool isBaselineSet() const { return m_baselineSet; }

    // ── 6. 业务操作 ─────────────────────────
    void captureBaseline();     // 将当前 Vpp 设为基线
    void clearBaseline();       // 清除基线
    void updateVpp();           // 从 rawData 重新计算 Vpp
    void updateSensitivity();   // 基于基线计算灵敏度

signals:
    void enabledChanged(bool enabled);          ///< 探头启用/禁用状态变化
    void dataUpdated();                         ///< 原始数据已更新
    void vppChanged(float vpp);                 ///< Vpp 计算结果变化
    void sensitivityChanged(float sensitivity); ///< 灵敏度变化
    void baselineCaptured(float baselineVpp);   ///< 基线采集完成
    void faultStateChanged(bool hasFault);      ///< 故障状态变化

private:
    /**
     * @brief 内部 Vpp 计算（去极值平均法）
     * @return 峰峰值电压
     */
    float computeVppInternal() const;

    // === 1. 身份 ===
    int m_id;           ///< 逻辑编号 (0,1,2...)
    int m_hwChannel;    ///< 硬件通道号 (1-16)
    QString m_name;     ///< 显示名称

    // === 2. 激励配置 ===
    int m_excitationFreq = 10000;   ///< 激励频率 (Hz)
    int m_excitationPhase = 0;      ///< 激励相位 (度)
    int m_excitationAmp = 60;       ///< 激励幅度 (%)

    // === 3. 实时数据 ===
    QVector<quint16> m_rawData;     ///< 原始 ADC 采样值
    QDateTime m_lastUpdateTime;     ///< 最近一次数据更新时间

    // === 4. 计算结果 ===
    float m_vpp = 0.0f;             ///< 当前峰峰值电压
    float m_baselineVpp = 0.0f;     ///< 基线 Vpp
    float m_sensitivity = 0.0f;     ///< 相对灵敏度 (%)

    // === 5. 状态 ===
    bool m_enabled = true;          ///< 是否启用
    bool m_hasFault = false;        ///< 是否存在数据异常
    bool m_baselineSet = false;     ///< 是否已采集基线
};

#endif // PROBE_H
