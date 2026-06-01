/*
 * @Descripttion: 单个探头数据封装
 * @version: 2.0.0
 */
#ifndef PROBE_H
#define PROBE_H

#include "devicemanager.h"
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>
#include <algorithm>

/**
 * @brief 探头幅值和相位数据容器（用于封装单组幅值和相位数据）
 *
 * 采用指针设计，便于在 Probe 内进行多组数据扩展（例如多频通道或乒乓缓冲区）。
 * 实现了 C++ 的 Rule of Five（深拷贝/移动语义），确保指针内存安全。
 * 提供了对乒乓缓冲区极为友好的 swap 操作和智能内存重用设计。
 */
class ProbeData {
public:
  // ── 构造与析构 ─────────────────────────
  ProbeData();
  ~ProbeData();

  // Rule of Five (深拷贝与移动语义)，防止指针悬挂和双重释放
  ProbeData(const ProbeData &other);                ///< 深拷贝构造
  ProbeData &operator=(const ProbeData &other);     ///< 深拷贝赋值
  ProbeData(ProbeData &&other) noexcept;            ///< 移动构造
  ProbeData &operator=(ProbeData &&other) noexcept; ///< 移动赋值

  // ── 内存管理 ───────────────────────────
  /**
   * @brief 统一分配内存大小
   * @note 智能内存分配：若已有内存大小与请求大小一致，则重用已有内存，避免频繁
   * new/delete 造成内存碎片与耗时。
   */
  void AssignedMemoryForProbeData(int ampSize, int phaseSize);

  /**
   * @brief 释放底层 QVector 指针内存并置空
   */
  void release();

  // ── 数据操作辅助函数 ─────────────────────
  /**
   * @brief 清空数据（大小设为 0，但保留已分配的内存容量容量）
   */
  void clear();

  /**
   * @brief 分配内存并以 0 填充
   */
  void fillZero(int ampSize, int phaseSize);

  /**
   * @brief 批量追加幅值和相位数据（用于数据累积）
   */
  void append(const QVector<float> &amp, const QVector<float> &phase);

  /**
   * @brief 追加单个数据点
   */
  void appendPoint(float ampVal, float phaseVal);

  /**
   * @brief 快速交换两个 ProbeData 的底层指针（O(1)
   * 复杂度，常用于乒乓缓冲区的无锁切换）
   */
  void swap(ProbeData &other) noexcept;

  // ── 状态获取 ───────────────────────────
  bool isEmpty() const;
  int ampSize() const;
  int phaseSize() const;

  // ── 原始数据指针 (保持原有名称不改变，完全兼容现有代码) ────────────────
  QVector<float> *m_rawData_amp = nullptr;   ///< 幅值数据指针
  QVector<float> *m_rawData_phase = nullptr; ///< 相位数据指针
};

/**
 * @brief 单个探头的完整数据封装
 *
 * 每个探头独立维护自己的配置、实时数据和计算结果。
 * ProbeManager 负责将 DeviceManager 的原始数据分发到各 Probe。
 */
class Probe : public QObject {
  Q_OBJECT
public:
  explicit Probe(int probeId, int hardwareChannel, QObject *parent = nullptr);
  ~Probe();

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
  ProbeData *realTimeData() { return m_activeData; }
  const ProbeData *realTimeData() const { return m_activeData; }
  QDateTime lastUpdateTime() const { return m_lastUpdateTime; }
  void setLastUpdateTime(const QDateTime &time) { m_lastUpdateTime = time; }

  // ── 3.1 乒乓缓冲区 ─────────────────────
  /** @brief 活跃缓冲区，仅供采集线程写入 */
  ProbeData *activeData() { return m_activeData; }
  /** @brief 保存缓冲区，主线程在收到 dataReady 信号后安全读取 */
  ProbeData *saveData() { return m_saveData; }
  /** @brief 交换活跃/保存缓冲区指针（采集线程在写完一批数据后调用） */
  void swapBuffers();

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
  void captureBaseline();   // 将当前 Vpp 设为基线
  void clearBaseline();     // 清除基线
  void updateVpp();         // 从 rawData 重新计算 Vpp
  void updateSensitivity(); // 基于基线计算灵敏度

signals:
  void enabledChanged(bool enabled); ///< 探头启用/禁用状态变化
  void dataUpdated();                ///< 原始数据已更新
  void vppChanged(float vpp);        ///< Vpp 计算结果变化
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
  int m_id;        ///< 逻辑编号 (0,1,2...)
  int m_hwChannel; ///< 硬件通道号 (1-16)
    QString m_name;  ///< 显示名称

  // === 2. 激励配置 ===
  int m_excitationFreq = 10000; ///< 激励频率 (Hz)
  int m_excitationPhase = 0;    ///< 激励相位 (度)
  int m_excitationAmp = 60;     ///< 激励幅度 (%)

  // === 3. 实时数据 ===
  ProbeData *m_activeData = nullptr; // 活跃缓冲区：消费者线程实时写入
  ProbeData *m_saveData = nullptr;  // 保存缓冲区：数据满了之后，交给保存线程进行落盘
  QDateTime m_lastUpdateTime;       // 最近一次数据更新时间

  // === 4. 计算结果 ===
  float m_vpp = 0.0f;         ///< 当前峰峰值电压
  float m_baselineVpp = 0.0f; ///< 基线 Vpp
  float m_sensitivity = 0.0f; ///< 相对灵敏度 (%)

  // === 5. 状态 ===
  bool m_enabled = true;      ///< 是否启用
  bool m_hasFault = false;    ///< 是否存在数据异常
  bool m_baselineSet = false; ///< 是否已采集基线
};

#endif // PROBE_H
