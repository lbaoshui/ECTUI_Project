/*
 * @file phaserotator.h
 * @brief 幅值-相位二维旋转工具
 *
 * 将 (ampMv, phaseDeg) 数据点视为平面坐标，绕原点做几何旋转。
 * 旋转角度为 degrees，内部换算为弧度后应用二维旋转矩阵：
 *
 *   x' = x * cosθ - y * sinθ    (x = ampMv)
 *   y' = x * sinθ + y * cosθ    (y = phaseDeg)
 *
 * 推荐用法：保留原始 (amp, phase)，每次角度变化时调用 rotateAmpPhase() 重算显示缓冲。
 * 保存数据时保留原始值，旋转角度作为元数据。
 *
 * @code
 * // 追加原始数据
 * m_originalAmp.append(packet.ampMv);
 * m_originalPhase.append(packet.phaseDeg);
 *
 * // 改变旋转角 → 重算
 * auto result = rotateAmpPhase(m_originalAmp, m_originalPhase, 30.0f);
 * // result.ampMv, result.phaseDeg 用于绘图
 * @endcode
 */
#ifndef PHASEROTATOR_H
#define PHASEROTATOR_H

#include <QVector>
#include <cmath>

/** 旋转结果：包含旋转后的幅值和相位 */
struct RotatedAmpPhase {
    QVector<float> ampMv;
    QVector<float> phaseDeg;
};

/**
 * @brief 对幅值-相位数据做二维旋转（不修改原数据）
 * @param ampMv    原始幅值（mV），作为 X 坐标
 * @param phaseDeg 原始相位（度），作为 Y 坐标
 * @param angleDeg 旋转角度（度）
 * @return 旋转后的 {ampMv, phaseDeg}
 *
 * 两个输入向量长度可以不同，取较小值处理。
 */
inline RotatedAmpPhase rotateAmpPhase(const QVector<float> &ampMv,
                                      const QVector<float> &phaseDeg,
                                      float angleDeg)
{
    RotatedAmpPhase result;
    const int n = qMin(ampMv.size(), phaseDeg.size());   // 这里主要是为了保证两个向量长度相同，取较小值，避免长度不一致导致的bug，不过一般不会有问题
    if (n <= 0) {
        return result;
    }

    result.ampMv.reserve(n);
    result.phaseDeg.reserve(n);

    if (std::fabs(angleDeg) < 1e-6f) {
        result.ampMv = ampMv;
        result.phaseDeg = phaseDeg;
        return result;
    }

    const float rad = angleDeg * static_cast<float>(M_PI) / 180.0f;
    const float cosA = std::cos(rad);
    const float sinA = std::sin(rad);

    for (int i = 0; i < n; ++i) {
        const float x = ampMv[i];
        const float y = phaseDeg[i];
        result.ampMv.append(x * cosA - y * sinA);
        result.phaseDeg.append(x * sinA + y * cosA);
    }

    return result;
}

/**
 * @brief 就地旋转，直接修改传入的向量（节省内存）
 *
 * 适合增量旋转场景：delta = newAngle - lastAngle; rotateAmpPhaseInPlace(bufAmp, bufPhase, delta);
 */
inline void rotateAmpPhaseInPlace(QVector<float> &ampMv,
                                  QVector<float> &phaseDeg,
                                  float angleDeg)
{
    const int n = qMin(ampMv.size(), phaseDeg.size());
    if (n <= 0 || std::fabs(angleDeg) < 1e-6f) {
        return;
    }

    const float rad = angleDeg * static_cast<float>(M_PI) / 180.0f;
    const float cosA = std::cos(rad);
    const float sinA = std::sin(rad);

    for (int i = 0; i < n; ++i) {
        const float x = ampMv[i];
        const float y = phaseDeg[i];
        ampMv[i]   = x * cosA - y * sinA;
        phaseDeg[i] = x * sinA + y * cosA;
    }
}

#endif // PHASEROTATOR_H
