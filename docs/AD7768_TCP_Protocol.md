# AD7768 TCP 传输协议

本文档用于上位机软件对接当前 AD7768 数字锁相工程。PC 上位机作为 TCP Server，默认监听 `8080`；Zynq PS 作为 TCP Client 主动连接。所有多字节字段均为小端格式。

TCP 是字节流，不保证一次 `recv` 收到一个完整包。上位机必须按包头和 `payload_length` 做缓存、拆包、粘包处理。

## 1. PS 到上位机：上传包通用头

每个上传包均由 16 字节包头和 payload 组成：

| Offset | 类型 | 字段 | 说明 |
| --- | --- | --- | --- |
| 0 | `uint32` | `id0` | 固定 `0xAA55AA55` |
| 4 | `uint32` | `id1` | 包类型，见下表 |
| 8 | `uint32` | `packet_index` | 上传包序号，递增，用于判断丢包 |
| 12 | `uint32` | `payload_length` | payload 字节数，不包含 16 字节包头 |

包类型：

| `id1` | 类型 |
| --- | --- |
| `0xAA55AA55` | 锁相结果包 |
| `0xAA55CC33` | 原始 ADC 波形包 |

当前 PS 代码按“锁相包、原始包、锁相包、原始包...”交替上传。上位机不要只按奇偶判断类型，应以 `id1` 为准。

## 2. 锁相结果包

锁相包 `id1 = 0xAA55AA55`。

payload 由若干个锁相结果帧组成：

```text
payload_length = frame_count * 64
```

每帧 64 字节，包含 8 个通道。通道顺序为 `CH1, CH2, ... CH8`，对应代码中的 `ch0, ch1, ... ch7`。

每个通道占 8 字节：

| 字段 | 类型 | 换算 |
| --- | --- | --- |
| `amp_mV_x100` | `int32` | `amp_mV = amp_mV_x100 / 100.0` |
| `phase_deg_x100` | `int32` | `phase_deg = phase_deg_x100 / 100.0` |

单帧 payload 布局：

```text
CH1 amp, CH1 phase,
CH2 amp, CH2 phase,
...
CH8 amp, CH8 phase
```

示例：若某帧 CH8 的两个 `int32` 为 `9114` 和 `-5116`，则 CH8 幅值为 `91.14 mV`，相位为 `-51.16 deg`。若显示峰峰值，当前上位机默认按 `Vpp = amp_mV * 2.0` 计算。

相位差由上位机计算。选择两个通道 A、B 后：

```text
phase_diff = phase_A - phase_B
```

计算后应归一化到 `(-180, 180]` 度。例如 `170 - (-170) = 340`，归一化后显示 `-20 deg`。

## 3. 原始 ADC 波形包

原始包 `id1 = 0xAA55CC33`。

payload 由若干个原始采样点组成：

```text
payload_length = sample_count * 32
```

每个采样点 32 字节，包含 8 个通道。通道顺序为 `CH1..CH8`。

每个通道占 4 字节：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `adc_code` | `int32` | 24-bit ADC 原始码值符号扩展到 32-bit |

单个采样点 payload 布局：

```text
sample0_CH1, sample0_CH2, ... sample0_CH8,
sample1_CH1, sample1_CH2, ... sample1_CH8,
...
```

示例：若原始包 `payload_length = 32768`，则包含 `32768 / 32 = 1024` 个采样点，每个采样点有 8 个通道。

当前上位机把原始码值换算为 mV：

```text
voltage_mV = adc_code * 4096.0 / 8388607.0
```

其中 `4096.0` 表示当前软件使用的满量程参考，若硬件前端量程不同，应在上位机中同步修改。

## 4. 上位机到 PS：下发命令

命令与数据上传共用同一 TCP 连接，由 PC 发给 PS。

4 字节命令：

| 命令 | `uint32` |
| --- | --- |
| 开始采集 | `0xAA55FFA0` |
| 停止采集 | `0xAA55FFB1` |

16 字节设置命令：

| Offset | 类型 | 说明 |
| --- | --- | --- |
| 0 | `uint32` | 命令字 |
| 4 | `uint32` | 保留，填 0 |
| 8 | `uint32` | 保留，填 0 |
| 12 | `uint32` | 参数值 |

| 功能 | 命令字 | 参数 |
| --- | --- | --- |
| 设置帧长 | `0xAA55FFC1` | `frame_count`，建议 `1..1024` |
| 设置 DDS fword | `0xAA55FFD2` | 32-bit DDS 频率控制字 |
| 设置 DDS phase | `0xAA55FFD3` | 32-bit DDS 初始相位字 |
| 设置 DDS 频率 Hz | `0xAA55FFD4` | 频率，单位 Hz，由 PS 换算 fword |

当前 WPF 上位机界面只输入 DDS 频率 Hz，并按下式换算后发送 `0xAA55FFD2`：

```text
fword = round(freq_hz * 2^32 / 50000000.0)
```

例如 DDS 时钟为 `50 MHz`、目标频率 `50 Hz`：

```text
fword = round(50 * 4294967296 / 50000000) = 4295 = 0x000010C7
```

## 5. 解析建议

1. 在接收缓存中查找 `id0 = 0xAA55AA55`。
2. 读取 16 字节包头，根据 `id1` 判断包类型。
3. 等待 `16 + payload_length` 字节全部到达后再解析。
4. 校验长度：锁相包 `payload_length % 64 == 0`，原始包 `payload_length % 32 == 0`。
5. 使用 `packet_index` 检测是否跳号；跳号说明可能丢包或软件处理不及时。
