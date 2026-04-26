/*
 * @Descripttion:
 * @version: 2.0.0
 * @Author: June0821
 * @Date: 2025-10-15 15:41:10
 * @LastEditors: June0821
 * @LastEditTime: 2026-04-01 00:00:00
 */
#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QVector>
#include <QHostAddress>
#include <QMutex>
#include <QTimer>
#include <array>

// ─────────────────────────────────────────────
//  数据结构
// ─────────────────────────────────────────────

/**
 * @brief DA 单通道配置参数
 *
 * freq:  激励频率，单位 Hz，范围 1 – 10,000,000
 * phase: 相位，单位 度，范围 0 – 359
 * amp:   幅度，单位 %，范围 0 – 60（协议限制，超出自动截断）
 */
struct DaChannelConfig {
    int ch;     // 通道号，1 – 16
    int index;  // 通道索引，通常为 1
    int freq;   // Hz
    int phase;  // 度
    int amp;    // %
};

/**
 * @brief ADC 单通道采集数据
 *
 * data: 512 个采样点，raw ADC code（uint16，little-endian 解析后存为 quint16）
 */
struct AdcChannelData {
    int ch;
    int index;
    QVector<quint16> data; // 固定 512 点
};

// 让自定义数据类型可以通过 Qt 信号槽跨线程/排队连接传递。
// 后续若 MainWindow 与 DeviceManager 不在同一线程，adcDataReady 仍可正常工作。
Q_DECLARE_METATYPE(AdcChannelData)
Q_DECLARE_METATYPE(QVector<AdcChannelData>)

// ─────────────────────────────────────────────
//  枚举
// ─────────────────────────────────────────────

/** ADC 采样率，仅支持以下五档 */
enum class SampleRate : quint32 {
    SR_1K    =  1000,
    SR_50K   =  50000,
    SR_100K  = 100000,
    SR_5M    = 5000000,
    SR_25M   = 25000000
};

/** TCP 连接状态 */
enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected
};

// 连接状态也注册为元类型，便于通过 connectionStateChanged 信号发送。
Q_DECLARE_METATYPE(ConnectionState)

// ─────────────────────────────────────────────
//  DeviceManager
// ─────────────────────────────────────────────

class DeviceManager : public QObject
{
    Q_OBJECT

public:
    // 协议中 ADC 固定为 16 通道，每通道固定 512 个点。
    // 当前实现严格按旧版 Python 协议写死，后续若下位机协议升级，可优先改这几项常量。
    static constexpr int ADC_CHANNELS        = 16;
    static constexpr int ADC_SAMPLES_PER_CH  = 512;
    static constexpr int ADC_FRAME_SIZE      = 16460;  // 总帧长（字节）
    static constexpr int DA_CHANNELS         = 16;
    static constexpr int DA_MAX_AMP          = 60;     // 幅度上限（%）

    explicit DeviceManager(QObject *parent = nullptr);
    ~DeviceManager();

    // ── 连接管理 ──────────────────────────────
    /**
     * @brief 连接到下位机 TCP 服务端
     * @param host 下位机 IP 地址
     * @param port 下位机端口，默认 8899，可按实际协议修改
     *
     * 该接口只负责发起异步连接，不阻塞 UI。
     * 真实连接结果通过 onConnected/onSocketError 回调，并转成信号通知界面层。
     */
    void connectToDevice(const QString &host, quint16 port = 8899);
    void disconnectFromDevice();
    ConnectionState connectionState() const { return m_connState; }

    // ── DA 配置 ───────────────────────────────
    /**
     * @brief 发送 DA 通道配置
     * @param channels 长度必须为 16，不足 16 项将返回 false
     * @return 发送是否成功（连接已建立且写入无误）
     */
    bool sendDaConfig(const QVector<DaChannelConfig> &channels);

    /**
     * @brief 返回上一次设置的 DA 配置（默认为 V2.0 参考配置）
     */
    QVector<DaChannelConfig> defaultDaConfig() const;

    // ── AD 控制 ───────────────────────────────
    /**
     * @brief 发送采样率配置帧
     * @param rate 枚举值，仅支持五档
     */
    bool sendSampleRateConfig(SampleRate rate);

    /**
     * @brief 发送启动采样命令
     *
     * 与 Python 版本 ad_start_sample_conf() 对应，发送 4 字节固定命令帧。
     */
    bool startSampling();

    // ── 数据读取 ──────────────────────────────
    /** @brief 获取最近一帧解析好的 ADC 数据（线程安全） */
    QVector<AdcChannelData> getAdcData() const;

    /**
     * @brief 计算各通道 Vpp（mV），按 V2.0 硬件通道顺序输出 4×4 矩阵（展平为 16 元素）
     *
     * 计算公式：Vpp_mV = 1.03 × 0.122 × (top5_avg - bottom5_avg)
     * 矩阵索引：row 0–3，col 0–3，展平顺序为行优先
     */
    QVector<float> calcVpp() const;

    /**
     * @brief 计算灵敏度（相对变化量，%）
     * @param baseline 基线 Vpp（4×4 矩阵展平，调用 calcVpp() 获得）
     * @return 同维度灵敏度矩阵，绝对值 ≤ 2% 的项置 0
     */
    QVector<float> calcSensitivity(const QVector<float> &baseline) const;

signals:
    /** 连接状态变化 */
    void connectionStateChanged(ConnectionState state);

    /**
     * @brief 收到并解析完整 ADC 数据帧后发出
     *
     * 信号参数中携带最近一帧 16 通道的完整 512 点采样数据，
     * UI 或上层算法模块可以直接订阅该信号做绘图、存盘、特征提取等操作。
     */
    void adcDataReady(QVector<AdcChannelData> data);

    /** 发生错误 */
    void errorOccurred(const QString &msg);

private slots:
    // socket 状态变化统一收敛到这些槽中，避免 MainWindow 直接操作底层 QTcpSocket。
    void onConnected();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onDataReceived();

private:
    // ── 帧打包辅助 ────────────────────────────
    // 这三个函数只负责“协议层”的二进制打包，
    // 业务层只需要准备结构化参数，不需要关心 little-endian 细节。
    QByteArray buildDaFrame(const QVector<DaChannelConfig> &channels) const;
    QByteArray buildSampleRateFrame(SampleRate rate) const;
    QByteArray buildStartSampleFrame() const;

    // ── ADC 帧解析辅助 ────────────────────────
    /**
     * @brief 在接收缓冲中寻找帧头
     * @return 帧头偏移；如果未找到则返回 -1
     *
     * TCP 是字节流协议，不保证一次 readyRead() 就正好拿到完整一帧，
     * 因此必须在粘包/半包缓冲区中持续查找合法帧头。
     */
    int findFrameHeader(const QByteArray &buf, int from = 0) const;
    /**
     * @brief 解析完整 16460 字节 ADC 帧并刷新缓存
     *
     * 解析成功后 m_adcData 会被整帧替换，
     * 这样上层始终读取到一份结构完整、通道对齐的数据快照。
     */
    bool parseAdcFrame(const QByteArray &frame);

    // ── Vpp 计算辅助 ──────────────────────────
    // 对单通道 512 点数据计算 Vpp。
    // 这里沿用 Python 版策略：取最大 5 点均值减最小 5 点均值，降低尖峰噪声影响。
    float computeChannelVpp(const QVector<quint16> &samples) const;

    // ── 成员 ──────────────────────────────────
    // 唯一的底层通信对象，DeviceManager 对外隐藏 socket 细节。
    QTcpSocket       *m_socket;
    ConnectionState   m_connState;
    QByteArray        m_receiveBuffer;    // TCP 粘包/半包缓冲

    // m_adcData 由 readyRead 回调写入，也可能被 UI 或算法线程读取，因此加锁保护。
    mutable QMutex    m_dataMutex;
    QVector<AdcChannelData> m_adcData;   // 最近一帧数据

    // 协议常量（帧头/命令字节）
    // 这些字节序列完全对应 Python adda_parser 中的固定命令。
    static const QByteArray DA_CONF_HEADER;      // 12 bytes
    static const QByteArray AD_START_CMD;        // 4 bytes
    static const QByteArray AD_RATE_CMD;         // 4 bytes
    static const QByteArray ADC_DATA_HEADER;     // 4 bytes
    static const QByteArray ADC_DATA_LEN_TAG;    // 4 bytes，[8:12] 校验用
};

#endif // DEVICEMANAGER_H
