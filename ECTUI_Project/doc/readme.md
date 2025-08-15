# ECT - 涡流检测系统

## 项目概述

ECT（Eddy Current Testing）是一个基于Qt的涡流检测系统，用于实时采集、处理、显示和保存多通道涡流检测数据。系统采用多线程架构，确保在高速数据采集和处理的同时保持界面的响应性。

## 主要功能

- 实时数据采集与显示
- 多通道数据处理
- 参数控制（驱动频率、采集频率、增益等）
- 数据存储与回放
- 滤波器应用
- 通道配置管理

## 系统架构

### 整体架构

系统采用多线程架构，将数据采集、处理和存储分离到不同的线程中：

1. **主线程（UI线程）**：负责界面更新和用户交互
2. **数据采集线程**：负责从网络接口获取数据
3. **数据处理线程**：处理原始数据，进行信号处理和分析
4. **数据存储线程**：负责数据的保存和文件管理

### 数据流向

```
网络接口 → 数据采集线程 → 通道管理器 → 数据处理线程 → UI显示/数据存储线程
```

### 关键组件

#### 1. 通道管理器（ChannelManager）

通道管理器是系统的核心组件，负责：

- 管理多个数据采集通道
- 分发数据包到相应的通道
- 维护每个通道的配置和状态
- 提供内存池和无锁队列，确保高效的数据传输
- 每个组件单独成一个模块，并用单独的h/cpp文件编写，模块逻辑清晰，方便阅读；

#### 2. 数据结构设计

系统采用清晰的数据结构设计，将探头参数与实际数据分离：

##### 探头参数配置（ProbeConfig）

```cpp
// 探头参数配置结构体
struct ProbeConfig {
    double drivingFreq;     // 驱动频率
    double acquisitionFreq; // 采集频率
    double preGain;         // 前置增益
    double postGain;        // 后置增益
    double rotationAngle;   // 旋转角度
    double refCurrent;      // 参考电流
    double driveCurrent;    // 驱动电流
};
```

##### 数据包结构（DataPacket）

```cpp
// 采集数据包结构体 - 仅包含实际数据
struct DataPacket {
    QByteArray rawData;              // 原始数据
    QVector<double> processedData; // 处理后的数据
    double realValue;                // 实部值
    double imagValue;                // 虚部值
    int channelId;                   // 通道ID
    bool isValid;                    // 数据是否有效
  
    // 指向相关探头配置的指针（不拥有该对象）
    const ProbeConfig* probeConfig;
};
```

这种设计将探头的参数配置与实际采集的数据分离，具有以下优点：

- 明确区分了设备参数和测量数据
- 减少了数据包的大小，提高了传输效率
- 允许多个数据包共享相同的探头配置
- 使参数管理更加集中和一致

#### 3. 无锁队列（LockFreeQueue）

为了解决高速数据采集中的互斥锁争用问题，系统使用无锁队列进行线程间通信：

- 基于原子操作实现，避免了传统互斥锁的开销
- 每个通道有独立的队列，减少争用
- 固定大小的环形缓冲区，避免动态内存分配

#### 4. 内存池（MemoryPool）

为了避免频繁的内存分配和释放，系统使用内存池管理数据包对象：

- 预分配固定数量的数据包对象
- 快速分配和回收，减少内存碎片
- 使用信号量控制资源访问

#### 5. 数据采集工作线程（DataAcquisitionWorker）

负责从网络接口获取数据：

- 支持多通道同时采集
- 使用定时器控制采集频率
- 解析接收到的数据并创建数据包

#### 6. 数据处理工作线程（DataProcessWorker）

负责处理原始数据：

- 支持多种滤波器（低通、高通、带通、陷波）
- 并行处理多个通道的数据
- 处理结果发送到UI显示和数据存储

#### 7. 数据存储工作线程（DataStorageWorker）

负责数据的保存：

- 支持自动保存和手动保存
- 每个通道可以独立保存
- 批量处理数据，提高I/O效率

## 优化设计

### 1. 参数与数据分离

将探头参数与采集数据分离，使系统设计更加清晰，同时提高了数据处理效率：

```cpp
// 在数据采集线程中处理接收到的数据
void DataAcquisitionWorker::processReceivedData()
{
    // ...
  
    // 创建一个新的ProbeConfig对象并设置参数
    static ProbeConfig currentProbeConfig;
    currentProbeConfig.drivingFreq = parts[1].toDouble();
    currentProbeConfig.acquisitionFreq = parts[2].toDouble();
    currentProbeConfig.preGain = parts[3].toDouble();
    currentProbeConfig.postGain = parts[4].toDouble();
    currentProbeConfig.rotationAngle = parts[5].toDouble();
  
    // 设置数据包属性
    packet->rawData = packetData;
    packet->timestamp = QDateTime::currentDateTime();
    packet->channelId = channelId;
    packet->realValue = parts[6].toDouble();
    packet->imagValue = parts[7].toDouble();
    packet->probeConfig = &currentProbeConfig; // 指向当前探头配置
  
    // ...
}
```

### 2. 无锁数据结构

传统的互斥锁在高频数据采集场景下可能成为性能瓶颈。本系统使用无锁队列替代传统的互斥锁保护的队列，大幅提高了数据吞吐能力：

```cpp
template<typename T, size_t Size>
class LockFreeQueue {
public:
    bool push(const T& item) {
        size_t currentTail = tail.load(std::memory_order_relaxed);
        size_t nextTail = (currentTail + 1) % Size;
  
        if (nextTail == head.load(std::memory_order_acquire))
            return false;  // 队列已满
  
        buffer[currentTail] = item;
        tail.store(nextTail, std::memory_order_release);
        return true;
    }
  
    bool pop(T& item) {
        size_t currentHead = head.load(std::memory_order_relaxed);
  
        if (currentHead == tail.load(std::memory_order_acquire))
            return false;  // 队列为空
  
        item = buffer[currentHead];
        head.store((currentHead + 1) % Size, std::memory_order_release);
        return true;
    }
  
    // ...其他方法...
  
private:
    std::array<T, Size> buffer;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
};
```

### 3. 通道分离

为了减少不同通道之间的数据争用，系统为每个通道创建独立的数据队列：

```cpp
QMap<int, LockFreeQueue<DataPacket*, 2048>*> channelQueues;
```

这种设计确保了不同通道的数据处理不会相互干扰，大大提高了系统的并发性能。

### 4. 内存池

频繁的内存分配和释放会导致性能下降和内存碎片。系统使用内存池预分配数据包对象，避免了这些问题：

```cpp
DataPacket* MemoryPool::allocate()
{
    // 尝试从池中获取一个对象
    if (available.tryAcquire(1)) {
        QMutexLocker locker(&mutex);
  
        if (!pool.empty()) {
            DataPacket* packet = pool.back();
            pool.pop_back();
            return packet;
        }
    }
  
    // 如果池为空，创建一个新对象
    return new DataPacket();
}
```

### 5. 双缓冲显示

为了避免UI更新和数据处理之间的争用，系统使用双缓冲技术进行显示更新：

```cpp
void ECT::updateChannelPlots()
{
    QMutexLocker locker(&plotMutex);
  
    if (!plotSwapReady)
        return;
  
    // 更新所有通道的图表
    for (auto it = channelGraphs.begin(); it != channelGraphs.end(); ++it) {
        int channelId = it.key();
        QCPGraph* graph = it.value();
  
        std::vector<double> xData, yData;
        channelManager->getChannelData(channelId, xData, yData);
  
        QVector<double> qxData(xData.begin(), xData.end());
        QVector<double> qyData(yData.begin(), yData.end());
  
        graph->setData(qxData, qyData);
    }
  
    // 使用队列重绘减少UI线程负担
    ui->plotLeft->replot(QCustomPlot::rpQueuedReplot);
    ui->plotRight->replot(QCustomPlot::rpQueuedReplot);
  
    plotSwapReady = false;
}
```

### 6. 批量处理

数据存储线程采用批量处理策略，减少I/O操作的频率：

```cpp
void DataStorageWorker::processBatch()
{
    const int batchSize = 100;
    std::vector<DataPacket*> batch;
    batch.reserve(batchSize);
  
    // 收集一批数据包
    DataPacket* packet;
    for (int i = 0; i < batchSize; ++i) {
        if (!channelManager->getMergedQueue()->pop(packet))
            break;
  
        batch.push_back(packet);
    }
  
    // 批量写入文件
    for (auto& packet : batch) {
        // 写入文件...
  
        // 释放数据包
        channelManager->releasePacket(packet);
    }
}
```

## 设计流程

### 1. 需求分析

首先分析了系统的关键需求：

- 多通道高速数据采集
- 实时处理和显示
- 数据保存功能
- 响应式用户界面

### 2. 架构设计

基于需求，设计了多线程架构，并确定了关键组件：

- 通道管理器
- 无锁队列
- 内存池
- 工作线程类

### 3. 数据结构设计

为了更好地组织和管理数据，进行了以下设计：

- 分离探头参数（ProbeConfig）和实际数据（DataPacket）
- 使用指针关联数据包和对应的探头配置
- 确保数据结构清晰且高效

### 4. 性能优化

针对高速数据采集场景，进行了以下优化：

- 使用无锁数据结构减少线程争用
- 通道分离减少不同通道间的干扰
- 内存池避免频繁内存分配
- 双缓冲显示提高UI响应性
- 批量处理提高I/O效率

### 5. 实现与测试

按照设计逐步实现各个组件，并进行了性能测试：

- 测试不同通道数量下的数据吞吐能力
- 测试UI响应性
- 测试长时间运行的稳定性

## 使用方法

### 1. 启动系统

运行ECT应用程序，将显示主界面。

### 2. 配置网络连接

点击"Settings > Network"菜单，配置数据采集服务器的IP地址和端口。

### 3. 配置通道

点击"More... > Channel Config"，添加和配置数据采集通道。

### 4. 开始采集

点击"Start"按钮开始数据采集。系统将自动连接到服务器并开始接收数据。

### 5. 数据保存

- 自动保存：点击"More... > Enable Auto Save"启用自动保存功能。
- 手动保存：点击"File > Save"手动保存当前数据。

### 6. 数据分析

使用界面上的控制面板调整显示参数，如驱动频率、采集频率、增益等。

## 最近修复的Bug

1. **内存泄漏问题**：修复了DataAcquisitionWorker中使用静态ProbeConfig导致的内存共享问题，现在为每个数据包创建独立的ProbeConfig对象
2. **线程安全问题**：改进了DataProcessWorker和DataStorageWorker的线程退出机制，使用QtConcurrent实现更安全的线程管理
3. **数据竞争问题**：优化了ChannelManager::distributePacket中的锁策略，避免了可能的死锁和数据竞争
4. **资源释放问题**：确保DataStorageWorker中的文件资源在关闭时正确释放
5. **内存管理问题**：增强了MemoryPool的线程安全性，改进了数据包的分配和释放机制
