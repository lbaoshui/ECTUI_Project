/*
 * @Descripttion: 数字滤波模块实现
 * @version: 1.0.0
 */
#include "filter.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ═══════════════════════════════════════════════
//  IFilter —— 默认实现
// ═══════════════════════════════════════════════

void IFilter::processBlock(const QVector<float> &input, QVector<float> &output)
{
    const int n = input.size();
    output.resize(n);
    for (int i = 0; i < n; ++i) {
        output[i] = process(input[i]);
    }
}

// ═══════════════════════════════════════════════
//  BiquadFilter —— 双二阶滤波器
// ═══════════════════════════════════════════════

BiquadFilter::BiquadFilter() = default;

BiquadFilter::BiquadFilter(FilterType type, float cutoffHz, float sampleRateHz, float q)
{
    configure(type, cutoffHz, sampleRateHz, q);
}

// 重新配置滤波器参数。
// 修改参数后必须 reset() 清空延迟单元，否则新旧数据混叠会在输出端产生暂态跳变。
void BiquadFilter::configure(FilterType type, float cutoffHz, float sampleRateHz, float q)
{
    m_type = type;
    m_cutoffHz = cutoffHz;
    m_sampleRateHz = sampleRateHz;
    m_q = q;
    reset();
    updateCoefficients();
}

// 运行时修改截止频率（类型和 Q 保持不变）。
// 同样需要 reset() 避免历史数据与新系数不匹配。
void BiquadFilter::setCutoff(float cutoffHz)
{
    m_cutoffHz = cutoffHz;
    reset();
    updateCoefficients();
}

// 将 4 个延迟单元全部清零，恢复到零初始状态。
// 调用时机：配置变更后、滤波开始前、或需要清除历史影响时。
void BiquadFilter::reset()
{
    m_x1 = 0.0f;
    m_x2 = 0.0f;
    m_y1 = 0.0f;
    m_y2 = 0.0f;
}

// ════ 直接 I 型差分方程 ════
//
// 传递函数:
//               b0 + b1·z⁻¹ + b2·z⁻²
//     H(z) = ──────────────────────
//               1 + a1·z⁻¹ + a2·z⁻²
//
// 展开为差分方程:
//     y[n] = b0·x[n] + b1·x[n-1] + b2·x[n-2]
//          - a1·y[n-1] - a2·y[n-2]
//
// x[n] = 当前输入, x[n-1]=m_x1, x[n-2]=m_x2
// y[n] = 当前输出, y[n-1]=m_y1, y[n-2]=m_y2
//
// 每次 process() 调用:
//   1. 用当前输入 + 4 个历史值计算输出 y
//   2. 将历史值向后平移一格（x2←x1, x1←input, y2←y1, y1←y）
//
// 计算量: 5 次乘法 + 4 次加法，适合高频实时采集。
float BiquadFilter::process(float input)
{
    const float y = m_b0 * input + m_b1 * m_x1 + m_b2 * m_x2
                  - m_a1 * m_y1 - m_a2 * m_y2;

    // 历史值平移，为下一个样本做准备
    m_x2 = m_x1;
    m_x1 = input;
    m_y2 = m_y1;
    m_y1 = y;

    return y;
}

// ════ 双线性变换法计算 biquad 系数 ════
//
// 设计流程:
//   1. 预畸变   omega = 2π·fc/fs
//      补偿双线性变换造成的频率轴非线性压缩，使数字滤波器的
//      实际截止频率与指定的 fc 一致。
//
//   2. 计算 alpha = sin(omega) / (2·Q)
//      Q = 品质因数，控制通带形状:
//        Q = 0.7071  → Butterworth (最平坦，无过冲)
//        Q > 0.7071  → 通带边缘隆起 (Chebyshev 特性)
//        Q < 0.7071  → 过渡带变宽，无过冲
//
//   3. 代入模拟 Butterworth 原型，经双线性变换后整理得:
//
//      低通:
//        b0 = (1 - cosω) / 2        b1 = 1 - cosω       b2 = (1 - cosω) / 2
//        a0 = 1 + α                 a1 = -2·cosω        a2 = 1 - α
//
//      高通:
//        b0 = (1 + cosω) / 2        b1 = -(1 + cosω)    b2 = (1 + cosω) / 2
//        a0 = 1 + α                 a1 = -2·cosω        a2 = 1 - α
//
//      注意高低通的 a1, a2 相同，仅分子 b0,b1,b2 不同。
//
//   4. 用 a0 归一化所有系数（使分母常数项 = 1）:
//        b0/=a0, b1/=a0, b2/=a0, a1/=a0, a2/=a0
//
//   fc 自动 clamp 到 [0.001, 0.49·fs]，避免 omega 趋近 π 时
//   cosω→-1 导致系数发散。
void BiquadFilter::updateCoefficients()
{
    // clamp: 下限 0.001Hz 防止数值下溢，上限 0.49·fs 防止接近奈奎斯特频率
    const float fc = std::clamp(m_cutoffHz, 0.001f, m_sampleRateHz * 0.49f);
    const float omega = 2.0f * static_cast<float>(M_PI) * fc / m_sampleRateHz;
    const float sn = std::sin(omega);
    const float cs = std::cos(omega);
    const float alpha = sn / (2.0f * m_q);

    float b0, b1, b2, a0, a1, a2;

    if (m_type == FilterType::LowPass) {
        // 低通: 0 ~ fc 保留，fc 以上衰减
        b0 = (1.0f - cs) / 2.0f;
        b1 = 1.0f - cs;
        b2 = (1.0f - cs) / 2.0f;
    } else {
        // 高通: fc 以上保留，0 ~ fc 衰减
        b0 = (1.0f + cs) / 2.0f;
        b1 = -(1.0f + cs);
        b2 = (1.0f + cs) / 2.0f;
    }

    a0 = 1.0f + alpha;
    a1 = -2.0f * cs;
    a2 = 1.0f - alpha;

    // 用 a0 归一化，使标准形式的传递函数分母常数项 = 1
    m_b0 = b0 / a0;
    m_b1 = b1 / a0;
    m_b2 = b2 / a0;
    m_a1 = a1 / a0;
    m_a2 = a2 / a0;
}

// ═══════════════════════════════════════════════
//  FilterChain —— 滤波链（多级串联）
// ═══════════════════════════════════════════════

// 串联滤波：数据依次穿过链中的每一级 BiquadFilter。
//
// 级联示例 —— 构建带通 (100Hz ~ 5000Hz):
//   chain.addStage(HighPass, 100,  100000);   // 第1级: 滤除 <100Hz 低频漂移
//   chain.addStage(LowPass,  5000, 100000);   // 第2级: 滤除 >5000Hz 高频噪声
//
// 数据流: input → HP(100Hz) → LP(5000Hz) → output
//
// 注意：级联后整体阶数 = 各级阶数之和（每级 2 阶）。
// 两阶级联 = 4 阶，滚降速率加倍（低通每倍频程 -24dB）。
float FilterChain::process(float input)
{
    float y = input;
    for (auto &stage : m_stages) {
        y = stage.process(y);
    }
    return y;
}

void FilterChain::reset()
{
    for (auto &stage : m_stages) {
        stage.reset();
    }
}

void FilterChain::addStage(FilterType type, float cutoffHz, float sampleRateHz, float q)
{
    m_stages.append(BiquadFilter(type, cutoffHz, sampleRateHz, q));
}

void FilterChain::clear()
{
    m_stages.clear();
}
