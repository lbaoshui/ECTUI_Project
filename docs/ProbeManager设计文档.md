# ProbeManager 探头管理器设计文档

## 1. 概述

`ProbeManager` 是探头层的**中央协调器**，负责探头的生命周期管理、数据路由分发、DA 配置帧生成和批量操作。它在 DeviceManager（底层通信）和 Probe（单个探头）之间充当**中介者（Mediator）**角色。

---

## 2. 设计思路

### 2.1 为什么需要 ProbeManager？

如果让 UI 层直接管理多个 Probe，会产生以下问题：

- **数据分发复杂**：DeviceManager 一次性吐出 16 通道数据，UI 需要知道每个探头对应哪个硬件通道
- **批量操作冗余**：一键采基线、一键清除等操作需要遍历所有探头，代码分散在各处
- **生命周期混乱**：探头增删时，UI 和数据层都要同步更新

ProbeManager 将这些横切关注点集中到一层，对上暴露简洁接口，对下封装复杂逻辑。

### 2.2 分层架构

```
┌──────────────────────────────────┐
│            UI 层                  │
│   (QML / MainWindow)             │
├──────────────────────────────────┤
│       ProbeManager               │  ← 本层
│   数据路由 / 批量操作 / 生命周期   │
├──────────────────────────────────┤
│    Probe × N                     │
│   单探头数据 + 计算               │
├──────────────────────────────────┤
│       DeviceManager              │
│   TCP 通信 / 协议解析             │
└──────────────────────────────────┘
```

---

## 3. 核心机制

### 3.1 探头生命周期管理

```cpp
void ProbeManager::setProbeCount(int count)
```

**增加探头**：从当前数量递增到目标数量，按顺序分配硬件通道（探头 i → 通道 i+1），创建时自动命名"探头-1"、"探头-2"……

**减少探头**：从尾部开始移除，先发 `probeRemoved` 信号通知 UI，再销毁对象。

**设计考量**——为什么用 QSharedPointer 而非裸指针？

```cpp
QVector<QSharedPointer<Probe>> m_probes;
```

Probe 继承自 QObject，生命周期由 ProbeManager 唯一拥有。使用 QSharedPointer 而非 QVector<Probe> 的原因：
1. QObject 不可拷贝，无法直接放入 QVector 值容器
2. QSharedPointer 保证异常安全，即使中途构造失败也不会泄漏
3. 对外暴露裸指针（`probeAt()`）时，调用方不需要关心所有权

### 3.2 硬件通道到探头的快速映射

```cpp
QMap<int, int> m_hwChannelToProbeIndex;  // 硬件通道号 → probe 索引

void rebuildChannelIndex()
{
    m_hwChannelToProbeIndex.clear();
    for (int i = 0; i < m_probes.size(); ++i) {
        m_hwChannelToProbeIndex[m_probes[i]->hardwareChannel()] = i;
    }
}
```

**为什么需要这个映射表？**

DeviceManager 发来的 ADC 数据按硬件通道号（1-16）标记，而 UI 按探头逻辑索引（0, 1, 2...）访问。如果不用映射表，每次数据分发都要遍历所有探头做线性查找——O(n)。映射表将查找降为 O(log n)（QMap 红黑树），在 50Hz+ 的数据刷新率下，这个优化是必要的。

**何时重建索引？** 任何可能改变通道映射的操作后都调用 `rebuildChannelIndex()`：
- `setProbeCount()` — 增删探头
- `setProbeHardwareChannel()` — 单探头换通道
- `setChannelMapping()` — 批量换通道

### 3.3 数据分发流程

```
DeviceManager::adcDataReady(QVector<AdcChannelData>)
        │
        ▼
ProbeManager::dispatchAdcData(adcData)
        │
        ├── for each AdcChannelData:
        │       probe = probeByHardwareChannel(chData.ch)  // O(log n) 查找
        │       if probe:
        │           probe->setRawData(chData.data)          // 写入 + 故障检测
        │           probe->updateVpp()                      // 计算 Vpp
        │           probe->updateSensitivity()              // 计算灵敏度
        │
        ▼
    各 Probe 发射独立信号 → UI 更新对应控件
```

**关键点**：每个 Probe 的数据更新是完全独立的。通道 1 的数据异常不会阻塞通道 2 的计算，各 Probe 的信号分别发出，UI 可以做到局部刷新而非整页重绘。

### 3.4 DA 配置帧生成

```cpp
QVector<DaChannelConfig> ProbeManager::buildDaConfig() const
{
    // 1. 创建 16 通道全关闭的默认配置
    QVector<DaChannelConfig> result(DeviceManager::DA_CHANNELS);
    for (int i = 0; i < DA_CHANNELS; ++i)
        result[i] = {i+1, 1, 10000, 0, 0};  // 幅度=0 → 通道关闭

    // 2. 已启用的探头覆盖对应通道
    for (const auto &probe : m_probes) {
        if (probe->isEnabled())
            result[probe->hardwareChannel() - 1] = probe->toDaChannelConfig();
    }
    return result;
}
```

**设计考量**：

- 先填默认值再覆盖，保证输出始终是固定的 16 通道，DeviceManager 协议要求严格定长
- 禁用的探头不参与覆盖，对应通道保持幅度为 0（硬件不输出激励）
- 多个探头可以映射到不同硬件通道，但不能共享同一通道（后者覆盖前者）

### 3.5 批量操作

```cpp
void captureAllBaselines();   // 一键采基线（仅已启用的探头）
void clearAllBaselines();     // 一键清除基线（所有探头）
void setAllEnabled(bool);     // 批量启用/禁用
void updateAllVpp();          // 批量重算 Vpp
void updateAllSensitivity();  // 批量重算灵敏度
```

批量操作的设计原则：**只做遍历，不做决策**。每个方法的逻辑都极简——遍历 m_probes，调用对应 Probe 的方法。真正的业务决策（如 captureBaselines 只对已启用探头生效）下沉到 Probe 自身或由调用方控制。

---

## 4. 类关系图

```
DeviceManager ──(adcDataReady 信号)──► ProbeManager
                                          │
                          ┌───────────────┼───────────────┐
                          │               │               │
                        Probe[0]       Probe[1]    ...  Probe[N-1]
                          │               │               │
                          └─── 各 Probe 信号 ──► UI 层 ───┘

ProbeManager
  ├── m_probes: QVector<QSharedPointer<Probe>>   (探头列表，内部所有权)
  ├── m_hwChannelToProbeIndex: QMap<int,int>     (通道→索引快速查找)
  │
  ├── 探头管理: setProbeCount / probeAt / allProbes
  ├── 通道映射: setProbeHardwareChannel / setChannelMapping
  ├── 数据分发: dispatchAdcData
  ├── 配置生成: buildDaConfig
  ├── 批量操作: captureAllBaselines / clearAllBaselines / setAllEnabled
  └── 信号: probeCountChanged / probeAdded / probeRemoved
```

---

## 5. 数据流全景

```
┌──────────────┐    TCP 原始字节流    ┌─────────────────┐
│  下位机硬件   │ ─────────────────►  │  DeviceManager   │
│  (FPGA+ADC)  │                     │  协议解析/粘包处理 │
└──────────────┘                     └────────┬────────┘
                                              │ adcDataReady(QVector<AdcChannelData>)
                                              ▼
                                     ┌─────────────────┐
                                     │  ProbeManager    │
                                     │  按通道分发数据    │
                                     └────────┬────────┘
                                              │ probe->setRawData(data)
                              ┌───────────────┼───────────────┐
                              ▼               ▼               ▼
                         ┌────────┐     ┌────────┐     ┌────────┐
                         │Probe[0]│     │Probe[1]│ ... │Probe[N]│
                         │ Vpp计算 │     │ Vpp计算 │     │ Vpp计算 │
                         │灵敏度计算│     │灵敏度计算│     │灵敏度计算│
                         └───┬────┘     └───┬────┘     └───┬────┘
                             │              │              │
                             ▼              ▼              ▼
                         vppChanged   vppChanged    vppChanged
                         sensitivity  sensitivity   sensitivity
                         Changed      Changed       Changed
                             │              │              │
                             ▼              ▼              ▼
                        ┌──────────────────────────────────┐
                        │            UI 层                  │
                        │   波形图 / 数值标签 / 状态灯       │
                        └──────────────────────────────────┘
```

---

## 6. 关键接口速查

| 接口 | 职责 | 调用方 |
|------|------|--------|
| `setProbeCount(n)` | 动态增删探头 | UI |
| `probeAt(i)` | 按逻辑索引获取探头 | UI / QML |
| `probeByHardwareChannel(ch)` | 按硬件通道查找探头 | 内部 / UI |
| `setProbeHardwareChannel(idx, ch)` | 单探头通道切换 | UI |
| `setChannelMapping(mapping)` | 批量通道映射 | UI / 配置加载 |
| `dispatchAdcData(adcData)` | 数据分发入口 | DeviceManager 信号槽 |
| `buildDaConfig()` | 生成 DA 配置帧 | DeviceManager 调用前 |
| `captureAllBaselines()` | 一键采基线 | UI |
| `clearAllBaselines()` | 一键清基线 | UI |
