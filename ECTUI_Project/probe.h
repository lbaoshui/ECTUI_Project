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
    void enabledChanged(bool enabled);
    void dataUpdated();
    void vppChanged(float vpp);
    void sensitivityChanged(float sensitivity);
    void baselineCaptured(float baselineVpp);
    void faultStateChanged(bool hasFault);

private:
    float computeVppInternal() const;

    // === 1. 身份 ===
    int m_id;           // 逻辑编号 (0,1,2...)
    int m_hwChannel;    // 硬件通道号 (1-16)
    QString m_name;     // 显示名称

    // === 2. 激励配置 ===
    int m_excitationFreq = 10000;   // Hz
    int m_excitationPhase = 0;      // 度
    int m_excitationAmp = 60;       // %

    // === 3. 实时数据 ===
    QVector<quint16> m_rawData;
    QDateTime m_lastUpdateTime;

    // === 4. 计算结果 ===
    float m_vpp = 0.0f;
    float m_baselineVpp = 0.0f;
    float m_sensitivity = 0.0f;

    // === 5. 状态 ===
    bool m_enabled = true;
    bool m_hasFault = false;
    bool m_baselineSet = false;
};

#endif // PROBE_H
