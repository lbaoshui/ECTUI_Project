#include "probobject.h"
// 构造函数
SingleFreObject::SingleFreObject()
{

}

// 析构函数
SingleFreObject::~SingleFreObject()
{
    if(nullptr != m_DataX)
    {
        delete m_DataX;
        delete m_DataX2;
        delete m_DataY;
        delete m_DataY2;
        delete m_signalProbX;
        delete m_signalProbY;
        delete m_impedanceData;
//        qDebug() << "memory has been deleted!";
    }
//    qDebug() << "SinglePreObject has delete!";
}

// 用于给单频对象进行分配数据，参数为分配的数据大小
void SingleFreObject::AssignedMemoryForSingleFre(int a, int b, int c, int d, int e, int f, int g)
{
    m_DataX = new QVector<QCPGraphData>(a);
    if (m_DataX == nullptr)
    {
        qDebug() << "alloc failed";
        return ;
    }
    m_DataX2 = new QVector<QCPGraphData>(b);
    if (m_DataX2 == nullptr)
    {
        qDebug() << "alloc failed";
        return ;
    }
    m_DataY = new QVector<QCPGraphData>(c);
    if (m_DataY == nullptr)
    {
        qDebug() << "alloc failed";
        return ;
    }
    m_DataY2 = new QVector<QCPGraphData>(d);
    if (m_DataY2 == nullptr)
    {
        qDebug() << "alloc failed";
        return ;
    }
    m_signalProbX = new QVector<qint16>(e);
    if (m_signalProbX == nullptr)
    {
        qDebug() << "alloc failed";
        return ;
    }
    m_signalProbY = new QVector<qint16>(f);
    if (m_signalProbY == nullptr)
    {
        qDebug() << "alloc failed";
        return ;
    }
    m_impedanceData = new QVector<QCPCurveData>(g);
    if (m_impedanceData == nullptr)
    {
        qDebug() << "alloc failed";
        return ;
    }
}

// 构造函数
ProbObject::ProbObject()
{

}

// 析构函数
ProbObject::~ProbObject()
{
    if(nullptr != SingleFreqObj_F1Channel)
    {
        delete SingleFreqObj_F1Channel;
        delete SingleFreqObj_F2Channel;
        delete SingleFreqObj_MixChannel;
    }
//    qDebug() << "ProbObject has delete!";
}
// 为探头对象分配对象
void ProbObject::AssignedMemoryForProb()
{
    if(nullptr == SingleFreqObj_F1Channel)
    {
        SingleFreqObj_F1Channel = new SingleFreObject();
        SingleFreqObj_F2Channel = new SingleFreObject();
        SingleFreqObj_MixChannel = new SingleFreObject();
    }
}

// 移除一路数据对象内存
void ProbObject::removeF1()
{
    if(SingleFreqObj_F1Channel != nullptr)
        delete SingleFreqObj_F1Channel;
}
void ProbObject::removeF2()
{
    if(SingleFreqObj_F2Channel != nullptr)
        delete SingleFreqObj_F2Channel;
}
void ProbObject::removeMix()
{
    if(SingleFreqObj_MixChannel != nullptr)
        delete SingleFreqObj_MixChannel;
}

// 设置混频状态
void ProbObject::setHPState(bool HPState)
{
    m_HPflag = HPState;
}

// 获取混频状态
bool ProbObject::HPState()
{
    return m_HPflag;
}
