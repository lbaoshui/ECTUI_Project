/*
 * @file framebuffer.h
 * @brief AD7768 采集数据有界环形缓冲池（SPSC，无锁）
 *
 * 本模块提供通用的单生产者单消费者（SPSC）帧缓冲模板类 SpscFrameBuffer<T>，
 * 用于在 DeviceManager 解析线程与外部消费模块（UI / 算法）之间传递结构化数据。
 *
 * 典型用法：
 * @code
 * SpscFrameBuffer<BufferedLockinPacket> lockinBuffer{1024};
 *
 * // 生产者（解析线程）
 * lockinBuffer.push(packet);
 *
 * // 消费者（计算 / UI 线程）
 * auto packets = lockinBuffer.take(100);          // 批量取，取走后从池中删除
 * BufferedLockinPacket latest;
 * if (lockinBuffer.latest(&latest)) { ... }        // 只读最新一帧，不删除
 * @endcode
 *
 * 线程模型：
 * - push()        仅生产者调用
 * - take/latest/size/clear()  仅消费者调用
 * - 不支持多线程同时消费，也不支持 push 与 clear 并发
 *
 * 溢出策略：
 * - 缓冲满时 push() 无条件写入，覆盖最旧槽位
 * - take() 检测到积压超过容量时，自动跳过已被覆盖的帧
 */
#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <QVector>
#include <QtGlobal>

#include <atomic>
#include <utility>

/**
 * @brief 单生产者单消费者（SPSC：Single Producer Single Consumer）有界环形缓冲池
 *
 * 内部使用固定容量 QVector 作为环形槽位，读写位置由两个原子整型下标维护。
 * 写指针 m_writeIndex 仅由生产者更新，读指针 m_readIndex 仅由消费者更新，
 * 因此不需要 QMutex，适合高吞吐、低延迟的数据转发场景。
 *
 * @tparam T 帧数据类型，需支持默认构造、移动构造和移动赋值（如 BufferedLockinPacket）
 */
template <typename T>
class SpscFrameBuffer
{
public:
    /**
     * @brief 构造指定容量的缓冲池
     * @param capacity 最大可缓存帧数；若传入 <= 0，则强制为 1
     *
     * 构造时会预分配 capacity 个槽位，避免 push 时频繁扩容。
     */
    explicit SpscFrameBuffer(int capacity)
        : m_capacity(capacity > 0 ? capacity : 1)
    {
        m_buffer.resize(m_capacity);
    }

    /**
     * @brief 生产者写入一帧（无条件写入，不读 m_readIndex）
     * @param value 待写入的数据帧（会被 move 进缓冲池）
     *
     * 缓冲满时直接覆盖最旧槽位。溢出检测和追赶由 take() 完成。
     */
    void push(T value)
    {
        const unsigned int writeIndex = m_writeIndex.load(std::memory_order_relaxed);
        m_buffer[writeIndex % m_capacity] = std::move(value);
        m_writeIndex.store(writeIndex + 1, std::memory_order_release);
    }

    /**
     * @brief 消费者批量取帧
     * @param maxCount 最多取多少帧；<= 0 时返回空 QVector
     * @return 取出的帧列表；不足 maxCount 时有多少返回多少
     *
     * 取走的帧会从缓冲池中删除（readIndex 前移），适合算法线程批量处理。
     */
    QVector<T> take(int maxCount)
    {
        QVector<T> frames;
        if (maxCount <= 0) {
            return frames;
        }

        unsigned int readIndex = m_readIndex.load(std::memory_order_relaxed);
        const unsigned int writeIndex = m_writeIndex.load(std::memory_order_acquire);
        unsigned int availableCount = writeIndex - readIndex;
        if (availableCount <= 0) {
            return frames;
        }

        // 若积压超过容量，说明生产者已覆盖未读帧，跳过丢失的帧。
        if (availableCount > static_cast<unsigned int>(m_capacity)) {
            const unsigned int droppedCount = availableCount - m_capacity;
            readIndex += droppedCount;
            availableCount = m_capacity;
        }

        const unsigned int takeCount = qMin(static_cast<unsigned int>(maxCount), availableCount);
        frames.reserve(takeCount);

        for (unsigned int i = 0; i < takeCount; ++i) {
            frames.append(std::move(m_buffer[(readIndex + i) % m_capacity]));
        }

        m_readIndex.store(readIndex + takeCount, std::memory_order_release);
        return frames;
    }

    /**
     * @brief 读取最新一帧，但不从缓冲池删除
     * @param out 输出参数，成功时写入最新帧的拷贝
     * @return 缓冲池非空返回 true，否则 false
     *
     * 适合 UI 定时刷新：始终展示最新数据，不影响 take() 的积压计数。
     * 注意：T 较大时会发生整帧拷贝。
     */
    bool latest(T *out) const
    {
        if (out == nullptr) {
            return false;
        }

        // 注意：两次 acquire 加载之间值可能变化，因此快照不是原子视图。
        // 最坏情况是误判为空（实际有数据），UI 刷新场景下可接受。
        const unsigned int writeIndex = m_writeIndex.load(std::memory_order_acquire);
        const unsigned int readIndex = m_readIndex.load(std::memory_order_acquire);
        if (writeIndex <= readIndex) {
            return false;
        }

        *out = m_buffer[(writeIndex - 1) % m_capacity];
        return true;
    }

    /**
     * @brief 当前缓冲池中尚未被 take 的帧数
     * @return [0, capacity()] 范围内的整数
     */
    int size() const
    {
        const unsigned int writeIndex = m_writeIndex.load(std::memory_order_acquire);
        const unsigned int readIndex = m_readIndex.load(std::memory_order_acquire);
        const unsigned int availableCount = writeIndex - readIndex;
        Q_ASSERT(availableCount <= static_cast<unsigned int>(m_capacity));
        return static_cast<int>(availableCount);
    }

    /** @brief 缓冲池最大容量（帧数） */
    int capacity() const { return m_capacity; }

    /**
     * @brief 清空缓冲池并释放槽位内大对象内存
     *
     * 重置读写下标，并将每个槽位置为 T{}。
     * 通常在设备断开连接或重新连接时调用。
     * 调用期间不应与 push() 并发。
     */
    void clear()
    {
        m_writeIndex.store(0, std::memory_order_relaxed);
        m_readIndex.store(0, std::memory_order_release);
        for (int i = 0; i < m_capacity; ++i) {
            m_buffer[i] = T{};
        }
    }

private:
    QVector<T> m_buffer;                       ///< 预分配的环形槽位数组
    int m_capacity = 0;                        ///< 最大帧数
    std::atomic<unsigned int> m_writeIndex{0}; ///< 下一帧写入位置（仅生产者修改）
    std::atomic<unsigned int> m_readIndex{0};  ///< 下一帧读取位置（仅消费者修改）
};

#endif // FRAMEBUFFER_H
