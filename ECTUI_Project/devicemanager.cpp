#include "devicemanager.h"

#include <QDataStream>
#include <QDebug>
#include <QMutexLocker>
#include <QtEndian>

#include <algorithm>
#include <utility>

namespace {

// Python 版本中 Vpp 的换算系数为 1.03 * 0.122，这里原样保留。
// 如果后续硬件前端增益或标定方式变化，只需要修改这一处即可。
constexpr float kVppScale = 1.03f * 0.122f;

constexpr quint32 kAd7768PacketId0 = 0xAA55AA55;        // 数据帧的包头标识
constexpr quint32 kAd7768LockinPacketId = 0xAA55AA55;   // 锁相结果包的包头标识
constexpr quint32 kAd7768RawAdcPacketId = 0xAA55CC33;   // 原始ADC波形包的包头标识
constexpr int kAd7768PacketHeaderSize = 16;
constexpr int kAd7768LockinFrameBytes = 64;
constexpr int kAd7768RawSampleBytes = 32;
constexpr int kAd7768MaxPayloadBytes = 16 * 1024 * 1024;   // 最大负载字节数的限制

const QByteArray kAd7768PacketHeaderBytes = QByteArray::fromHex("55aa55aa");   // 包头字节

void appendUInt32LE(QByteArray &buffer, quint32 value)
{
    // DA/AD 配置协议按 little-endian 发送 32 位整数。
    char bytes[4];
    qToLittleEndian<quint32>(value, reinterpret_cast<uchar *>(bytes));
    buffer.append(bytes, sizeof(bytes));
}

quint16 readUInt16LE(const char *data)
{
    // ADC 原始采样与通道索引都按 little-endian 回传。
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(data));
}

quint32 readUInt32LE(const char *data)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data));
}

qint32 readInt32LE(const char *data)
{
    return qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(data));
}

} // namespace

const QByteArray DeviceManager::DA_CONF_HEADER =
    QByteArray::fromHex("ccff55aa0001000010000000");
const QByteArray DeviceManager::AD_START_CMD =
    QByteArray::fromHex("a0ff55aa");                      // 采样开始的指令
const QByteArray DeviceManager::AD_RATE_CMD =
    QByteArray::fromHex("20aa55aa");                      // 配置开发板采样率
const QByteArray DeviceManager::ADC_DATA_HEADER =
    QByteArray::fromHex("33dd33dd");                      // 数据包标志
const QByteArray DeviceManager::ADC_DATA_LEN_TAG =
    QByteArray::fromHex("40400000");                      // 旧版数据包长度：16384个byte
const QByteArray DeviceManager::BOARD_INFO_RESPONSE_HEADER =
    QByteArray::fromHex("10aa55aa");                      // 0xAA55AA10 LE 字节序

// 构造函数
DeviceManager::DeviceManager(QObject *parent)
    : QObject(parent),
      m_server(new QTcpServer(this)),
      m_socket(nullptr),
      m_connState(ConnectionState::Disconnected)
    //   m_adcData(ADC_CHANNELS)
{
    // // 预初始化 16 个通道的数据容器，避免首次收包前上层读取到空结构。
    // for (int channel = 0; channel < ADC_CHANNELS; ++channel) {
    //     m_adcData[channel].ch = channel + 1;
    //     m_adcData[channel].index = 1;
    //     m_adcData[channel].data.fill(0, ADC_SAMPLES_PER_CH);
    // }

    // 注册元类型，保证自定义类型可经由 Qt 信号槽系统传递。
    qRegisterMetaType<ConnectionState>("ConnectionState");
    // qRegisterMetaType<AdcChannelData>("AdcChannelData");
    // qRegisterMetaType<QVector<AdcChannelData>>("QVector<AdcChannelData>");
    // qRegisterMetaType<LockinChannelData>("LockinChannelData");
    // qRegisterMetaType<QVector<LockinChannelData>>("QVector<LockinChannelData>");
    // qRegisterMetaType<LockinFrameData>("LockinFrameData");
    // qRegisterMetaType<QVector<LockinFrameData>>("QVector<LockinFrameData>");
    // qRegisterMetaType<RawAdcChannelData>("RawAdcChannelData");
    // qRegisterMetaType<QVector<RawAdcChannelData>>("QVector<RawAdcChannelData>");
    qRegisterMetaType<LockinChannelPacket>("LockinChannelPacket");
    qRegisterMetaType<RawAdcChannelPacket>("RawAdcChannelPacket");
    qRegisterMetaType<BoardInfo>("BoardInfo");

    // 为每个 AD7768 通道预分配独立无锁缓冲池。
    m_lockinBuffers.reserve(AD7768_CHANNELS);
    m_rawAdcBuffers.reserve(AD7768_CHANNELS);
    for (int i = 0; i < AD7768_CHANNELS; ++i) {
        m_lockinBuffers.push_back(
            std::make_unique<SpscFrameBuffer<LockinChannelPacket>>(LOCKIN_BUFFER_CAPACITY));
        m_rawAdcBuffers.push_back(
            std::make_unique<SpscFrameBuffer<RawAdcChannelPacket>>(RAW_ADC_BUFFER_CAPACITY));
    }

    connect(m_server, &QTcpServer::newConnection, this, &DeviceManager::onNewConnection);
}

DeviceManager::~DeviceManager()
{
    stopListening();
}

// 启动 TCP 服务端，等待下位机连接
void DeviceManager::startListening(quint16 port)
{
    if (m_server->isListening() && m_server->serverPort() == port) {
        qDebug() << "已经处理连接中状态!\n";
        return;
    }
    if (m_connState == ConnectionState::Connecting || m_connState == ConnectionState::Connected) {
        stopListening();
    }
    

    m_connState = ConnectionState::Connecting;
    emit connectionStateChanged(m_connState);

    if (!m_server->listen(QHostAddress::Any, port)) {
        m_connState = ConnectionState::Disconnected;
        emit connectionStateChanged(m_connState);
        emit errorOccurred(QStringLiteral("TCP 服务端启动失败: %1").arg(m_server->errorString()));
    }
}

bool DeviceManager::isListening() const
{
    return m_server->isListening();
}

// 停止监听并断开已有客户端连接
void DeviceManager::stopListening()
{
    qDebug() << "停止监听stopListening\n" ;
    resetStreamingState();
    qDebug() << "重置流式数据状态\n" ;
    sendStopAcquisition();  // 发送停止采集命令
    qDebug() << "发送停止采集命令\n" ;
    if (m_socket) {
        qDebug() << "断开socket连接\n" ;
        m_socket->disconnect();
        qDebug() << "断开socket连接成功\n" ;
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->waitForDisconnected(1000);
            qDebug() << "等待断开socket连接完成\n" ;
        }
        qDebug() << "删除socket\n" ;
        m_socket->deleteLater();
        qDebug() << "删除socket成功\n" ;
        m_socket = nullptr;
    }

    m_server->close();
    delete m_server;
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &DeviceManager::onNewConnection);

    if (m_connState != ConnectionState::Disconnected) {
        m_connState = ConnectionState::Disconnected;
        emit connectionStateChanged(m_connState);
    }
}

// 发送DA配置,即为激励的DA通道配置参数，如频率、相位、幅值、
bool DeviceManager::sendDaConfig(const QVector<DaChannelConfig> &channels)
{
    // 先做连接态与参数完整性校验，再进入协议打包阶段。
    if (m_connState != ConnectionState::Connected) {
        emit errorOccurred(QStringLiteral("设备未连接，无法发送 DA 配置。"));
        return false;
    }

    const QByteArray frame = buildDaFrame(channels);
    // write() 只是把数据交给 Qt socket 缓冲区；waitForBytesWritten 用于确保已实际写出。
    const qint64 written = m_socket->write(frame);
    if (written != frame.size()) {
        emit errorOccurred(QStringLiteral("DA 配置发送失败。"));
        return false;
    }

    return m_socket->waitForBytesWritten(1000);
}

// 构建DA配置帧
QByteArray DeviceManager::buildDaFrame(const QVector<DaChannelConfig> &channels) const
{
    // DA 帧结构：
    // 12 字节固定头 + 16 个通道 * 4 个 uint32(index/freq/phase/amp)
    QByteArray frame = DA_CONF_HEADER;
    frame.reserve(DA_CONF_HEADER.size() + DA_CHANNELS * 16);

    for (const DaChannelConfig &channel : channels) {
        // amp 按旧协议强制夹紧到 [0, 60]，避免下位机收到非法参数。
        const int DAAmp = std::clamp(channel.amp, 0, DA_MAX_AMP);
        appendUInt32LE(frame, static_cast<quint32>(channel.index));
        appendUInt32LE(frame, static_cast<quint32>(channel.freq));
        appendUInt32LE(frame, static_cast<quint32>(channel.phase));
        appendUInt32LE(frame, static_cast<quint32>(DAAmp));
    }

    return frame;
}

// 默认的通道
QVector<DaChannelConfig> DeviceManager::defaultDaConfig() const
{
    // 这里是按照给的python代码里保持一致的顺序
    return {
        {4,  1, 10000, 0,   60},
        {3,  1, 10000, 0,   60},
        {2,  1, 10000, 0,   60},
        {1,  1, 10000, 0,   60},
        {8,  1, 10000, 0,   60},
        {7,  1, 10000, 0,   60},
        {6,  1, 10000, 0,   60},
        {5,  1, 10000, 0,   60},
        {12, 1, 10000, 0,   60},
        {11, 1, 10000, 0,   60},
        {10, 1, 10000, 0,   60},
        {9,  1, 10000, 0,   60},
        {16, 1, 10000, 0,   60},
        {15, 1, 10000, 0,   60},
        {14, 1, 10000, 0,   60},
        {13, 1, 10000, 0,   60}
    };
}


// ─────────────────────────────────────────────
// 新协议 DA 配置（AD7768，全局参数，所有通道共用）
//
// 新协议采用 16 字节定长命令帧，与旧协议 12+16×16 字节 DA 配置帧完全不同。
// 新协议所有通道共用同一套 DDS 激励参数，不需要逐通道下发。
// 格式依据 AD7768_TCP_Protocol.md §4：
//   Offset 0: 命令字 (uint32 LE)
//   Offset 4: 保留，填 0
//   Offset 8: 保留，填 0
//   Offset12: 参数值 (uint32 LE)
// ─────────────────────────────────────────────

// 构建新协议 16 字节命令帧。
// 帧布局：cmd(4B) + reserved(4B) + reserved(4B) + param(4B)，全部 little-endian。
QByteArray DeviceManager::buildNewCmdFrame(quint32 cmd, quint32 param) const
{
    QByteArray frame(16, '\0');
    char *data = frame.data();
    qToLittleEndian(cmd,   reinterpret_cast<uchar *>(data));       // [0:4)  命令字
    // offset [4:8)  保留为 0
    // offset [8:12) 保留为 0
    qToLittleEndian(param, reinterpret_cast<uchar *>(data + 12));  // [12:16) 参数值
    return frame;
}

// 发送 16 字节命令的通用入口：校验连接态 → 打包 → 写入 socket → 等待刷新。
// 所有新协议 DDS/帧长命令最终都走此函数，避免散落重复的连接判断和错误处理。
bool DeviceManager::sendNewCmd(quint32 cmd, quint32 param)
{
    if (m_connState != ConnectionState::Connected) {
        emit errorOccurred(QStringLiteral("设备未连接，无法发送命令 0x%1。").arg(cmd, 8, 16, QLatin1Char('0')));
        return false;
    }

    const QByteArray frame = buildNewCmdFrame(cmd, param);
    const qint64 written = m_socket->write(frame);
    if (written != frame.size()) {
        emit errorOccurred(QStringLiteral("命令 0x%1 发送失败。").arg(cmd, 8, 16, QLatin1Char('0')));
        qDebug() << "命令发送失败!\n";
        return false;
    }
    qDebug() << cmd << "命令发送完成!\n";
    m_socket->flush();
    return m_socket->waitForBytesWritten(1000);
}

// 新协议 DA 配置：发送全局 DDS 激励频率和相位，所有通道共用。
// 取 channels 首个元素的 freq/phase 作为全局参数下发，不区分通道。
bool DeviceManager::sendDaConfigNew(const QVector<DaChannelConfig> &channels)
{
    if (m_connState != ConnectionState::Connected) {
        emit errorOccurred(QStringLiteral("设备未连接，无法发送 DA 配置（新协议）。"));
        return false;
    }

    if (channels.isEmpty())
        return true;

    const DaChannelConfig &ch = channels.first();

    // PS 端收到 0xAA55FFD4 后按 fword = round(freqHz * 2^32 / 50e6) 换算 DDS 频率控制字
    if (!sendDdsFreqHz(static_cast<quint32>(ch.freq)))
        return false;
    // 相位字直接透传给 DDS 初始相位寄存器
    if (!sendDdsPhase(static_cast<quint32>(ch.phase)))
        return false;
    return true;
}

// 设置全局 DDS 激励频率（Hz），所有通道共用。
// PS 端收到后自动完成 fword 换算：fword = round(freqHz * 2^32 / 50000000.0)
bool DeviceManager::sendDdsFreqHz(quint32 freqHz)
{
    return sendNewCmd(CMD_SET_FREQ_HZ, freqHz);
}

// 设置全局 DDS 初始相位字（32-bit），所有通道共用。
bool DeviceManager::sendDdsPhase(quint32 phaseWord)
{
    return sendNewCmd(CMD_SET_PHASE, phaseWord);
}

// 设置每包帧数（frame_count），建议范围 1–1024。
bool DeviceManager::sendFrameLength(quint32 frameCount)
{
    return sendNewCmd(CMD_SET_FRAME, frameCount);
}

// ─────────────────────────────────────────────
// 新协议 采集控制（4 字节命令）
//
// 采集控制采用 4 字节精简命令，与旧协议 AD_START_CMD (0xA0FF55AA) 结构类似但命令字不同。
// 格式：单个 uint32 LE，无额外参数。
// ─────────────────────────────────────────────

// 构建新协议 4 字节采集控制命令。
QByteArray DeviceManager::buildNewAcqCmd(quint32 cmd) const
{
    QByteArray frame(4, '\0');
    qToLittleEndian(cmd, reinterpret_cast<uchar *>(frame.data()));
    return frame;
}

// 发送 4 字节采集控制命令的通用入口。
bool DeviceManager::sendNewAcqCmd(quint32 cmd)
{
    if (m_connState != ConnectionState::Connected) {
        emit errorOccurred(QStringLiteral("设备未连接，无法发送采集控制命令。"));
        return false;
    }

    const QByteArray frame = buildNewAcqCmd(cmd);
    const qint64 written = m_socket->write(frame);
    if (written != frame.size()) {
        emit errorOccurred(QStringLiteral("采集控制命令发送失败。"));
        return false;
    }
    return m_socket->waitForBytesWritten(1000);
}

// 开始采集：发送 0xAA55FFA0，PS 收到后启动 AD7768 数据上传。
bool DeviceManager::sendStartAcquisition()
{
    return sendNewAcqCmd(CMD_START_ACQ);
}

// 停止采集：发送 0xAA55FFB1，PS 收到后停止数据上传。
bool DeviceManager::sendStopAcquisition()
{
    return sendNewAcqCmd(CMD_STOP_ACQ);
}

// ─────────────────────────────────────────────
// 开发板信息询问

// 发送询问开发板信息命令（0xAA55AA10，4 字节）。
// 下位机收到后回应 24 字节固定包，在 onDataReceived_New 中拦截解析。
bool DeviceManager::queryBoardInfo()
{
    if (m_connState != ConnectionState::Connected) {
        emit errorOccurred(QStringLiteral("设备未连接，无法询问开发板信息。"));
        return false;
    }

    const QByteArray frame = buildNewAcqCmd(CMD_QUERY_BOARD_INFO);
    const qint64 written = m_socket->write(frame);
    if (written != frame.size()) {
        emit errorOccurred(QStringLiteral("询问开发板信息命令发送失败。"));
        return false;
    }
    return m_socket->waitForBytesWritten(1000);
}

// ─────────────────────────────────────────────
// 旧协议 AD 控制

// 发送采样率配置
bool DeviceManager::sendSampleRateConfig(SampleRate rate)
{
    // 采样率被收敛为枚举，避免外部传入协议不支持的值。
    if (m_connState != ConnectionState::Connected) {
        emit errorOccurred(QStringLiteral("设备未连接，无法发送采样率配置。"));
        qDebug() << "设备未连接，无法发送采样率配置。";
        return false;
    }

    const QByteArray frame = buildSampleRateFrame(rate);         // 构建采样率配置帧
    const qint64 written = m_socket->write(frame);               // 写入数据到socket缓冲区
    if (written != frame.size()) {
        emit errorOccurred(QStringLiteral("采样率配置发送失败。"));
        return false;
    }

    const bool ok = m_socket->waitForBytesWritten(1000);
    if (ok) {
        m_currentSampleRate = rate;
    }
    return ok;
}

// 开启采样操作
bool DeviceManager::startSampling()
{
    // 启动命令应在完成连接、DA 参数设置、AD 采样率设置之后再调用。
    if (m_connState != ConnectionState::Connected) {
        emit errorOccurred(QStringLiteral("设备未连接，无法启动采样。"));
        return false;
    }
    qDebug() << "发送开始采集指令\n" ;
    const QByteArray frame = buildStartSampleFrame();
    const qint64 written = m_socket->write(frame);      // 获取实际写入的数据量
    if (written != frame.size()) {                      // 如果写入数据量不相同，则代表数据发送失败
        emit errorOccurred(QStringLiteral("启动采样命令发送失败。"));
        return false;
    }

    return m_socket->waitForBytesWritten(1000);         // 等待数据发送完成，超时时间1000ms
}

/*
QVector<AdcChannelData> DeviceManager::getAdcData() const
{
    QMutexLocker locker(&m_dataMutex);
    return  m_adcData;
}

// 计算峰峰值
QVector<float> DeviceManager::calcVpp() const
{
    // 取最近一帧完整数据做计算，避免直接操作共享成员造成竞争。
    const QVector<AdcChannelData> adcData = getAdcData();
    if (adcData.size() != ADC_CHANNELS) {
        return {};
    }

    QVector<float> channelVpp(ADC_CHANNELS, 0.0f);
    for (int channel = 0; channel < ADC_CHANNELS; ++channel) {
        channelVpp[channel] = computeChannelVpp(adcData[channel].data);
    }

    QVector<float> reordered(ADC_CHANNELS, 0.0f);
    const int mapping[ADC_CHANNELS] = {
        13, 12, 15, 14,
         9,  8, 11, 10,
         5,  4,  7,  6,
         1,  0,  3,  2
    };

    // 这一步对应 Python get_ADC_realtime_Vpp() 中的 V2.0 通道重排逻辑。
    // 返回值虽然是一维 QVector，但逻辑上表示 4x4 阵列，按行优先展开。
    for (int i = 0; i < ADC_CHANNELS; ++i) {
        reordered[i] = channelVpp[mapping[i]];
    }

    return reordered;
}

// 按照python的方式计算灵敏度
QVector<float> DeviceManager::calcSensitivity(const QVector<float> &baseline) const
{
    // 灵敏度定义与 Python 版本一致：
    // ((realtime_Vpp - baseline) / baseline) * 100
    const QVector<float> current = calcVpp();
    if (baseline.size() != ADC_CHANNELS || current.size() != ADC_CHANNELS) {
        return {};
    }

    QVector<float> sensitivity(ADC_CHANNELS, 0.0f);
    for (int i = 0; i < ADC_CHANNELS; ++i) {
        // 基线为 0 时无法做除法，这里沿用旧版思路，直接退化为当前值。
        if (qFuzzyIsNull(baseline[i])) {
            sensitivity[i] = current[i];
        } else {
            sensitivity[i] = ((current[i] - baseline[i]) / baseline[i]) * 100.0f;
        }

        // 滤掉绝对值不超过 2% 的微小变化，减少噪声触发。
        if (sensitivity[i] >= -2.0f && sensitivity[i] <= 2.0f) {
            sensitivity[i] = 0.0f;
        }
    }

    return sensitivity;
}
*/

// QTcpServer 收到新连接，接受下位机 socket
void DeviceManager::onNewConnection()
{
    qDebug() << "收到新连接\n" ;
    while (m_server->hasPendingConnections()) {
        QTcpSocket *clientSocket = m_server->nextPendingConnection();
        if (!clientSocket)
            continue;

        // 单客户端模式：新连接到来时，直接替换旧连接
        if (m_socket) {
            resetStreamingState();
            // m_socket->disconnectFromHost();
            m_socket->disconnect();
            m_socket->deleteLater();
            m_socket = nullptr;
        }

        m_socket = clientSocket;
        connect(m_socket, &QTcpSocket::disconnected, this, &DeviceManager::onClientDisconnected);
        connect(m_socket, &QTcpSocket::readyRead, this, &DeviceManager::onDataReceived_New);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        connect(m_socket, &QTcpSocket::errorOccurred, this, &DeviceManager::onSocketError);
#else
        connect(m_socket,
                QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
                this,
                &DeviceManager::onSocketError);
#endif

        m_connState = ConnectionState::Connected;
        emit connectionStateChanged(m_connState);
    }
}

// 客户端 socket 断开
void DeviceManager::onClientDisconnected()
{
    // 仅处理来自当前有效 socket 的信号，忽略已被替换的旧 socket 的延迟信号
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock || sock != m_socket)
        return;

    resetStreamingState();
    m_socket->deleteLater();
    m_socket = nullptr;

    // 服务端若仍在监听则回到等待连接状态，否则标记为断开
    if (m_server->isListening()) {
        if (m_connState != ConnectionState::Connecting) {
            m_connState = ConnectionState::Connecting;
            emit connectionStateChanged(m_connState);
        }
    } else {
        if (m_connState != ConnectionState::Disconnected) {
            m_connState = ConnectionState::Disconnected;
            emit connectionStateChanged(m_connState);
        }
    }
}

// socket错误，更新标志位
void DeviceManager::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)

    // 仅处理来自当前有效 socket 的信号，忽略已被替换的旧 socket 的延迟信号
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock || sock != m_socket)
        return;

    resetStreamingState();
    emit errorOccurred(m_socket->errorString());
    m_socket->deleteLater();
    m_socket = nullptr;

    if (m_server->isListening()) {
        m_connState = ConnectionState::Connecting;
    } else {
        m_connState = ConnectionState::Disconnected;
    }
    emit connectionStateChanged(m_connState);
}

/*
// 旧版的数据接收解析方式
void DeviceManager::onDataReceived()
{
    // readyRead 可能一次收到半帧、一帧或多帧，因此先追加到缓冲区统一处理。
    m_receiveBuffer.append(m_socket->readAll());

    while (true) {
        // 先在缓冲区中找合法帧头；如果没有找到，只保留可能构成“半个帧头”的尾巴。
        const int headerIndex = findFrameHeader(m_receiveBuffer);
        if (headerIndex < 0) {
            if (m_receiveBuffer.size() > ADC_DATA_HEADER.size() - 1) {
                m_receiveBuffer = m_receiveBuffer.right(ADC_DATA_HEADER.size() - 1);
            }
            break;
        }

        if (headerIndex > 0) {
            m_receiveBuffer.remove(0, headerIndex);
        }

        if (m_receiveBuffer.size() < ADC_FRAME_SIZE) {
            // 已对齐到帧头，但数据还没收全，等待下一次 readyRead。
            break;
        }

        const QByteArray frame = m_receiveBuffer.left(ADC_FRAME_SIZE);
        if (frame.mid(8, ADC_DATA_LEN_TAG.size()) != ADC_DATA_LEN_TAG) {
            // 帧头撞上了伪匹配数据，向后滑动 1 字节继续搜索。
            m_receiveBuffer.remove(0, 1);
            continue;
        }

        if (!parseAdcFrame(frame)) {
            // 解析失败也只丢掉 1 字节，尽量从后续字节中恢复同步。
            m_receiveBuffer.remove(0, 1);
            continue;
        }

        // 成功解析一整帧后，移除已消费数据，并把结构化结果发给上层。
        m_receiveBuffer.remove(0, ADC_FRAME_SIZE);
        emit adcDataReady(getAdcData());
    }
}
*/

// AD7768 版本数据接收解析。
// readyRead 触发时拿到的是 TCP 字节流片段，可能出现半包、粘包或包头前有残留脏数据；
// 因此这里先追加到 m_receiveBuffer，再从缓冲区里按“包头 + 长度字段”逐包拆解。
void DeviceManager::onDataReceived_New()
{
    qDebug() << "收到数据\n" ;
    // 仅处理来自当前有效 socket 的数据，忽略已被替换的旧 socket 的延迟信号
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock || sock != m_socket)
        return;

    // 把本次 socket 已到达的所有字节追加到缓存中。
    // 因为 readAll() 不保证刚好等于一个完整 AD7768 数据包。
    m_receiveBuffer.append(m_socket->readAll());

    // 先检查是否有开发板信息回应包，独立于 AD7768 数据流解析。
    checkAndParseBoardInfoResponse();

    // 一次 readyRead 里可能已经收到了多个完整包，所以这里持续尝试解析。
    // 直到缓存中找不到包头，或者只剩下一个未收完整的包，才退出等待下一次 readyRead。
    while (true) {
        // 新协议包头固定为 55 aa 55 aa，小端读取后对应 id0 = 0xAA55AA55。
        const int headerIndex = m_receiveBuffer.indexOf(kAd7768PacketHeaderBytes);
        if (headerIndex < 0) {
            // 当前缓存里还没有完整包头。
            // 但包头可能被截断在缓存末尾，例如只收到 55 aa 55，
            // 所以最多保留包头长度 - 1 个字节，用于和下一次 readAll() 拼接。
            if (m_receiveBuffer.size() > kAd7768PacketHeaderBytes.size() - 1) {
                m_receiveBuffer = m_receiveBuffer.right(kAd7768PacketHeaderBytes.size() - 1);
            }
            break;
        }

        if (headerIndex > 0) {
            // 丢弃包头前面的脏数据或上一次异常解析留下的残余字节，使缓存从包头开始。
            m_receiveBuffer.remove(0, headerIndex);
        }

        if (m_receiveBuffer.size() < kAd7768PacketHeaderSize) {
            // 已经找到包头，但 16 字节包头还没收完整，等待后续数据。
            break;
        }

        const char *header = m_receiveBuffer.constData();
        // 包头结构：
        // id0           ：固定同步字 0xAA55AA55
        // id1           ：包类型：锁相结果包或原始 ADC 包
        // packetIndex   ：包序号，用于检测丢包
        // payloadLength ：后续 payload 字节数
        const quint32 id0 = readUInt32LE(header);
        const quint32 id1 = readUInt32LE(header + 4);
        const quint32 packetIndex = readUInt32LE(header + 8);
        const quint32 payloadLength = readUInt32LE(header + 12);

        if (id0 != kAd7768PacketId0) {
            // 理论上 indexOf 已经匹配了包头；这里再校验一次，异常时滑动 1 字节重新同步。
            m_receiveBuffer.remove(0, 1);
            continue;
        }

        if (id1 != kAd7768LockinPacketId && id1 != kAd7768RawAdcPacketId) {
            emit errorOccurred(QStringLiteral("AD7768 上传包类型未知：0x%1")
                                   .arg(id1, 8, 16, QLatin1Char('0')));
            m_receiveBuffer.remove(0, 1);
            continue;
        }

        if (payloadLength > static_cast<quint32>(kAd7768MaxPayloadBytes)) {   // 判断payloadlength大小是否有异常
            // 长度字段异常通常表示数据流已经错位，避免按错误长度申请或等待超大数据
            emit errorOccurred(QStringLiteral("AD7768 payload 过大：%1 字节").arg(payloadLength));
            m_receiveBuffer.remove(0, 1);
            continue;
        }

        const int totalLength = kAd7768PacketHeaderSize + static_cast<int>(payloadLength);
        if (m_receiveBuffer.size() < totalLength) {
            // 包头已完整，但 payload 还没收全；保留当前缓存，下一次 readyRead 后继续解析。
            break;
        }

        // 到这里说明已经拿到一个完整 AD7768 包，可以把 payload 交给业务解析函数，即把包头去掉
        const QByteArray payload = m_receiveBuffer.mid(kAd7768PacketHeaderSize,
                                                       static_cast<int>(payloadLength));
        bool parsed = false;
        if (id1 == kAd7768LockinPacketId) {      // 锁相包
            parsed = parseLockinPacket_New(payload, packetIndex);
        } else {                                 // 原始包
            parsed = parseRawAdcPacket_New(payload, packetIndex);
        }

        if (parsed) {
            checkPacketIndex_New(packetIndex);
        }

        // 将解析完的数据从缓冲区中移除
        m_receiveBuffer.remove(0, totalLength);
    }
}

// 解析锁相包：按通道拆分，每通道独立推入各自的缓冲池
bool DeviceManager::parseLockinPacket_New(const QByteArray &payload, quint32 packetIndex)
{
    // 判断payload的数据是否有异常
    if (payload.size() % kAd7768LockinFrameBytes != 0) {
        emit errorOccurred(QStringLiteral("锁相包长度非法：%1 字节").arg(payload.size()));
        return false;
    }

    const int frameCount = payload.size() / kAd7768LockinFrameBytes; // 解析有多少帧数据包（每帧64字节，即8个通道）

    // 为每个通道预分配容量。
    QVector<LockinChannelPacket> channelPackets(AD7768_CHANNELS);
    for (int ch = 0; ch < AD7768_CHANNELS; ++ch) {
        channelPackets[ch].packetIndex = packetIndex;
        channelPackets[ch].ampMv.reserve(frameCount);
        channelPackets[ch].phaseDeg.reserve(frameCount);
        // channelPackets[ch].vppMv.reserve(frameCount);
    }

    // 将数据放入各自的通道中
    const char *raw = payload.constData();
    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {    // N帧数据
        const int frameOffset = frameIndex * kAd7768LockinFrameBytes;
        for (int ch = 0; ch < AD7768_CHANNELS; ++ch) {                   // 每帧数据64个字节，8个通道
            const int channelOffset = frameOffset + ch * 8;
            const qint32 ampRaw = readInt32LE(raw + channelOffset);      // 拿出4个字节的数据
            const qint32 phaseRaw = readInt32LE(raw + channelOffset + 4);

            const float ampMv = static_cast<float>(ampRaw) / 100.0f;      // 换算出实际的值
            const float phaseDeg = static_cast<float>(phaseRaw) / 100.0f;
            // const float vppMv = ampMv * 2.0f;                             // 幅值包络

            channelPackets[ch].ampMv.append(ampMv);
            channelPackets[ch].phaseDeg.append(phaseDeg);
            // channelPackets[ch].vppMv.append(vppMv);
        }
    }

    // 各通道独立推入各自的无锁缓冲池。
    for (int ch = 0; ch < AD7768_CHANNELS; ++ch) {
        // emit lockinChannelDataReady(ch + 1, packetIndex);          // 数据写入缓冲池的通知信号，可在缓冲区无数据等待数据时通知消费者线程，暂时不用
        m_lockinBuffers[ch]->push(std::move(channelPackets[ch]));
    }

    return true;
}

// 解析原始 ADC 波形包：按通道拆分，每通道独立推入各自的缓冲池。
bool DeviceManager::parseRawAdcPacket_New(const QByteArray &payload, quint32 packetIndex)
{
    if (payload.size() % kAd7768RawSampleBytes != 0) {
        emit errorOccurred(QStringLiteral("原始 ADC 包长度非法：%1 字节").arg(payload.size()));
        return false;
    }

    const int sampleCount = payload.size() / kAd7768RawSampleBytes;

    QVector<RawAdcChannelPacket> channelPackets(AD7768_CHANNELS);
    for (int ch = 0; ch < AD7768_CHANNELS; ++ch) {
        channelPackets[ch].packetIndex = packetIndex;
        channelPackets[ch].adcCodes.reserve(sampleCount);
        channelPackets[ch].voltageMv.reserve(sampleCount);
    }

    const char *raw = payload.constData();
    for (int sample = 0; sample < sampleCount; ++sample) {
        const int sampleOffset = sample * kAd7768RawSampleBytes;
        for (int ch = 0; ch < AD7768_CHANNELS; ++ch) {
            const int byteOffset = sampleOffset + ch * 4;
            const qint32 adcCode = readInt32LE(raw + byteOffset);
            const double voltageMv = static_cast<double>(adcCode) * 4096.0 / 8388607.0;

            channelPackets[ch].adcCodes.append(adcCode);
            channelPackets[ch].voltageMv.append(voltageMv);
        }
    }

    for (int ch = 0; ch < AD7768_CHANNELS; ++ch) {
        emit rawAdcChannelDataReady(ch + 1, packetIndex);
        m_rawAdcBuffers[ch]->push(std::move(channelPackets[ch]));
    }

    return true;
}

void DeviceManager::checkPacketIndex_New(quint32 packetIndex)
{
    if (m_hasLastNewPacketIndex && packetIndex != m_lastNewPacketIndex + 1) {
        emit errorOccurred(QStringLiteral("AD7768 上传包序号跳变：上一包 %1，当前包 %2")
                               .arg(m_lastNewPacketIndex)
                               .arg(packetIndex));
    }

    m_hasLastNewPacketIndex = true;
    m_lastNewPacketIndex = packetIndex;
}

// 从 m_receiveBuffer 中扫描开发板信息回应头（0xAA55AA10），
// 找到完整 24 字节后调用 parseBoardInfoResponse 解析。
// 解析成功后移除已消费数据；解析失败则滑动 1 字节重试。
void DeviceManager::checkAndParseBoardInfoResponse()
{
    while (m_receiveBuffer.size() >= BOARD_INFO_RESPONSE_SIZE) {
        const int infoPos = m_receiveBuffer.indexOf(BOARD_INFO_RESPONSE_HEADER);
        if (infoPos < 0)
            break;

        if (infoPos > 0) {
            m_receiveBuffer.remove(0, infoPos);
        }

        if (m_receiveBuffer.size() < BOARD_INFO_RESPONSE_SIZE) {
            break;  // 包头已找到但数据未收全，等下一次 readyRead
        }

        if (!parseBoardInfoResponse(m_receiveBuffer.left(BOARD_INFO_RESPONSE_SIZE))) {
            m_receiveBuffer.remove(0, 1);  // 解析失败，滑动 1 字节重试
            continue;
        }

        m_receiveBuffer.remove(0, BOARD_INFO_RESPONSE_SIZE);  // 解析成功，移除已消费数据
    }
}

// 解析开发板信息回应包（24 字节固定长度）。
// 格式依据设计需求 §三.2-1：
//   [0:4)   CMD (0xAA55AA10)
//   [4:10)  MAC 地址 (6 字节)
//   [10:14) IP 地址 (4 字节)
//   [14]    符号位 (0x00=无符号, 0x01=有符号)
//   [15]    每通道有效数据位数 (默认 0x0B=12bit)
//   [16:20) 当前采样率 (uint32 LE, Hz)
//   [20:24) 每次发送数据字节数 (uint32 LE)
bool DeviceManager::parseBoardInfoResponse(const QByteArray &data)
{
    if (data.size() < BOARD_INFO_RESPONSE_SIZE)
        return false;

    const char *raw = data.constData();
    const quint32 cmd = readUInt32LE(raw);
    if (cmd != CMD_QUERY_BOARD_INFO)
        return false;

    BoardInfo info;
    info.valid = true;

    // MAC 地址：6 字节，格式 XX:XX:XX:XX:XX:XX
    info.macAddress = QString::asprintf("%02X:%02X:%02X:%02X:%02X:%02X",
        static_cast<uchar>(raw[4]),  static_cast<uchar>(raw[5]),
        static_cast<uchar>(raw[6]),  static_cast<uchar>(raw[7]),
        static_cast<uchar>(raw[8]),  static_cast<uchar>(raw[9]));

    // IP 地址：4 字节，大端序 IPv4
    info.ipAddress = QStringLiteral("%1.%2.%3.%4")
        .arg(static_cast<uchar>(raw[10]))
        .arg(static_cast<uchar>(raw[11]))
        .arg(static_cast<uchar>(raw[12]))
        .arg(static_cast<uchar>(raw[13]));

    info.signedData   = (static_cast<uchar>(raw[14]) != 0x00);
    info.dataBits     = static_cast<uchar>(raw[15]);
    info.sampleRateHz = readUInt32LE(raw + 16);
    info.bytesPerSend = readUInt32LE(raw + 20);

    m_lastBoardInfo = info;
    emit boardInfoReceived(info);
    return true;
}

// 重置所有流式数据状态，在断联、重连或停止监听时调用，确保新连接从干净状态开始。
void DeviceManager::resetStreamingState()
{
    m_receiveBuffer.clear();                 // 清空 TCP 粘包/半包接收缓冲
    m_hasLastNewPacketIndex = false;         // 复位丢包检测状态
    m_lastNewPacketIndex = 0;
    for (auto &buf : m_lockinBuffers) {     // 清空所有通道的锁相数据缓冲池
        buf->clear();
    }
    for (auto &buf : m_rawAdcBuffers) {     // 清空所有通道的原始 ADC 数据缓冲池
        buf->clear();
    }
}

QByteArray DeviceManager::buildSampleRateFrame(SampleRate rate) const
{
    // AD 采样率帧为 4 字节命令头 + 4 字节 little-endian 采样率值。
    QByteArray frame = AD_RATE_CMD;
    frame.reserve(AD_RATE_CMD.size() + 4);
    appendUInt32LE(frame, static_cast<quint32>(rate));
    return frame;
}

QByteArray DeviceManager::buildStartSampleFrame() const
{
    return AD_START_CMD;
}

int DeviceManager::findFrameHeader(const QByteArray &buf, int from) const
{
    // 直接在字节流中搜索固定帧头 0x33DD33DD。
    return buf.indexOf(ADC_DATA_HEADER, from);
}

/*
bool DeviceManager::parseAdcFrame(const QByteArray &frame)
{
    // 先做长度和关键字段校验，避免后续按固定偏移解析时越界或误解包。
    if (frame.size() != ADC_FRAME_SIZE) {
        return false;
    }

    if (!frame.startsWith(ADC_DATA_HEADER) ||
        frame.mid(8, ADC_DATA_LEN_TAG.size()) != ADC_DATA_LEN_TAG) {
        return false;
    }

    QVector<AdcChannelData> parsed(ADC_CHANNELS);
    for (int channel = 0; channel < ADC_CHANNELS; ++channel) {
        parsed[channel].ch = channel + 1;
        parsed[channel].index = 1;
        parsed[channel].data.resize(ADC_SAMPLES_PER_CH);
    }

    const char *raw = frame.constData();
    const int adcDataOffset = 12;
    const int groupWords = 4 * ADC_SAMPLES_PER_CH;
    const int groupBytes = groupWords * static_cast<int>(sizeof(quint16));

    // Python 旧版协议中 16 个通道被分成 4 组，每组 4 个通道交错存放：
    // ch1-4, ch5-8, ch9-12, ch13-16
    // 每组内部排列为 chA[0], chB[0], chC[0], chD[0], chA[1]...
    for (int group = 0; group < 4; ++group) {
        const int groupOffset = adcDataOffset + group * groupBytes;
        for (int sampleIndex = 0; sampleIndex < ADC_SAMPLES_PER_CH; ++sampleIndex) {
            for (int chOffset = 0; chOffset < 4; ++chOffset) {
                const int byteOffset = groupOffset + (sampleIndex * 4 + chOffset) * 2;
                parsed[group * 4 + chOffset].data[sampleIndex] = readUInt16LE(raw + byteOffset);
            }
        }
    }

    // 紧跟在 16384 字节 ADC 数据后的 32 字节是 16 个通道索引，每通道 2 字节。
    const int channelIndexOffset = adcDataOffset + 16384;
    for (int channel = 0; channel < ADC_CHANNELS; ++channel) {
        parsed[channel].index = readUInt16LE(raw + channelIndexOffset + channel * 2);
    }

    {
        // 用整帧替换而不是边解析边写共享成员，保证上层拿到的数据总是自洽的。
        QMutexLocker locker(&m_dataMutex);
        m_adcData = parsed;
    }

    return true;
}
*/

/*
float DeviceManager::computeChannelVpp(const QVector<quint32> &samples) const
{
    if (samples.isEmpty()) {
        return 0.0f;
    }

    // 通过排序后取最大 5 点均值和最小 5 点均值，较单纯 max-min 更抗孤立尖峰噪声。
    QVector<quint32> sorted = samples;
    std::sort(sorted.begin(), sorted.end());

    const int sampleCount = std::min(5,  static_cast<int>(sorted.size()));
    quint64 bottomSum = 0;
    quint64 topSum = 0;

    for (int i = 0; i < sampleCount; ++i) {
        bottomSum += sorted[i];
        topSum += sorted[sorted.size() - 1 - i];
    }

    const float bottomAvg = static_cast<float>(bottomSum) / sampleCount;
    const float topAvg = static_cast<float>(topSum) / sampleCount;
    const float rawVpp = topAvg - bottomAvg;

    // 转成物理量，单位与旧版 Python 实现保持一致。
    return rawVpp * kVppScale;
}
*/

// 取出指定通道的锁相包。channel 从 1 开始。
QVector<LockinChannelPacket> DeviceManager::takeLockinPackets(int channel, int maxCount)
{
    const int index = channel - 1;
    if (index < 0 || index >= static_cast<int>(m_lockinBuffers.size())) {
        return {};
    }
    return m_lockinBuffers[index]->take(maxCount);
}

QVector<RawAdcChannelPacket> DeviceManager::takeRawAdcPackets(int channel, int maxCount)
{
    const int index = channel - 1;
    if (index < 0 || index >= static_cast<int>(m_rawAdcBuffers.size())) {
        return {};
    }
    return m_rawAdcBuffers[index]->take(maxCount);
}

// 读取指定通道最新一帧锁相数据，不删除。适合 UI 定时器轮询。
bool DeviceManager::latestLockinPacket(int channel, LockinChannelPacket *out) const
{
    const int index = channel - 1;
    if (index < 0 || index >= static_cast<int>(m_lockinBuffers.size())) {
        return false;
    }
    return m_lockinBuffers[index]->latest(out);
}

bool DeviceManager::latestRawAdcPacket(int channel, RawAdcChannelPacket *out) const
{
    const int index = channel - 1;
    if (index < 0 || index >= static_cast<int>(m_rawAdcBuffers.size())) {
        return false;
    }
    return m_rawAdcBuffers[index]->latest(out);
}

// 指定通道缓冲池中未取走的帧数。
int DeviceManager::lockinPacketCount(int channel) const
{
    const int index = channel - 1;
    if (index < 0 || index >= static_cast<int>(m_lockinBuffers.size())) {
        return 0;
    }
    return m_lockinBuffers[index]->size();
}

int DeviceManager::rawAdcPacketCount(int channel) const
{
    const int index = channel - 1;
    if (index < 0 || index >= static_cast<int>(m_rawAdcBuffers.size())) {
        return 0;
    }
    return m_rawAdcBuffers[index]->size();
}

// 清空指定通道的缓冲池。调用期间不应与 push 并发。
void DeviceManager::clearLockinPackets(int channel)
{
    const int index = channel - 1;
    if (index < 0 || index >= static_cast<int>(m_lockinBuffers.size())) {
        return;
    }
    m_lockinBuffers[index]->clear();
}

void DeviceManager::clearRawAdcPackets(int channel)
{
    const int index = channel - 1;
    if (index < 0 || index >= static_cast<int>(m_rawAdcBuffers.size())) {
        return;
    }
    m_rawAdcBuffers[index]->clear();
}

// 清空全部通道的锁相/原始 ADC 缓冲池。
void DeviceManager::clearAllLockinPackets()
{
    for (auto &buf : m_lockinBuffers) {
        buf->clear();
    }
}

void DeviceManager::clearAllRawAdcPackets()
{
    for (auto &buf : m_rawAdcBuffers) {
        buf->clear();
    }
}
