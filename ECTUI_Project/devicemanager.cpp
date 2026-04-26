#include "devicemanager.h"

#include <QDataStream>
#include <QDebug>
#include <QMutexLocker>
#include <QtEndian>

#include <algorithm>

namespace {

// Python 版本中 Vpp 的换算系数为 1.03 * 0.122，这里原样保留。
// 如果后续硬件前端增益或标定方式变化，只需要修改这一处即可。
constexpr float kVppScale = 1.03f * 0.122f;

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

} // namespace

const QByteArray DeviceManager::DA_CONF_HEADER =
    QByteArray::fromHex("ccff55aa0001000010000000");
const QByteArray DeviceManager::AD_START_CMD =
    QByteArray::fromHex("a0ff55aa");
const QByteArray DeviceManager::AD_RATE_CMD =
    QByteArray::fromHex("20aa55aa");
const QByteArray DeviceManager::ADC_DATA_HEADER =
    QByteArray::fromHex("33dd33dd");
const QByteArray DeviceManager::ADC_DATA_LEN_TAG =
    QByteArray::fromHex("40400000");

DeviceManager::DeviceManager(QObject *parent)
    : QObject(parent),
      m_socket(new QTcpSocket(this)),
      m_connState(ConnectionState::Disconnected),
      m_adcData(ADC_CHANNELS)
{
    // 预初始化 16 个通道的数据容器，避免首次收包前上层读取到空结构。
    for (int channel = 0; channel < ADC_CHANNELS; ++channel) {
        m_adcData[channel].ch = channel + 1;
        m_adcData[channel].index = 1;
        m_adcData[channel].data.fill(0, ADC_SAMPLES_PER_CH);
    }

    // 注册元类型，保证自定义类型可经由 Qt 信号槽系统传递。
    qRegisterMetaType<ConnectionState>("ConnectionState");
    qRegisterMetaType<AdcChannelData>("AdcChannelData");
    qRegisterMetaType<QVector<AdcChannelData>>("QVector<AdcChannelData>");

    // 统一在 DeviceManager 内部处理 socket 生命周期与事件分发。
    connect(m_socket, &QTcpSocket::connected, this, &DeviceManager::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &DeviceManager::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &DeviceManager::onDataReceived);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_socket, &QTcpSocket::errorOccurred, this, &DeviceManager::onSocketError);
#else
    connect(m_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this,
            &DeviceManager::onSocketError);
#endif
}

DeviceManager::~DeviceManager()
{
    disconnectFromDevice();
}

void DeviceManager::connectToDevice(const QString &host, quint16 port)
{
    // 已经处于有效连接时直接返回，避免重复 connectToHost。
    if (m_connState == ConnectionState::Connected &&
        m_socket->state() == QAbstractSocket::ConnectedState) {
        return;
    }

    // 每次重新连接前清空旧接收缓冲，避免把上一次残留数据误判为新帧。
    m_receiveBuffer.clear();
    m_connState = ConnectionState::Connecting;
    emit connectionStateChanged(m_connState);

    // abort() 会立刻终止旧连接/旧连接尝试，适合“重新连接”场景。
    m_socket->abort();
    m_socket->connectToHost(host, port);
}

void DeviceManager::disconnectFromDevice()
{
    // 主动断开时也清掉缓冲，防止下次连接误用旧字节流。
    m_receiveBuffer.clear();

    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        if (m_connState != ConnectionState::Disconnected) {
            m_connState = ConnectionState::Disconnected;
            emit connectionStateChanged(m_connState);
        }
        return;
    }

    m_socket->disconnectFromHost();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->waitForDisconnected(1000);
    }
}

bool DeviceManager::sendDaConfig(const QVector<DaChannelConfig> &channels)
{
    // 先做连接态与参数完整性校验，再进入协议打包阶段。
    if (m_connState != ConnectionState::Connected) {
        emit errorOccurred(QStringLiteral("设备未连接，无法发送 DA 配置。"));
        return false;
    }

    if (channels.size() != DA_CHANNELS) {
        emit errorOccurred(QStringLiteral("DA 配置通道数必须为 16。"));
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

QVector<DaChannelConfig> DeviceManager::defaultDaConfig() const
{
    // 这里保留 Python da_ch_conf_ref 的 V2.0 默认映射顺序。
    // 注意：返回顺序不是简单的 ch=1..16，而是与旧硬件排布保持一致。
    return {
        {4, 1, 10000, 0,   60},
        {3, 1, 10000, 0,   60},
        {2, 1, 10000, 180, 60},
        {1, 1, 10000, 180, 60},
        {8, 1, 10000, 0,   60},
        {7, 1, 10000, 0,   60},
        {6, 1, 10000, 180, 60},
        {5, 1, 10000, 180, 60},
        {12, 1, 10000, 0,   60},
        {11, 1, 10000, 0,   60},
        {10, 1, 10000, 180, 60},
        {9, 1, 10000, 180, 60},
        {16, 1, 10000, 0,   60},
        {15, 1, 10000, 0,   60},
        {14, 1, 10000, 180, 60},
        {13, 1, 10000, 180, 60}
    };
}

bool DeviceManager::sendSampleRateConfig(SampleRate rate)
{
    // 采样率被收敛为枚举，避免外部传入协议不支持的值。
    if (m_connState != ConnectionState::Connected) {
        emit errorOccurred(QStringLiteral("设备未连接，无法发送采样率配置。"));
        return false;
    }

    const QByteArray frame = buildSampleRateFrame(rate);
    const qint64 written = m_socket->write(frame);
    if (written != frame.size()) {
        emit errorOccurred(QStringLiteral("采样率配置发送失败。"));
        return false;
    }

    return m_socket->waitForBytesWritten(1000);
}

bool DeviceManager::startSampling()
{
    // 启动命令应在完成连接、DA 参数设置、AD 采样率设置之后再调用。
    if (m_connState != ConnectionState::Connected) {
        emit errorOccurred(QStringLiteral("设备未连接，无法启动采样。"));
        return false;
    }

    const QByteArray frame = buildStartSampleFrame();
    const qint64 written = m_socket->write(frame);
    if (written != frame.size()) {
        emit errorOccurred(QStringLiteral("启动采样命令发送失败。"));
        return false;
    }

    return m_socket->waitForBytesWritten(1000);
}

QVector<AdcChannelData> DeviceManager::getAdcData() const
{
    QMutexLocker locker(&m_dataMutex);
    return m_adcData;
}

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

void DeviceManager::onConnected()
{
    // 只维护状态并发信号，把 UI 表现留给上层。
    m_connState = ConnectionState::Connected;
    emit connectionStateChanged(m_connState);
}

void DeviceManager::onDisconnected()
{
    m_connState = ConnectionState::Disconnected;
    emit connectionStateChanged(m_connState);
}

void DeviceManager::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    // 一旦 socket 出错，统一回到断开态，避免上层继续认为连接可用。
    m_connState = ConnectionState::Disconnected;
    emit connectionStateChanged(m_connState);
    emit errorOccurred(m_socket->errorString());
}

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

QByteArray DeviceManager::buildDaFrame(const QVector<DaChannelConfig> &channels) const
{
    // DA 帧结构：
    // 12 字节固定头 + 16 个通道 * 4 个 uint32(index/freq/phase/amp)
    QByteArray frame = DA_CONF_HEADER;
    frame.reserve(DA_CONF_HEADER.size() + DA_CHANNELS * 16);

    for (const DaChannelConfig &channel : channels) {
        // amp 按旧协议强制夹紧到 [0, 60]，避免下位机收到非法参数。
        const int clampedAmp = std::clamp(channel.amp, 0, DA_MAX_AMP);
        appendUInt32LE(frame, static_cast<quint32>(channel.index));
        appendUInt32LE(frame, static_cast<quint32>(channel.freq));
        appendUInt32LE(frame, static_cast<quint32>(channel.phase));
        appendUInt32LE(frame, static_cast<quint32>(clampedAmp));
    }

    return frame;
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

float DeviceManager::computeChannelVpp(const QVector<quint16> &samples) const
{
    if (samples.isEmpty()) {
        return 0.0f;
    }

    // 通过排序后取最大 5 点均值和最小 5 点均值，较单纯 max-min 更抗孤立尖峰噪声。
    QVector<quint16> sorted = samples;
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
