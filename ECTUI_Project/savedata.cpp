#include "savedata.h"

SaveData::SaveData(QObject *parent)
    : QObject{parent}
{
    // 方式一：使用定时器方式定时检测数据是否达到保存要求
    saveDatatimer = new QTimer(this);
    connect(saveDatatimer, &QTimer::timeout, this, &SaveData::saveDateFile);
    saveDatatimer->start(100);
    // 方式二：通过主线程定时器给子线程发送信号

    // 方式三：在主线程中检测是否达到数据保存条件，然后创建一个任务线程来保存数据
}

SaveData::~SaveData()
{
    if (saveDatatimer != nullptr) {
        if (saveDatatimer->isActive()) {
            saveDatatimer->stop();
            delete saveDatatimer;
            saveDatatimer = nullptr;
        }
    }
}

// 获取信号数据
void SaveData::getSignalData_Prob1(QVector<qint16> *m_F1signalData1, QVector<qint16> *m_F1signalData2, QVector<qint16> *m_F2signalData1, QVector<qint16> *m_F2signalData2, QVector<qint16> *m_MixsignalData1, QVector<qint16> *m_MixsignalData2)
{
    F1signalData1 = m_F1signalData1;
    F1signalData2 = m_F1signalData2;
    F2signalData1 = m_F2signalData1;
    F2signalData2 = m_F2signalData2;
    MixsignalData1 = m_MixsignalData1;
    MixsignalData2 = m_MixsignalData2;
}

 // 用于从接收文件名初始化文件地址和文件名
void SaveData::getFileName(QString &m_DataFolder, QString &m_DataFileName_prob1, QString &m_DataFileName_prob2, int m_state)
{
    DataFolder = m_DataFolder;
    DataFileName_prob1 = m_DataFileName_prob1;
    DataFileName_prob2 = m_DataFileName_prob2;
    state = m_state;
}

void SaveData::saveDateFile()
{
    saveDatatimer->stop();   //
    this->deleteLater();

    std::chrono::steady_clock::time_point time1 = std::chrono::steady_clock::now();
    QString fileName1 = DataFolder + DataFileName_prob1;
    QString fileName2 = DataFolder + DataFileName_prob2;
    QFile file1(fileName1);
    QFile file2(fileName2);
    QTextStream in1(&file1);
    QTextStream in2(&file2);

    if (file1.open(QIODevice::ReadWrite|QIODevice::Truncate)) {
        qDebug() << "file1 has been created";
        for (int i = 0; i < F1signalData1->size(); i++) {
            in1 << F1signalData1->at(i) << "," << F1signalData2->at(i) << "," << F2signalData1->at(i) << "," << F2signalData2->at(i) << "," << MixsignalData1->at(i) << "," << MixsignalData1->at(i) << "\n";
        }
        file1.close();
    } else {
        qDebug() << "file1 has failed to create";
    }

    if (file2.open(QIODevice::ReadWrite|QIODevice::Truncate)) {
        qDebug() << "file2 has been created";
        for (int i = 0; i < F1signalData1->size(); i++) {
            in2 << F1signalData1->at(i) << "," << F1signalData2->at(i) << "," << F2signalData1->at(i) << "," << F2signalData2->at(i) << "," << MixsignalData1->at(i) << "," << MixsignalData1->at(i) << "\n";
        }
        file2.close();
    } else {
        qDebug() << "file2 has failed to create";
    }
    std::chrono::steady_clock::time_point time2 = std::chrono::steady_clock::now();
    int total_time = std::chrono::duration_cast<std::chrono::milliseconds>(time2 - time1).count();
    qDebug() << "saveData took time: " << total_time;

    qDebug() << "this thread has been delete";
}


// 此方式需要使用线程池
SaveData1::SaveData1()
{

}

SaveData1::~SaveData1()
{
    F1signalData1 = nullptr;
    F1signalData2 = nullptr;
    F2signalData1 = nullptr;
    F2signalData2 = nullptr;
    MixsignalData1 = nullptr;
    MixsignalData2 = nullptr;

    prob2_F1signalData1 = nullptr;
    prob2_F1signalData2 = nullptr;
    prob2_F2signalData1 = nullptr;
    prob2_F2signalData2 = nullptr;
    prob2_MixsignalData1 = nullptr;
    prob2_MixsignalData2 = nullptr;
    qDebug() << "thread delete over";
}

void SaveData1::getSignalData_Prob(ProbObject *&m_prob1, ProbObject *&m_prob2)
{
    // 探头一数据
    F1signalData1 = m_prob1->SingleFreqObj_F1Channel->m_signalProbX;
    F1signalData2 = m_prob1->SingleFreqObj_F1Channel->m_signalProbY;
    F2signalData1 = m_prob1->SingleFreqObj_F2Channel->m_signalProbX;
    F2signalData2 = m_prob1->SingleFreqObj_F2Channel->m_signalProbY;
    MixsignalData1 = m_prob1->SingleFreqObj_MixChannel->m_signalProbX;
    MixsignalData2 = m_prob1->SingleFreqObj_MixChannel->m_signalProbY;
    // 探头二数据
    prob2_F1signalData1 = m_prob2->SingleFreqObj_F1Channel->m_signalProbX;
    prob2_F1signalData2 = m_prob2->SingleFreqObj_F1Channel->m_signalProbY;
    prob2_F2signalData1 = m_prob2->SingleFreqObj_F2Channel->m_signalProbX;
    prob2_F2signalData2 = m_prob2->SingleFreqObj_F2Channel->m_signalProbY;
    prob2_MixsignalData1 = m_prob2->SingleFreqObj_MixChannel->m_signalProbX;
    prob2_MixsignalData2 = m_prob2->SingleFreqObj_MixChannel->m_signalProbY;
}

// 用于传输数据指针
void SaveData1::getSignalData_Prob1(QVector<qint16> *&m_F1signalData1, QVector<qint16> *&m_F1signalData2, QVector<qint16> *&m_F2signalData1, QVector<qint16> *&m_F2signalData2, QVector<qint16> *&m_MixsignalData1, QVector<qint16> *&m_MixsignalData2)
{
    F1signalData1 = m_F1signalData1;
    F1signalData2 = m_F1signalData2;
    F2signalData1 = m_F2signalData1;
    F2signalData2 = m_F2signalData2;
    MixsignalData1 = m_MixsignalData1;
    MixsignalData2 = m_MixsignalData2;
}
// 获取文件名和地址
void SaveData1::getFileName(QString &m_DataFolder, QString &m_DataFileName_prob1, QString &m_DataFileName_prob2, int m_state)
{
    DataFolder = m_DataFolder;
    DataFileName_prob1 = m_DataFileName_prob1;     // 带有文件后缀
    DataFileName_prob2 = m_DataFileName_prob2;
    state = m_state;
}
//
void SaveData1::run()
{
//    std::chrono::steady_clock::time_point time1 = std::chrono::steady_clock::now();
    QString updir = QFileInfo(DataFolder).dir().dirName();
    QDir newdir(DataFolder);
    newdir.cdUp();
    if (!newdir.exists(updir)) {
        newdir.mkdir(updir);  // 创建子目录
        qDebug() << "mkdir succeed";
    }
    QString fileName1 = DataFolder + DataFileName_prob1;     // 带有文件后缀
    QString fileName2 = DataFolder + DataFileName_prob2;
    QFile file1(fileName1);
    QFile file2(fileName2);
    QTextStream in1(&file1);
    QTextStream in2(&file2);
//    std::chrono::steady_clock::time_point time2 = std::chrono::steady_clock::now();
//    int total_time = std::chrono::duration_cast<std::chrono::milliseconds>(time2 - time1).count();
//    qDebug() << "saveData took time: " << total_time;

    if (state == 1) {         // 单探头单频
        QString fileName1 = DataFolder + DataFileName_prob1;
        QFile file1(fileName1);
        QTextStream in1(&file1);
        if (file1.open(QIODevice::ReadWrite|QIODevice::Append)) {
            qDebug() << "file1 has been created";
            for (int i = 0; i < F1signalData1->size(); i++) {
                in1 << F1signalData1->at(i) << "," << F1signalData2->at(i) << "\n";
            }
            file1.close();
        } else {
            qDebug() << "file1 has failed to create";
        }
    } else if (state == 2) {  // 双探头单频
        QString fileName1 = DataFolder + DataFileName_prob1;
        QString fileName2 = DataFolder + DataFileName_prob2;
        QFile file1(fileName1);
        QFile file2(fileName2);
        QTextStream in1(&file1);
        QTextStream in2(&file2);
        if (file1.open(QIODevice::ReadWrite|QIODevice::Truncate)) {
            qDebug() << "file1 has been created";
            for (int i = 0; i < F1signalData1->size(); i++) {
                in1 << F1signalData1->at(i) << "," << F1signalData2->at(i) << "\n";
            }
            file1.close();
        } else {
            qDebug() << "file1 has failed to create";
        }

        if (file2.open(QIODevice::ReadWrite|QIODevice::Truncate)) {
            qDebug() << "file2 has been created";
            for (int i = 0; i < F1signalData1->size(); i++) {
                in2 << prob2_F1signalData1->at(i) << "," << prob2_F1signalData2->at(i) << "\n";
            }
            file2.close();
        } else {
            qDebug() << "file2 has failed to create";
        }
    } else if (state == 4) {  // 单探头双频
        QString fileName1 = DataFolder + DataFileName_prob1;
        QFile file1(fileName1);
        QTextStream in1(&file1);
        if (file1.open(QIODevice::ReadWrite|QIODevice::Truncate)) {
            qDebug() << "file1 has been created";
            qDebug() << "filename: " << file1.fileName();
            for (int i = 0; i < F1signalData1->size(); i++) {
                in1 << F1signalData1->at(i) << "," << F1signalData2->at(i) << "," << F2signalData1->at(i) << "," << F2signalData2->at(i) << "," << MixsignalData1->at(i) << "," << MixsignalData2->at(i) << "\n";
            }
            file1.close();
            qDebug() << F1signalData1->end() << " " << F1signalData2->end();
        } else {
            qDebug() << "file1 has failed to create";
            qDebug() << "filename: " << file1.fileName();
        }
    } else if (state == 6) {   // 双探头双频
        QString fileName1 = DataFolder + DataFileName_prob1;
        QString fileName2 = DataFolder + DataFileName_prob2;
        QFile file1(fileName1);
        QFile file2(fileName2);
        QTextStream in1(&file1);
        QTextStream in2(&file2);
        if (file1.open(QIODevice::ReadWrite|QIODevice::Truncate)) {
            qDebug() << "file1 has been created";
            for (int i = 0; i < F1signalData1->size(); i++) {
                in1 << F1signalData1->at(i) << "," << F1signalData2->at(i) << "," << F2signalData1->at(i) << "," << F2signalData2->at(i) << "," << MixsignalData1->at(i) << "," << MixsignalData2->at(i) << "\n";
            }
            file1.close();
        } else {
            qDebug() << "file1 has failed to create";
        }
        if (file2.open(QIODevice::ReadWrite|QIODevice::Truncate)) {
            qDebug() << "file2 has been created";
            for (int i = 0; i < F1signalData1->size(); i++) {
                in2 << prob2_F1signalData1->at(i) << "," << prob2_F1signalData2->at(i) << "," << prob2_F2signalData1->at(i) << "," << prob2_F2signalData2->at(i) << "," << prob2_MixsignalData1->at(i) << "," << prob2_MixsignalData2->at(i) << "\n";
            }
            file2.close();
        } else {
            qDebug() << "file2 has failed to create";
        }
    }


    qDebug() << "this thread has been delete";
}
