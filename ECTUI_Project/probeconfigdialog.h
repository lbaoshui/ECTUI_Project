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
    explicit ProbeConfigDialog(ProbeManager *probeManager, QWidget *parent = nullptr);

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
    };

    ProbeManager *m_probeManager;            ///< 探头管理器（外部传入，不持有所有权）
    QSpinBox *m_probeCountSpinBox;           ///< 探头数量选择器
    QScrollArea *m_scrollArea;               ///< 参数组滚动区域
    QWidget *m_probeGroupsContainer;         ///< 参数组容器
    QVBoxLayout *m_probeGroupsLayout;        ///< 参数组垂直布局
    QList<ProbeGroupWidgets> m_probeGroupWidgets; ///< 当前所有参数组控件
};

#endif // PROBECONFIGDIALOG_H
