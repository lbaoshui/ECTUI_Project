#ifndef SAVEDATA_H
#define SAVEDATA_H

#include <QObject>
#include <QTimer>
#include <QRunnable>
#include <QThreadPool>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include "probobject.h"

// 此对象用于接收文件名保存监测数据

// 此类使用定时器方式
class SaveData : public QObject
{
    Q_OBJECT
public:
    explicit SaveData(QObject *parent = nullptr);
    ~SaveData();

private:
    QVector<qint16>* F1signalData1 = nullptr;
    QVector<qint16>* F1signalData2 = nullptr;
    QVector<qint16>* F2signalData1 = nullptr;
    QVector<qint16>* F2signalData2 = nullptr;
    QVector<qint16>* MixsignalData1 = nullptr;
    QVector<qint16>* MixsignalData2 = nullptr;

    QTimer* saveDatatimer = nullptr;

    // 定义变量用于接收文件地址和文件名
    QString DataFolder;
    QString DataFileName_prob1;
    QString DataFileName_prob2;
    int state = 4;    // 用于确定目前处于什么检测状态（如：单探头双频，双探头双频等...）

private:
    void getSignalData_Prob1(QVector<qint16>* m_F1signalData1, QVector<qint16>* m_F1signalData2, QVector<qint16>* m_F2signalData1,QVector<qint16>* m_F2signalData2, QVector<qint16>* m_MixsignalData1, QVector<qint16>* m_MixsignalData2);
    // 用于从接收文件名初始化文件地址和文件名
    void getFileName(QString& m_DataFolder, QString& m_DataFileName_prob1, QString& m_DataFileName_prob2, int m_state);
    // 保存数据
    void saveDateFile();

signals:

};

// 此类使用线程池方式
class SaveData1 : public QRunnable
{
public:
    explicit SaveData1();
    ~SaveData1();
    void getSignalData_Prob(ProbObject*& m_prob1, ProbObject*& m_prob2);
    void getSignalData_Prob1(QVector<qint16>*& m_F1signalData1, QVector<qint16>*& m_F1signalData2, QVector<qint16>*& m_F2signalData1,QVector<qint16>*& m_F2signalData2, QVector<qint16>*& m_MixsignalData1, QVector<qint16>*& m_MixsignalData2);
    void getFileName(QString& m_DataFolder, QString& m_DataFileName_prob1, QString& m_DataFileName_prob2, int m_state);   // 获取文件名和地址目录
    void run() override;

private:
    QVector<qint16>* F1signalData1 = nullptr;
    QVector<qint16>* F1signalData2 = nullptr;
    QVector<qint16>* F2signalData1 = nullptr;
    QVector<qint16>* F2signalData2 = nullptr;
    QVector<qint16>* MixsignalData1 = nullptr;
    QVector<qint16>* MixsignalData2 = nullptr;

    QVector<qint16>* prob2_F1signalData1 = nullptr;
    QVector<qint16>* prob2_F1signalData2 = nullptr;
    QVector<qint16>* prob2_F2signalData1 = nullptr;
    QVector<qint16>* prob2_F2signalData2 = nullptr;
    QVector<qint16>* prob2_MixsignalData1 = nullptr;
    QVector<qint16>* prob2_MixsignalData2 = nullptr;

    // 定义变量用于接收文件地址和文件名
    QString DataFolder;
    QString DataFileName_prob1;
    QString DataFileName_prob2;
    int state = 4;    // 用于确定目前处于什么检测状态（如：单探头双频，双探头双频等...）
};

#endif // SAVEDATA_H
