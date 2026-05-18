# Probe 探头类设计文档

## 1. 概述

`Probe` 是对**单个物理探头**的完整数据封装。每个实例独立维护自己的配置、实时数据和计算结果，通过 Qt 信号机制向上层（UI/算法模块）通知状态变化。

**核心原则**：一个 Probe = 一个物理探头 = 一个独立的"数据+计算+状态"单元。

---

## 2. 设计思路

### 2.1 为什么要把探头独立成一个类？

在旧版代码中，探头的数据、配置和计算散落在 DeviceManager 或 MainWindow 中，导致：

- 新增/删除探头需要改动多处代码
- 每个探头的状态（启用、故障、基线）混在一起，难以追踪
- 业务逻辑（采基线、算灵敏度）与 UI 代码耦合

**独立为 Probe 类后的收益**：

| 问题 | 解决方式 |
|------|----------|
| 探头数量动态变化 | ProbeManager 统一管理生命周期 |
| 多探头状态追踪 | 每个 Probe 自带完整状态，信号机制解耦 |
| 计算逻辑分散 | Vpp/灵敏度算法封装在 Probe 内部 |
| UI 更新 | 信号驱动，无需手动轮询 |

### 2.2 数据结构分层

每个 Probe 将数据分为五个层级，各司其职：

```
┌─────────────────────────────────────────┐
│  1. 身份信息                             │
│     id / hardwareChannel / name         │
├─────────────────────────────────────────┤
│  2. DA 激励配置                          │
│     频率 / 相位 / 幅度                   │
├─────────────────────────────────────────┤
│  3. 实时采集数据                          │
│     rawData (512 点 ADC) / 更新时间      │
├─────────────────────────────────────────┤
│  4. 计算结果                             │
│     Vpp / 基线Vpp / 灵敏度               │
├─────────────────────────────────────────┤
│  5. 状态标志                             │
│     启用 / 故障 / 基线已设置              │
└─────────────────────────────────────────┘
```

---

## 3. 核心机制

### 3.1 Vpp 计算：去极值平均法

```cpp
// 对 512 点排序 → 取最小 5 个和最大 5 个做平均 → 差值 × 缩放系数
float computeVppInternal() const
```

**为什么用去极值平均而不是直接取最大最小值？**

电子测量中，原始 ADC 数据不可避免地混入尖峰噪声。若用 max - min 计算峰峰值，单个毛刺就会导致 Vpp 剧烈跳变。取 top 5 和 bottom 5 的平均值有效抑制了偶发干扰，同时保留了信号的真实幅度特征。

**缩放系数 `1.03 × 0.122` 的含义**：
- `1.03`：硬件前端运放增益补偿
- `0.122`：ADC 参考电压与分辨率换算系数（mV/bit）

### 3.2 灵敏度计算：带死区的相对变化

```cpp
sensitivity = ((Vpp - baseline) / baseline) × 100%
// 变化幅度 ≤ ±2% 时，灵敏度归零（死区）
```

**为什么需要 ±2% 死区？**

被测件未接触探头时，Vpp 仍会因温漂、电源纹波等因素产生微小波动。如果没有死区，界面会持续显示零点几的灵敏度数值，干扰操作人员判断。2% 的死区消除了这种"背景噪声"，只有真正接触被测件产生的显著变化才会触发灵敏度显示。

### 3.3 故障检测

```cpp
void setRawData(const QVector<quint16> &data)
{
    if (data.size() != DeviceManager::ADC_SAMPLES_PER_CH) {
        // 标记故障，发信号
        m_hasFault = true;
        emit faultStateChanged(true);
        return;
    }
    // 数据正常则清除故障
}
```

数据长度校验是硬件通信中的防御性设计——下位机传输可能出现丢包或协议解析异常，导致单通道数据点数不完整。Probe 在数据入口就做校验，避免后续计算因数组越界崩溃。

### 3.4 信号驱动的 UI 更新

```
setRawData()  →  emit dataUpdated()
updateVpp()   →  emit vppChanged(vpp)
captureBaseline() → emit baselineCaptured(baseline)
setEnabled()  →  emit enabledChanged(bool)
faultStateChanged → emit faultStateChanged(bool)
```

每个状态变化都有对应信号，UI 层只需连接信号即可响应，无需轮询或手动刷新。这是 Qt 信号槽机制的标准用法——**数据层主动通知，视图层被动更新**。

---

## 4. 类图

```
QObject
  └── Probe
        ├── 属性: id, hwChannel, name
        ├── 激励: freq, phase, amp
        ├── 数据: rawData[512], lastUpdateTime
        ├── 结果: vpp, baselineVpp, sensitivity
        ├── 状态: enabled, hasFault, baselineSet
        └── 信号: dataUpdated, vppChanged, sensitivityChanged,
                   baselineCaptured, faultStateChanged, enabledChanged
```

---

## 5. 关键接口速查

| 接口 | 职责 | 调用方 |
|------|------|--------|
| `setRawData(data)` | 写入原始 ADC 数据，触发故障检测 | ProbeManager |
| `updateVpp()` | 从 rawData 计算 Vpp，变化时发信号 | ProbeManager |
| `updateSensitivity()` | 基于 Vpp 和基线计算灵敏度 | ProbeManager |
| `captureBaseline()` | 将当前 Vpp 保存为基线 | ProbeManager / UI |
| `clearBaseline()` | 清除基线，复位灵敏度为 0 | ProbeManager / UI |
| `setExcitation(freq, phase, amp)` | 设置 DA 激励参数 | UI |
| `toDaChannelConfig()` | 导出为 DeviceManager 需要的配置结构体 | ProbeManager |
| `setEnabled(bool)` | 启用/禁用探头，禁用时不参与 DA 帧生成 | UI |
