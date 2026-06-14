#ifndef PROBECONFIGDIALOG_H
#define PROBECONFIGDIALOG_H

#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>

class ProbeManager;
class DataAcquisitionThread;

/**
 * @brief 探头通道参数配置对话框
 *
 * 提供探头数量设置及各通道独立的激励参数配置界面。
 * 探头数量变更时实时重建 UI 组，确认后将配置写回 ProbeManager。
 */
class ProbeConfigDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造配置对话框
     * @param probeManager 探头管理器，用于读取/写入探头配置
     * @param parent 父控件
     */
    explicit ProbeConfigDialog(ProbeManager *probeManager,
                               DataAcquisitionThread *acqThread = nullptr,
                               QWidget *parent = nullptr);

private slots:
    /**
     * @brief 探头数量变更时重建参数组 UI
     * @param count 新的探头通道数量
     */
    void onProbeCountChanged(int count);

    /** @brief 应用配置到 ProbeManager 并关闭对话框 */
    void onApplyClicked();

private:
    /**
     * @brief 搭建对话框整体布局与样式
     *
     * 创建标题栏、探头数量选择器、滚动区域、Apply/Cancel 按钮，
     * 并设置深色主题样式表。
     */
    void setupUI();

    /**
     * @brief 按给定数量重建探头参数组控件
     * @param count 目标探头通道数量
     *
     * 移除旧控件后依次创建 count 个 QGroupBox，
     * 每组含硬件通道、激励频率/相位/幅度及启用开关。
     */
    void rebuildProbeGroups(int count);

    /**
     * @brief 从 ProbeManager 现有状态加载到 UI
     *
     * 读取当前探头数量及各 Probe 的配置参数，回填到对应控件。
     */
    void loadFromProbeManager();

    /** @brief 单个探头参数组的控件集合 */
    struct ProbeGroupWidgets {
        QGroupBox *groupBox;            ///< 分组容器
        QSpinBox *hwChannelSpinBox;     ///< 硬件通道号 (1-16)
        QSpinBox *excFreqSpinBox;       ///< 激励频率 (Hz)
        QSpinBox *excPhaseSpinBox;      ///< 激励相位 (deg)
        QSpinBox *excAmpSpinBox;        ///< 激励幅度 (%)
        QCheckBox *enabledCheckBox;     ///< 启用开关
        QCheckBox *filterLpCheckBox;    ///< 低通使能
        QSpinBox *filterLpCutoffSpinBox; ///< 低通截止频率 (Hz)
        QCheckBox *filterHpCheckBox;    ///< 高通使能
        QSpinBox *filterHpCutoffSpinBox; ///< 高通截止频率 (Hz)
    };

    /** @brief 探头参数的快照，用于在数量变更时保留用户修改 */  
    struct ProbeStateSnapshot {
        int hwChannel = 0;
        int excFreq = 10000;
        int excPhase = 0;
        int excAmp = 60;
        bool enabled = true;
        bool lpEnabled = false;
        int lpCutoff = 5000;
        bool hpEnabled = false;
        int hpCutoff = 100;
    };

    ProbeManager *m_probeManager;            ///< 探头管理器（外部传入，不持有所有权）
    DataAcquisitionThread *m_acqThread;      ///< 采集线程（外部传入，用于配置滤波器）
    QSpinBox *m_probeCountSpinBox;           ///< 探头数量选择器
    QVector<ProbeStateSnapshot> m_savedStates; ///< 跨重建累积的快照（索引=探头序号）
    QScrollArea *m_scrollArea;               ///< 参数组滚动区域
    QWidget *m_probeGroupsContainer;         ///< 参数组容器
    QVBoxLayout *m_probeGroupsLayout;        ///< 参数组垂直布局
    QList<ProbeGroupWidgets> m_probeGroupWidgets; ///< 当前所有参数组控件
};

#endif // PROBECONFIGDIALOG_H
