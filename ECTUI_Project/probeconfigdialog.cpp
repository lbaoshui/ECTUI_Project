#include "probeconfigdialog.h"
#include "probemanager.h"
#include "probe.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QFrame>

/**
 * @brief 构造探头参数配置对话框
 * @param probeManager 探头管理器指针，用于读写探头配置
 * @param parent 父控件，通常为 MainWindow
 *
 * 设置窗口属性后依次调用 setupUI() 搭建界面、loadFromProbeManager() 加载现有配置。
 */
ProbeConfigDialog::ProbeConfigDialog(ProbeManager *probeManager, QWidget *parent)
    : QDialog(parent)
    , m_probeManager(probeManager)
{
    setWindowTitle(tr("Probe Channel Configuration"));
    setMinimumSize(620, 520);
    resize(660, 560);
    setupUI();
    loadFromProbeManager();
}

/**
 * @brief 搭建对话框整体布局与深色主题样式
 *
 * 布局自上而下为：标题 → 分隔线 → 探头数量选择行 → QScrollArea（参数组） → Apply/Cancel 按钮行。
 * 样式表统一使用暗色系，与主窗口 VS Code Dark 主题保持一致。
 */
void ProbeConfigDialog::setupUI()
{
    setStyleSheet(
        "QDialog { background-color: #1e1e1e; }"
        "QLabel { color: #cccccc; font-size: 13px; background: transparent; border: none; }"
        "QGroupBox {"
        "    background-color: #252526; border: 1px solid #3c3c3c;"
        "    border-radius: 4px; margin-top: 12px; padding-top: 16px;"
        "    font-size: 13px; font-weight: bold; color: #cccccc;"
        "}"
        "QGroupBox::title {"
        "    subcontrol-origin: margin; subcontrol-position: top left;"
        "    padding: 2px 8px; color: #4fc3f7;"
        "}"
        "QSpinBox {"
        "    background-color: #2d2d30; color: #ffffff; border: 1px solid #3c3c3c;"
        "    border-radius: 3px; padding: 4px 8px; font-size: 13px; min-height: 22px;"
        "}"
        "QSpinBox:focus { border: 1px solid #0e639c; }"
        "QCheckBox { color: #cccccc; font-size: 13px; spacing: 6px; background: transparent; }"
        "QCheckBox::indicator {"
        "    width: 16px; height: 16px; border: 1px solid #3c3c3c;"
        "    border-radius: 2px; background-color: #2d2d30;"
        "}"
        "QCheckBox::indicator:checked { background-color: #0e639c; }"
        "QPushButton {"
        "    background-color: #0e639c; color: white; border: 1px solid #3c3c3c;"
        "    border-radius: 3px; padding: 8px 20px; font-size: 13px; font-weight: bold;"
        "    min-width: 90px;"
        "}"
        "QPushButton:hover { background-color: #1177bb; }"
        "QPushButton:pressed { background-color: #094771; }"
        "QScrollArea { background-color: #1e1e1e; border: 1px solid #3c3c3c; border-radius: 3px; }"
        "QScrollBar:vertical {"
        "    background: #1e1e1e; width: 10px; border-radius: 5px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: #3c3c3c; border-radius: 5px; min-height: 30px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: #0e639c; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 14, 16, 14);
    mainLayout->setSpacing(10);

    // ── 标题 ──
    QLabel *titleLabel = new QLabel(tr("探头参数配置"), this);
    titleLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: #ffffff; padding: 0; }");
    mainLayout->addWidget(titleLabel);

    // ── 分隔线 ──
    QFrame *sepLine = new QFrame(this);
    sepLine->setFrameShape(QFrame::HLine);
    sepLine->setStyleSheet("QFrame { background-color: #3c3c3c; max-height: 1px; border: none; }");
    mainLayout->addWidget(sepLine);

    // ── 探头数量选择 ──
    QHBoxLayout *countLayout = new QHBoxLayout();
    countLayout->setSpacing(10);
    QLabel *countLabel = new QLabel(tr("探头通道数量:"), this);
    countLabel->setStyleSheet("QLabel { font-size: 13px; font-weight: bold; color: #cccccc; }");
    m_probeCountSpinBox = new QSpinBox(this);
    m_probeCountSpinBox->setRange(1, 16);
    m_probeCountSpinBox->setValue(8);
    m_probeCountSpinBox->setFixedWidth(80);
    m_probeCountSpinBox->setToolTip(tr("Set the number of probe channels (1-16)"));
    countLayout->addWidget(countLabel);
    countLayout->addWidget(m_probeCountSpinBox);
    countLayout->addStretch();
    mainLayout->addLayout(countLayout);

    // ── 滚动区域 (探头参数组) ──
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_probeGroupsContainer = new QWidget();
    m_probeGroupsLayout = new QVBoxLayout(m_probeGroupsContainer);
    m_probeGroupsLayout->setContentsMargins(6, 6, 6, 6);
    m_probeGroupsLayout->setSpacing(8);
    m_probeGroupsLayout->addStretch();
    m_scrollArea->setWidget(m_probeGroupsContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    // ── 底部按钮 ──
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(12);
    buttonLayout->addStretch();
    QPushButton *applyBtn = new QPushButton(tr("Apply"), this);
    QPushButton *cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setStyleSheet(
        "QPushButton { background-color: #3c3c3c; color: #cccccc; border: 1px solid #555;"
        "    border-radius: 3px; padding: 8px 20px; font-size: 13px; font-weight: bold; min-width: 90px; }"
        "QPushButton:hover { background-color: #555; }"
    );
    buttonLayout->addWidget(applyBtn);
    buttonLayout->addWidget(cancelBtn);
    mainLayout->addLayout(buttonLayout);

    // ── 信号连接 ──
    connect(m_probeCountSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ProbeConfigDialog::onProbeCountChanged);
    connect(applyBtn, &QPushButton::clicked, this, &ProbeConfigDialog::onApplyClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

/**
 * @brief 从 ProbeManager 读取现有配置并回填 UI
 *
 * 先同步探头数量，再逐个 Probe 回填硬件通道、激励频率/相位/幅度及启用状态。
 * 通常在对话框首次打开时调用，确保 UI 反映当前实际配置。
 */
void ProbeConfigDialog::loadFromProbeManager()
{
    const int count = m_probeManager->probeCount();
    m_probeCountSpinBox->setValue(count);
    rebuildProbeGroups(count);

    for (int i = 0; i < count && i < m_probeGroupWidgets.size(); ++i) {
        Probe *probe = m_probeManager->probeAt(i);
        if (!probe) continue;
        const auto &w = m_probeGroupWidgets[i];
        w.hwChannelSpinBox->setValue(probe->hardwareChannel());
        w.excFreqSpinBox->setValue(probe->excitationFreq());
        w.excPhaseSpinBox->setValue(probe->excitationPhase());
        w.excAmpSpinBox->setValue(probe->excitationAmp());
        w.enabledCheckBox->setChecked(probe->isEnabled());
    }
}

/**
 * @brief 探头数量变更槽函数
 * @param count 新的通道数量，由 QSpinBox 发出
 *
 * 通知 rebuildProbeGroups() 销毁旧控件并按新数量重建。
 */
void ProbeConfigDialog::onProbeCountChanged(int count)
{
    rebuildProbeGroups(count);
}

/**
 * @brief 销毁旧参数组并创建新数量的探头配置卡片
 * @param count 目标探头通道数量
 *
 * 先移除 m_probeGroupsLayout 内所有旧 GroupBox 和底部 stretch，
 * 再逐个创建含硬件通道、激励参数及启用开关的 QGroupBox 卡片。
 */
void ProbeConfigDialog::rebuildProbeGroups(int count)
{
    for (auto &w : m_probeGroupWidgets) {
        m_probeGroupsLayout->removeWidget(w.groupBox);
        delete w.groupBox;
    }
    m_probeGroupWidgets.clear();

    // 移除 stretch item（最后一个）
    if (m_probeGroupsLayout->count() > 0) {
        QLayoutItem *stretchItem = m_probeGroupsLayout->takeAt(m_probeGroupsLayout->count() - 1);
        delete stretchItem;
    }

    for (int i = 0; i < count; ++i) {
        ProbeGroupWidgets w;
        w.groupBox = new QGroupBox(tr("探头通道 %1").arg(i + 1), m_probeGroupsContainer);

        QGridLayout *grid = new QGridLayout(w.groupBox);
        grid->setContentsMargins(12, 18, 12, 10);
        grid->setHorizontalSpacing(16);
        grid->setVerticalSpacing(8);

        // ── 第一行：硬件通道 + 启用 ──
        QLabel *hwLabel = new QLabel(tr("硬件通道:"), w.groupBox);
        hwLabel->setStyleSheet("QLabel { font-weight: normal; color: #cccccc; }");
        w.hwChannelSpinBox = new QSpinBox(w.groupBox);
        w.hwChannelSpinBox->setRange(1, 16);
        w.hwChannelSpinBox->setValue(i + 1);
        w.hwChannelSpinBox->setFixedWidth(80);
        w.hwChannelSpinBox->setToolTip(tr("Hardware ADC channel (1-16)"));

        QLabel *enLabel = new QLabel(tr("启用:"), w.groupBox);
        enLabel->setStyleSheet("QLabel { font-weight: normal; color: #cccccc; }");
        w.enabledCheckBox = new QCheckBox(w.groupBox);
        w.enabledCheckBox->setChecked(true);

        grid->addWidget(hwLabel, 0, 0);
        grid->addWidget(w.hwChannelSpinBox, 0, 1);
        grid->addWidget(enLabel, 0, 2);
        grid->addWidget(w.enabledCheckBox, 0, 3);
        grid->setColumnStretch(1, 0);
        grid->setColumnStretch(3, 1);

        // ── 第二行：激励频率 ──
        QLabel *freqLabel = new QLabel(tr("激励频率 (Hz):"), w.groupBox);
        freqLabel->setStyleSheet("QLabel { font-weight: normal; color: #cccccc; }");
        w.excFreqSpinBox = new QSpinBox(w.groupBox);
        w.excFreqSpinBox->setRange(100, 1000000);
        w.excFreqSpinBox->setSingleStep(100);
        w.excFreqSpinBox->setValue(10000);
        w.excFreqSpinBox->setFixedWidth(120);
        w.excFreqSpinBox->setToolTip(tr("Excitation frequency in Hz"));

        grid->addWidget(freqLabel, 1, 0);
        grid->addWidget(w.excFreqSpinBox, 1, 1);

        // ── 第三行：相位 ──
        QLabel *phaseLabel = new QLabel(tr("激励相位 (deg):"), w.groupBox);
        phaseLabel->setStyleSheet("QLabel { font-weight: normal; color: #cccccc; }");
        w.excPhaseSpinBox = new QSpinBox(w.groupBox);
        w.excPhaseSpinBox->setRange(0, 359);
        w.excPhaseSpinBox->setValue(0);
        w.excPhaseSpinBox->setFixedWidth(120);
        w.excPhaseSpinBox->setToolTip(tr("Excitation phase angle in degrees"));

        grid->addWidget(phaseLabel, 2, 0);
        grid->addWidget(w.excPhaseSpinBox, 2, 1);

        // ── 第四行：幅度 ──
        QLabel *ampLabel = new QLabel(tr("激励幅度 (%):"), w.groupBox);
        ampLabel->setStyleSheet("QLabel { font-weight: normal; color: #cccccc; }");
        w.excAmpSpinBox = new QSpinBox(w.groupBox);
        w.excAmpSpinBox->setRange(0, 100);
        w.excAmpSpinBox->setValue(60);
        w.excAmpSpinBox->setFixedWidth(120);
        w.excAmpSpinBox->setToolTip(tr("Excitation amplitude in percent (max 60% recommended)"));

        grid->addWidget(ampLabel, 3, 0);
        grid->addWidget(w.excAmpSpinBox, 3, 1);

        m_probeGroupWidgets.append(w);
        m_probeGroupsLayout->addWidget(w.groupBox);
    }

    // 添加底部弹簧，使参数组卡片紧凑靠上排列
    m_probeGroupsLayout->addStretch();
}

/**
 * @brief 应用当前 UI 配置到 ProbeManager 并关闭对话框
 *
 * 依次执行：设置探头数量 → 逐通道写入硬件通道号、激励参数及启用状态。
 * 所有写入操作直接作用于 Probe 对象，调用 accept() 关闭对话框。
 */
void ProbeConfigDialog::onApplyClicked()
{
    const int count = m_probeCountSpinBox->value();

    m_probeManager->setProbeCount(count);
    for (int i = 0; i < count && i < m_probeGroupWidgets.size(); ++i) {
        Probe *probe = m_probeManager->probeAt(i);
        if (!probe) continue;
        const auto &w = m_probeGroupWidgets[i];
        probe->setHardwareChannel(w.hwChannelSpinBox->value());
        probe->setExcitation(w.excFreqSpinBox->value(),
                             w.excPhaseSpinBox->value(),
                             w.excAmpSpinBox->value());
        probe->setEnabled(w.enabledCheckBox->isChecked());
    }

    accept();
}
