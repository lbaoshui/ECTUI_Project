/******************************************************************************
 * Copyright 2022-xxxx xxx NanChang Hangkong University.
 * All right reserved. See COPYRIGHT for detailed Information.
 *
 * @file       XXX.h
 * @brief      探头数据 Function
 *
 * @author     June0821<1844426163@qq.com>
 * @date       2022/12/05
 * @history    9.06
 *****************************************************************************/
#ifndef PROBOBJECT_H
#define PROBOBJECT_H
#include "qcustomplot.h"

// 单个缺陷数据结构
struct defectInfo
{
    int TubeID = 0;
    QString position = 0;
    int amplitude = 0;
    float phase = 0;
    int defectType = -1;
};

// 混频参数存储结构
struct UserMixFreqParameter{
    double fAmplitude, fPhase = 0;
    double x = 0.0, y = 0.0;       // 最小二乘法计算出来的最小误差 (坐标点)

    double line_phase = 0, ratio_k = 0;       // 最小二乘法修正之后的计算因子

    double LSAmplitude = 1, LSRotateAngle = 0;   // 默认的缩放尺度和角度
    float fAmplitude_x = 1, fAmplitude_y = 1;
    int HPfunc = 1;
};
// 单频对象
class SingleFreObject
{
public:
    SingleFreObject();
    ~SingleFreObject();

private:
    struct SingleFrePara
    {
        float m_Gain = 25;              // 探头增益
        float m_GainRatio_y = 1;        // 探头增益比
        float m_Phase = 0;              // 相位
        UINT32 m_ProbFreq = 25000;      // 探头频率
        UINT32 m_LPF = 0;               // 低通滤波
        UINT32 m_HPF = 0;               // 高通滤波
        UINT8 m_ProbMatch = 0;          // 探头匹配
        UINT8 m_ProbDrive = 3;          // 探头驱动
    } m_SingleFrePara;              // 定义一个单探头参数结构体

public:
    // 设置、获取增益
    float getGain(){return m_SingleFrePara.m_Gain;}
    void setGain(float gain){ m_SingleFrePara.m_Gain = gain;}
    // 设置、获取增益比
    float getGainRatioY(){return m_SingleFrePara.m_GainRatio_y;}
    void setGainRatioY(float gainRatio){ m_SingleFrePara.m_GainRatio_y = gainRatio;}
    // 设置、获取相位
    void setPhase(float phase){ m_SingleFrePara.m_Phase = phase;}
    float getPhase(){ return m_SingleFrePara.m_Phase;}
    // 设置、获取探头匹配
    UINT8 getProbMatch(){ return m_SingleFrePara.m_ProbMatch;}
    void setProbMatch(UINT8 probMatch){ m_SingleFrePara.m_ProbMatch = probMatch;}
    // 设置、获取探头驱动
    UINT8 getProbDrive(){ return m_SingleFrePara.m_ProbDrive;}
    void setProbDrive(UINT8 probDrive){ m_SingleFrePara.m_ProbDrive = probDrive;}
    // 设置、获取探头频率
    UINT32 getProbFreq(){ return m_SingleFrePara.m_ProbFreq;}
    void setProbFreq(UINT32 probFreq){ m_SingleFrePara.m_ProbFreq = probFreq;}

public:
    //单通道数据接口
    QVector<QCPGraphData> *m_DataX = nullptr;              //时基图X轴显示数据
    QVector<QCPGraphData> *m_DataX2 = nullptr;
    QVector<QCPGraphData> *m_DataY = nullptr;              //时基图Y轴显示数据
    QVector<QCPGraphData> *m_DataY2 = nullptr;
    QVector<qint16> *m_signalProbX = nullptr;              //信号点x坐标
    QVector<qint16> *m_signalProbY = nullptr;              //信号点y坐标
    QVector<QCPCurveData> *m_impedanceData = nullptr;      //组坑图显示数据

    int TubeF1_Am_threshold = 500;                         // 用于判断缺陷的阈值设置
    int TubeMix_Am_threshold = 320;
    int TubeF1_phase_threshold = 90;

    //内部实现函数
    void AssignedMemoryForSingleFre(int a, int b, int c, int d, int e, int f, int g);                 //用于分配内存，参数列表为一个探头的各种数据
//    void determineImpendance(int PosOnAxisRect, QCustomPlot* impedanceCoordinate);   //用于确定在哪个窗口显示
};

// 单探头对象
class ProbObject{

private:
    bool m_HPflag = false;    //用于确定是否是混频模式

public:
    ProbObject();
    ~ProbObject();

public:
    // 单探头包含两个单频对象和一个混频对象指针
    SingleFreObject* SingleFreqObj_F1Channel = nullptr;
    SingleFreObject* SingleFreqObj_F2Channel = nullptr;
    SingleFreObject* SingleFreqObj_MixChannel = nullptr;
    void AssignedMemoryForProb();
    void removeF1();
    void removeF2();
    void removeMix();

    void setHPState(bool HPState);
    bool HPState();

    UserMixFreqParameter m_HPParameter;
};

#endif // PROBOBJECT_H
