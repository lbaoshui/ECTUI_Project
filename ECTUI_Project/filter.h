/*
 * @Descripttion: 数字滤波模块 —— 支持低通/高通双二阶滤波
 * @version: 1.0.0
 *
 * ==== 滤波器类型 ====
 * 二阶 IIR 滤波器，采用双线性变换法从模拟 Butterworth 原型设计。
 * 默认 Q = 0.7071 对应 Butterworth 最平坦响应，Q > 0.7071 则通带边缘有峰值，
 * Q < 0.7071 则过渡带更平缓。
 *
 * ==== 双二阶 (Biquad) 传递函数 ====
 *
 *        b0 + b1·z⁻¹ + b2·z⁻²
 * H(z) = ──────────────────────
 *        1  + a1·z⁻¹ + a2·z⁻²
 *
 * 分子和分母各是 z⁻¹ 的二次多项式，故名"双二次"(bi-quadratic)。
 * 一个 biquad 就是一个二阶滤波器单元，高阶滤波器可通过多个 biquad 级联实现。
 *
 * ==== 直接 I 型 (Direct Form I) 实现 ====
 *
 * 将 H(z) 展开为差分方程：
 *
 * y[n] = b0·x[n] + b1·x[n-1] + b2·x[n-2] - a1·y[n-1] - a2·y[n-2]
 *
 * 直接按此方程计算，存储 4 个历史值（x[n-1], x[n-2], y[n-1], y[n-2]），
 * 故称"直接 I 型"。相比直接 II 型（仅 2 个延迟单元），直接 I 型数值稳定性更好。
 * 每个样本仅需 5 次乘法 + 4 次加法，适合实时采集。
 *
 * ==== 双线性变换 (Bilinear Transform) ====
 *
 * 将模拟域 s 映射到数字域 z：
 *
 *        2   1 - z⁻¹
 * s  =  ── · ────────     (Ts 为采样周期)
 *       Ts   1 + z⁻¹
 *
 * 变换步骤：
 * 1. 预畸变：omega = 2π·fc/fs，补偿频率轴的非线性压缩
 * 2. 代入模拟 Butterworth 低通原型 H(s) = 1/(s² + s/Q + 1)
 * 3. 整理为 biquad 标准形式，提取系数 b0,b1,b2,a1,a2
 *
 * ==== 滤波链 (FilterChain) ====
 *
 * 多个 BiquadFilter 首尾串联，上一级的输出作为下一级的输入。
 * 典型用法：HP(100Hz) + LP(5000Hz) = 带通 100~5000Hz。
 * 级联后整体为 4 阶滤波器（每级 2 阶），滚降更陡。
 *
 * ==== 使用注意 ====
 * - 截止频率需满足 fc < fs/2（奈奎斯特），代码内部自动 clamp 到 0.49·fs
 * - 修改参数或切换滤波类型时会自动 reset() 清空历史值，避免暂态跳变
 * - 滤波仅作用于曲线显示数据，Probe 中保存的原始数据不受影响
 */
#ifndef FILTER_H
#define FILTER_H

#include <QVector>

enum class FilterType {
    LowPass,   // 低通：保留低于 fc 的频率分量
    HighPass   // 高通：保留高于 fc 的频率分量
};

// 抽象基类，定义滤波器的统一接口。
// 派生类只需实现 process() 和 reset()，即可插入 FilterChain 中串联使用。
class IFilter {
public:
    virtual ~IFilter() = default;

    // 处理单个样本，返回滤波后的值
    virtual float process(float input) = 0;

    // 批量处理，默认实现逐样本调用 process()
    virtual void processBlock(const QVector<float> &input, QVector<float> &output);

    // 清空内部延迟单元，恢复到零初始状态
    virtual void reset() = 0;
};

// 双二阶滤波器 —— 滤波模块的核心单元。
// 每个实例是一个独立的二阶滤波器，拥有自己的系数和延迟单元，
// 多个实例可通过 FilterChain 级联构成高阶滤波器。
class BiquadFilter : public IFilter {
public:
    BiquadFilter();

    // 构造时立即配置并计算系数
    BiquadFilter(FilterType type, float cutoffHz, float sampleRateHz, float q = 0.7071f);

    // 逐样本滤波 —— 直接 I 型差分方程
    float process(float input) override;

    // 将 4 个延迟单元 (x1,x2,y1,y2) 全部清零
    void reset() override;

    // 重新配置滤波器（改变类型/截止频率/Q值），会触发系数重算和状态清零
    void configure(FilterType type, float cutoffHz, float sampleRateHz, float q = 0.7071f);

    // 仅修改截止频率，保持类型和 Q 不变
    void setCutoff(float cutoffHz);

    FilterType type() const { return m_type; }
    float cutoffHz() const { return m_cutoffHz; }
    float sampleRateHz() const { return m_sampleRateHz; }

private:
    // 根据 type/fc/fs/q 重新计算 biquad 系数
    void updateCoefficients();

    FilterType m_type = FilterType::LowPass;
    float m_cutoffHz = 1000.0f;       // 截止频率 (Hz)
    float m_sampleRateHz = 100000.0f; // 采样率 (Hz)
    float m_q = 0.7071f;              // 品质因数

    // 归一化后的滤波器系数（a0 已归一化为 1，不需要存储）
    float m_b0 = 1.0f, m_b1 = 0.0f, m_b2 = 0.0f;
    float m_a1 = 0.0f, m_a2 = 0.0f;

    // 延迟单元：分别存储输入/输出的历史值
    float m_x1 = 0.0f, m_x2 = 0.0f;  // x[n-1], x[n-2]
    float m_y1 = 0.0f, m_y2 = 0.0f;  // y[n-1], y[n-2]
};

// 滤波链：将多个 BiquadFilter 首尾串联。
// addStage() 按添加顺序级联，process() 将数据依次穿过每一级。
// 链为空时 process() 不会被调用，相当于直通（不过滤）。
class FilterChain : public IFilter {
public:
    // 串联滤波：input → stage[0] → stage[1] → ... → 返回最终结果
    float process(float input) override;

    // 重置链中所有级的延迟单元
    void reset() override;

    // 在链末尾追加一级滤波器
    void addStage(FilterType type, float cutoffHz, float sampleRateHz, float q = 0.7071f);

    // 清空全部级，恢复直通状态
    void clear();

    int stageCount() const { return m_stages.size(); }
    bool isEmpty() const { return m_stages.isEmpty(); }

private:
    QVector<BiquadFilter> m_stages;  // 按添加顺序排列的滤波器级
};

#endif // FILTER_H
