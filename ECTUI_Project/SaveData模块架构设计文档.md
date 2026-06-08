# SaveData 模块设计文档

> **版本**: 1.0.0 | **日期**: 2026-05-31

---

## 1. 概述

SaveData 模块负责将探头采集的原始数据**异步写入磁盘**。它被设计为信号驱动的、以独立工作线程执行 I/O 的模块，与采集线程（DataAcquisitionThread）解耦，确保磁盘写入不会阻塞数据采集或 UI 刷新。

### 核心需求

| 需求 | 实现方式 |
|------|---------|
| 信号触发保存 | DataAcquisitionThread 发出 `saveDataReady` 信号驱动 |
| 阈值批量落盘 | active 缓冲区攒够 100000 点才 swap 并触发保存 |
| 同次采集同一文件 | `QFile::Append` 模式持续追加 |
| 停止后换新文件 | 采集停止关文件，下次采集生成新时间戳文件名 |
| 磁盘 I/O 不阻塞 UI | SaveWorker 在独立 `QThread` 中执行 |

---

## 2. 架构

```
┌──────────────────────────────────────────────────────────────┐
│                       MainWindow (主线程)                      │
│                                                              │
│  ┌─────────────────┐     ┌──────────────────────────────┐   │
│  │ DataAcquisition │     │        SaveManager           │   │
│  │    Thread       │     │                              │   │
│  │  (采集线程)      │     │  onAcquisitionStarted()      │   │
│  │                 │     │  onAcquisitionStopped()      │   │
│  │  run() 循环:     │     │  onSaveDataReady(idx)        │   │
│  │   写入 active   │     │                              │   │
│  │   达到阈值? ────────> saveDataReady(idx) ──>  拷贝     │   │
│  │   swapBuffers()│     │   saveData → invokeMethod ──┐ │   │
│  │   emit signal  │     │                            │ │   │
│  └─────────────────┘     └────────────────────────────┼─┘   │
│                                                       │     │
└───────────────────────────────────────────────────────┼─────┘
                                                        │
                                          QueuedConnection
                                                        │
┌───────────────────────────────────────────────────────┼─────┐
│                 SaveWorker (工作线程)                   │     │
│                                                       ▼     │
│              openFiles()   → 打开 CSV 文件 (Append)          │
│              appendData()  → 逐行写入 amp,phase               │
│              flushAndClose() → 刷盘 + 关闭                   │
└──────────────────────────────────────────────────────────────┘
```

### 线程模型

```
主线程 (Main)         采集线程 (Acquisition)      工作线程 (I/O)
    │                       │                        │
    │  SaveManager          │  DataAcquisitionThread │  SaveWorker
    │  ├─ 文件生命周期       │  ├─ 读设备数据          │  ├─ openFiles()
    │  ├─ 数据转发          │  ├─ 写 active 缓冲区     │  ├─ appendData()
    │  └─ 信号中转          │  ├─ 阈值判断 → swap     │  └─ closeAllFiles()
    │                       │  └─ emit saveDataReady  │
    │                       │                        │
    ├───────────────────────┼────────────────────────┤
    │    信号槽 (auto → QueuedConnection)             │
    │    QMetaObject::invokeMethod (QueuedConnection) │
```

---

## 3. 类设计

### 3.1 SaveWorker —— 工作线程中的 I/O 执行者

```
SaveWorker : QObject
├── 生命周期: 由 SaveManager 创建，moveToThread 到独立 QThread
├── 职责: 文件打开 / CSV 追加写入 / 关闭
│
├── openFiles(folder, {probeIndex → fileName})
│   └── 以 QFile::Append 模式打开每个探头的 CSV 文件
│
├── appendData(probeIndex, amp, phase)
│   └── 逐行写入 "amp,phase\n"，每批数据写入后立即 flush
│
├── closeAllFiles()
│   └── 关闭所有文件句柄，释放资源
│
└── flushAndClose()
    └── 等价于 closeAllFiles()，为采集停止时的入口
```

**关键实现细节：**

- 每个探头维护独立的 `QFile` + `QTextStream` 句柄，通过 `QHash<int, FileHandle>` 管理
- 写入后立即 `flush()`，防止进程异常退出时丢失缓冲区数据
- 析构时确保所有文件句柄被关闭，避免资源泄漏

### 3.2 SaveManager —— 主线程中的控制器

```
SaveManager : QObject
├── 生命周期: 在 MainWindow 中创建，跟随主窗口生命周期
├── 职责: 文件生命周期管理 / 数据转发 / 信号中转
│
├── onAcquisitionStarted()
│   ├── 为每个已启用的 Probe 生成带时间戳的 CSV 文件名
│   ├── 通知 SaveWorker 打开文件
│   └── 设置 m_isAcquiring = true
│
├── onAcquisitionStopped()
│   ├── 通知 SaveWorker 刷盘并关闭文件
│   └── 设置 m_isAcquiring = false
│
├── onSaveDataReady(probeIndex)
│   ├── 从 probe->saveData() 深拷贝 amp / phase 数据
│   └── 通过 QMetaObject::invokeMethod 发送到 SaveWorker
│
└── generateFileName(probeIndex)
    └── 格式: "probe_{N}_{yyyyMMdd_HHmmss}.csv"
```

---

## 4. 数据流详解

### 4.1 单次保存的完整链路

```
 1. 采集线程从 DeviceManager 取数据包
 2. active->append(ampMv, phaseDeg)   // 累积数据
 3. if (active->ampSize() >= 100000)  // 阈值检查
 4.     probe->swapBuffers()          // 交换缓冲区
         ├── std::swap(active, save)  // O(1) 指针交换
         └── active->clear()          // 新 active 清零
 5.     emit saveDataReady(probeIndex) // 发出保存信号
 6. SaveManager::onSaveDataReady()
     ├── 从 probe->saveData() 深拷贝 QVector<float>
     └── QMetaObject::invokeMethod("appendData", amp, phase)
 7. SaveWorker::appendData()
     ├── 定位 probeIndex 对应的 QTextStream
     └── 逐行写入 "amp,phase\n" → flush()
```

### 4.2 文件生命周期

```
采集开始
  │
  ├─ onAcquisitionStarted()
  │   └─ 生成文件名: probe_0_20260531_143021.csv
  │   └─ SaveWorker::openFiles() → QFile::Append
  │
  ├─ 持续采集中...
  │   └─ 每 100000 点触发一次 appendData()，追加到同一个文件
  │
  ├─ 持续采集中...
  │   └─ 又 100000 点，继续追加...
  │
采集停止
  │
  ├─ onAcquisitionStopped()
  │   └─ SaveWorker::flushAndClose() → 关闭文件
  │
下次采集开始
  │
  └─ onAcquisitionStarted()
      └─ 生成新文件名: probe_0_20260531_144530.csv  ← 新文件!
```

### 4.3 CSV 文件格式

```csv
0.123,45.678
0.234,56.789
0.345,67.890
...共 100000 行...
(amp,phase)
```

每行一对 `幅值mV, 相位deg`，浮点数直接输出，无表头。

---

## 5. 线程安全分析

```
共享资源                          写入者              读取者           保护机制
────────────────────────────────────────────────────────────────────────
ProbeData* m_activeData    采集线程              -              单写者
ProbeData* m_saveData      采集线程 (swap后)     主线程 (拷贝)   时序隔离：swap 后采集线程不再碰 saveData
SaveWorker::m_files        工作线程              工作线程        单线程访问（moveToThread 独占）
```

- **active ↔ saveData 交换**：`std::swap` 是原子指针交换，采集线程执行
- **saveData 读取**：SaveManager 在主线程读取，此时采集线程已 emit 信号并不再持有 saveData 引用
- **文件 I/O**：全部在 SaveWorker 的专属线程内执行，无竞争

---

## 6. 文件结构

```
ECTUI_Project/
├── savemanager.h          # SaveWorker + SaveManager 声明
├── savemanager.cpp        # 完整实现
├── dataacquisitionthread.h/cpp  # 采集线程（含阈值判断 + saveDataReady 信号）
├── probe.h/cpp            # 探头对象（含 swapBuffers 清零逻辑）
├── mainwindow.h/cpp       # UI 层（SaveManager 实例化 + 信号连接）
└── ECTUI_Project.pro      # 构建文件（已加入 savemanager）
```

---

## 7. 接入方式

在 MainWindow 中初始化 DataAcquisitionThread 后，连接以下三个信号即可完成集成：

```cpp
// 采集线程的保存信号 → SaveManager 处理
connect(m_acquisitionThread, &DataAcquisitionThread::saveDataReady,
        m_saveManager, &SaveManager::onSaveDataReady);

// 采集开始 → 创建新文件
connect(startBtn, &QPushButton::clicked,
        m_saveManager, &SaveManager::onAcquisitionStarted);

// 采集停止 → 关闭文件
connect(stopBtn, &QPushButton::clicked,
        m_saveManager, &SaveManager::onAcquisitionStopped);
```

---

## 8. 可配置参数

| 参数 | 位置 | 默认值 | 说明 |
|------|------|--------|------|
| `SAVE_THRESHOLD` | `dataacquisitionthread.h:65` | 100000 | 触发 swap + 保存的数据点数 |
| `m_dataFolder` | `SaveManager::setDataFolder()` | `./data/` | CSV 文件输出目录 |
| 文件名模板 | `SaveManager::generateFileName()` | `probe_{N}_{yyyyMMdd_HHmmss}.csv` | 可通过覆写该函数自定义 |
