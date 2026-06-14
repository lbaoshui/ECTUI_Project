#include "probeconfigdialog.h"
#include "probemanager.h"
#include "probe.h"
#include "dataacquisitionthread.h"
#include "filter.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>

/**
 * @brief 构造探头参数配置对话框
 * @param probeManager 探头管理器指针，用于读写探头配置
 * @param parent 父控件，通常为 MainWindow
 *
 * 设置窗口属性后依次调用 setupUI() 搭建界面、loadFromProbeManager() 加载现有配置。
 */
ProbeConfigDialog::ProbeConfigDialog(ProbeManager *probeManager,
                                   DataAcquisitionThread *acqThread,
                                   QWidget *parent)
    : QDialog(parent)
    , m_probeManager(probeManager)
    , m_acqThread(acqThread)
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
    QPushButton *loadCfgBtn = new QPushButton(tr("Load Config"), this);
    QPushButton *saveCfgBtn = new QPushButton(tr("Save Config"), this);
    QPushButton *applyBtn = new QPushButton(tr("Apply"), this);
    QPushButton *cancelBtn = new QPushButton(tr("Cancel"), this);
    const QString secondaryBtnStyle =
        "QPushButton { background-color: #3c3c3c; color: #cccccc; border: 1px solid #555;"
        "    border-radius: 3px; padding: 8px 20px; font-size: 13px; font-weight: bold; min-width: 90px; }"
        "QPushButton:hover { background-color: #555; }";
    loadCfgBtn->setStyleSheet(secondaryBtnStyle);
    saveCfgBtn->setStyleSheet(secondaryBtnStyle);
    cancelBtn->setStyleSheet(secondaryBtnStyle);
    buttonLayout->addWidget(loadCfgBtn);
    buttonLayout->addWidget(saveCfgBtn);
    buttonLayout->addWidget(applyBtn);
    buttonLayout->addWidget(cancelBtn);
    mainLayout->addLayout(buttonLayout);

    // ── 信号连接 ──
    connect(m_probeCountSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ProbeConfigDialog::onProbeCountChanged);
    connect(applyBtn, &QPushButton::clicked, this, &ProbeConfigDialog::onApplyClicked);
    connect(saveCfgBtn, &QPushButton::clicked, this, &ProbeConfigDialog::onSaveConfigClicked);
    connect(loadCfgBtn, &QPushButton::clicked, this, &ProbeConfigDialog::onLoadConfigClicked);
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

        // 从 Probe 恢复滤波配置（对话框再次打开时不丢失）
        w.filterLpCheckBox->setChecked(probe->filterLpEnabled());
        w.filterLpCutoffSpinBox->setValue(static_cast<int>(probe->filterLpCutoffHz()));
        w.filterHpCheckBox->setChecked(probe->filterHpEnabled());
        w.filterHpCutoffSpinBox->setValue(static_cast<int>(probe->filterHpCutoffHz()));
    }

    // 初次加载后，将 Probe 中的持久化值写入快照，       
    // 确保后续数量变更时从正确值恢复（而非从重建时的默认值）。 
    if (count > m_savedStates.size()) {
        m_savedStates.resize(count);
    }
    for (int i = 0; i < count && i < m_probeGroupWidgets.size(); ++i) {
        const auto &w = m_probeGroupWidgets[i];
        auto &s = m_savedStates[i];
        s.hwChannel = w.hwChannelSpinBox->value();
        s.excFreq   = w.excFreqSpinBox->value();
        s.excPhase  = w.excPhaseSpinBox->value();
        s.excAmp    = w.excAmpSpinBox->value();
        s.enabled   = w.enabledCheckBox->isChecked();
        s.lpEnabled = w.filterLpCheckBox->isChecked();
        s.lpCutoff  = w.filterLpCutoffSpinBox->value();
        s.hpEnabled = w.filterHpCheckBox->isChecked();
        s.hpCutoff  = w.filterHpCutoffSpinBox->value();
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
 * 先保存当前 UI 中已修改的参数，再销毁旧控件并重建。
 * 重建后将保存的参数恢复到索引匹配的探头（避免修改探头数量后已改参数丢失）。
 */
void ProbeConfigDialog::rebuildProbeGroups(int count)
{
    // ── 1. 将当前 UI 状态写入成员快照（跨多次重建累积保留） ──
    if (m_probeGroupWidgets.size() > m_savedStates.size()) {
        m_savedStates.resize(m_probeGroupWidgets.size());
    }
    for (int i = 0; i < m_probeGroupWidgets.size(); ++i) {
        const auto &w = m_probeGroupWidgets[i];
        auto &s = m_savedStates[i];
        s.hwChannel = w.hwChannelSpinBox->value();
        s.excFreq   = w.excFreqSpinBox->value();
        s.excPhase  = w.excPhaseSpinBox->value();
        s.excAmp    = w.excAmpSpinBox->value();
        s.enabled   = w.enabledCheckBox->isChecked();
        s.lpEnabled = w.filterLpCheckBox->isChecked();
        s.lpCutoff  = w.filterLpCutoffSpinBox->value();
        s.hpEnabled = w.filterHpCheckBox->isChecked();
        s.hpCutoff  = w.filterHpCutoffSpinBox->value();
    }

    // ── 2. 销毁旧控件 ──
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

    // ── 3. 快照只增不减：增长时先扩容，新建控件后同步默认值 ──
    const int oldSavedSize = m_savedStates.size();
    if (count > oldSavedSize) {
        m_savedStates.resize(count);
    }

    // ── 4. 重建新控件（先用默认值创建，再连接实时信号） ──
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
        w.excFreqSpinBox->setRange(100, 10000000);
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

        // ── 第五行：数字滤波（LP 和 HP 可独立使能，同时勾选即带通） ──
        QLabel *filterLabel = new QLabel(tr("数字滤波:"), w.groupBox);
        filterLabel->setStyleSheet("QLabel { font-weight: normal; color: #cccccc; }");

        // 低通组
        w.filterLpCheckBox = new QCheckBox(tr("低通"), w.groupBox);
        w.filterLpCheckBox->setChecked(false);
        w.filterLpCutoffSpinBox = new QSpinBox(w.groupBox);
        w.filterLpCutoffSpinBox->setRange(1, 50000);
        w.filterLpCutoffSpinBox->setValue(5000);
        w.filterLpCutoffSpinBox->setFixedWidth(100);
        w.filterLpCutoffSpinBox->setSuffix(tr(" Hz"));
        w.filterLpCutoffSpinBox->setEnabled(false);
        QObject::connect(w.filterLpCheckBox, &QCheckBox::toggled,
                         w.filterLpCutoffSpinBox, &QSpinBox::setEnabled);

        // 高通组
        w.filterHpCheckBox = new QCheckBox(tr("高通"), w.groupBox);
        w.filterHpCheckBox->setChecked(false);
        w.filterHpCutoffSpinBox = new QSpinBox(w.groupBox);
        w.filterHpCutoffSpinBox->setRange(1, 50000);
        w.filterHpCutoffSpinBox->setValue(100);
        w.filterHpCutoffSpinBox->setFixedWidth(100);
        w.filterHpCutoffSpinBox->setSuffix(tr(" Hz"));
        w.filterHpCutoffSpinBox->setEnabled(false);
        QObject::connect(w.filterHpCheckBox, &QCheckBox::toggled,
                         w.filterHpCutoffSpinBox, &QSpinBox::setEnabled);

        grid->addWidget(filterLabel, 4, 0);
        grid->addWidget(w.filterLpCheckBox, 4, 1);
        grid->addWidget(w.filterLpCutoffSpinBox, 4, 2);
        grid->addWidget(w.filterHpCheckBox, 4, 3);
        grid->addWidget(w.filterHpCutoffSpinBox, 4, 4);

        m_probeGroupWidgets.append(w);
        m_probeGroupsLayout->addWidget(w.groupBox);
    }

    // ── 5. 从快照恢复已有通道的数据，新增通道的默认值同步到快照 ──
    const int restoreCount = qMin(count, oldSavedSize);
    for (int i = 0; i < restoreCount; ++i) {
        const auto &s = m_savedStates[i];
        auto &w = m_probeGroupWidgets[i];
        w.hwChannelSpinBox->setValue(s.hwChannel);
        w.excFreqSpinBox->setValue(s.excFreq);
        w.excPhaseSpinBox->setValue(s.excPhase);
        w.excAmpSpinBox->setValue(s.excAmp);
        w.enabledCheckBox->setChecked(s.enabled);
        w.filterLpCheckBox->setChecked(s.lpEnabled);
        w.filterLpCutoffSpinBox->setValue(s.lpCutoff);
        w.filterHpCheckBox->setChecked(s.hpEnabled);
        w.filterHpCutoffSpinBox->setValue(s.hpCutoff);
    }
    // 新增通道：控件已按默认值创建，同步到快照尾部
    for (int i = oldSavedSize; i < count; ++i) {
        const auto &w = m_probeGroupWidgets[i];
        auto &s = m_savedStates[i];
        s.hwChannel = w.hwChannelSpinBox->value();
        s.excFreq   = w.excFreqSpinBox->value();
        s.excPhase  = w.excPhaseSpinBox->value();
        s.excAmp    = w.excAmpSpinBox->value();
        s.enabled   = w.enabledCheckBox->isChecked();
        s.lpEnabled = w.filterLpCheckBox->isChecked();
        s.lpCutoff  = w.filterLpCutoffSpinBox->value();
        s.hpEnabled = w.filterHpCheckBox->isChecked();
        s.hpCutoff  = w.filterHpCutoffSpinBox->value();
    }

    // 添加底部弹簧，使参数组卡片紧凑靠上排列
    m_probeGroupsLayout->addStretch();
}

/**
 * @brief 应用当前 UI 配置到 ProbeManager 并同步快照
 *
 * 依次执行：设置探头数量 → 逐通道写入硬件通道号、激励参数及启用状态 →
 * 配置采集线程滤波器 → 将当前 UI 状态同步到快照。
 * 应用后不关闭对话框，由用户手动关闭。
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

        // 滤波配置持久化到 Probe（下次打开对话框时恢复）
        probe->setFilterLpEnabled(w.filterLpCheckBox->isChecked());
        probe->setFilterLpCutoffHz(static_cast<float>(w.filterLpCutoffSpinBox->value()));
        probe->setFilterHpEnabled(w.filterHpCheckBox->isChecked());
        probe->setFilterHpCutoffHz(static_cast<float>(w.filterHpCutoffSpinBox->value()));
    }

    // 将滤波配置写入采集线程（如果传入了采集线程指针）
    if (m_acqThread) {
        const float sampleRateHz = m_acqThread->sampleRateHz();
        for (int i = 0; i < count && i < m_probeGroupWidgets.size(); ++i) {
            const auto &w = m_probeGroupWidgets[i];
            const bool useLp = w.filterLpCheckBox->isChecked();
            const bool useHp = w.filterHpCheckBox->isChecked();

            if (!useLp && !useHp) {
                m_acqThread->removeFilter(i);
            } else if (useLp && !useHp) {
                m_acqThread->configureFilter(i, FilterType::LowPass,
                    static_cast<float>(w.filterLpCutoffSpinBox->value()), sampleRateHz);
            } else if (!useLp && useHp) {
                m_acqThread->configureFilter(i, FilterType::HighPass,
                    static_cast<float>(w.filterHpCutoffSpinBox->value()), sampleRateHz);
            } else {
                // LP + HP → 带通：先 HP 滤低频漂移，再 LP 滤高频噪声
                m_acqThread->configureFilter(i, FilterType::HighPass,
                    static_cast<float>(w.filterHpCutoffSpinBox->value()), sampleRateHz);
                m_acqThread->addFilterStage(i, FilterType::LowPass,
                    static_cast<float>(w.filterLpCutoffSpinBox->value()), sampleRateHz);
            }
        }
    }

    // 将当前 UI 状态同步到快照（只在 Apply 时更新）
    if (count > m_savedStates.size()) {
        m_savedStates.resize(count);
    }
    for (int i = 0; i < count && i < m_probeGroupWidgets.size(); ++i) {
        const auto &w = m_probeGroupWidgets[i];
        auto &s = m_savedStates[i];
        s.hwChannel = w.hwChannelSpinBox->value();
        s.excFreq   = w.excFreqSpinBox->value();
        s.excPhase  = w.excPhaseSpinBox->value();
        s.excAmp    = w.excAmpSpinBox->value();
        s.enabled   = w.enabledCheckBox->isChecked();
        s.lpEnabled = w.filterLpCheckBox->isChecked();
        s.lpCutoff  = w.filterLpCutoffSpinBox->value();
        s.hpEnabled = w.filterHpCheckBox->isChecked();
        s.hpCutoff  = w.filterHpCutoffSpinBox->value();
    }
}

void ProbeConfigDialog::onSaveConfigClicked()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("保存探头配置"), QString(), tr("JSON 文件 (*.json)"));
    if (filePath.isEmpty()) return;

    const int count = m_probeCountSpinBox->value();

    QJsonObject root;
    root[QStringLiteral("probeCount")] = count;

    QJsonArray probesArray;
    for (int i = 0; i < count && i < m_probeGroupWidgets.size(); ++i) {
        const auto &w = m_probeGroupWidgets[i];
        QJsonObject probeObj;
        probeObj[QStringLiteral("hwChannel")] = w.hwChannelSpinBox->value();
        probeObj[QStringLiteral("excFreq")]   = w.excFreqSpinBox->value();
        probeObj[QStringLiteral("excPhase")]  = w.excPhaseSpinBox->value();
        probeObj[QStringLiteral("excAmp")]    = w.excAmpSpinBox->value();
        probeObj[QStringLiteral("enabled")]   = w.enabledCheckBox->isChecked();
        probeObj[QStringLiteral("lpEnabled")] = w.filterLpCheckBox->isChecked();
        probeObj[QStringLiteral("lpCutoff")]  = w.filterLpCutoffSpinBox->value();
        probeObj[QStringLiteral("hpEnabled")] = w.filterHpCheckBox->isChecked();
        probeObj[QStringLiteral("hpCutoff")]  = w.filterHpCutoffSpinBox->value();
        probesArray.append(probeObj);
    }
    root[QStringLiteral("probes")] = probesArray;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("保存失败"),
                             tr("无法写入文件:\n%1").arg(file.errorString()));
        return;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

void ProbeConfigDialog::onLoadConfigClicked()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("加载探头配置"), QString(), tr("JSON 文件 (*.json)"));
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("加载失败"),
                             tr("无法打开文件:\n%1").arg(file.errorString()));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, tr("加载失败"),
                             tr("JSON 解析错误:\n%1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject root = doc.object();
    const int count = qBound(1, root.value(QStringLiteral("probeCount")).toInt(1), 16);
    const QJsonArray probesArray = root.value(QStringLiteral("probes")).toArray();

    // 更新探头数量 → 触发重建 UI
    m_probeCountSpinBox->setValue(count);

    // 回填各通道参数
    for (int i = 0; i < count && i < probesArray.size() && i < m_probeGroupWidgets.size(); ++i) {
        const QJsonObject probeObj = probesArray[i].toObject();
        const auto &w = m_probeGroupWidgets[i];
        w.hwChannelSpinBox->setValue(probeObj.value(QStringLiteral("hwChannel")).toInt(i + 1));
        w.excFreqSpinBox->setValue(probeObj.value(QStringLiteral("excFreq")).toInt(10000));
        w.excPhaseSpinBox->setValue(probeObj.value(QStringLiteral("excPhase")).toInt(0));
        w.excAmpSpinBox->setValue(probeObj.value(QStringLiteral("excAmp")).toInt(60));
        w.enabledCheckBox->setChecked(probeObj.value(QStringLiteral("enabled")).toBool(true));
        w.filterLpCheckBox->setChecked(probeObj.value(QStringLiteral("lpEnabled")).toBool(false));
        w.filterLpCutoffSpinBox->setValue(probeObj.value(QStringLiteral("lpCutoff")).toInt(5000));
        w.filterHpCheckBox->setChecked(probeObj.value(QStringLiteral("hpEnabled")).toBool(false));
        w.filterHpCutoffSpinBox->setValue(probeObj.value(QStringLiteral("hpCutoff")).toInt(100));
    }

    // 同步到快照（后续通道数量变更时可正确恢复）
    if (count > m_savedStates.size()) {
        m_savedStates.resize(count);
    }
    for (int i = 0; i < count && i < m_probeGroupWidgets.size(); ++i) {
        const auto &w = m_probeGroupWidgets[i];
        auto &s = m_savedStates[i];
        s.hwChannel = w.hwChannelSpinBox->value();
        s.excFreq   = w.excFreqSpinBox->value();
        s.excPhase  = w.excPhaseSpinBox->value();
        s.excAmp    = w.excAmpSpinBox->value();
        s.enabled   = w.enabledCheckBox->isChecked();
        s.lpEnabled = w.filterLpCheckBox->isChecked();
        s.lpCutoff  = w.filterLpCutoffSpinBox->value();
        s.hpEnabled = w.filterHpCheckBox->isChecked();
        s.hpCutoff  = w.filterHpCutoffSpinBox->value();
    }
}

QString ProbeConfigDialog::defaultConfigFilePath()
{
    return QDir::currentPath() + QStringLiteral("/config/default_probe.json");
}

bool ProbeConfigDialog::saveProbeConfigToFile(ProbeManager *pm, const QString &filePath)
{
    if (!pm) return false;

    const int count = pm->probeCount();

    QJsonObject root;
    root[QStringLiteral("probeCount")] = count;

    QJsonArray probesArray;
    for (int i = 0; i < count; ++i) {
        const Probe *probe = pm->probeAt(i);
        if (!probe) continue;
        QJsonObject probeObj;
        probeObj[QStringLiteral("hwChannel")] = probe->hardwareChannel();
        probeObj[QStringLiteral("excFreq")]   = probe->excitationFreq();
        probeObj[QStringLiteral("excPhase")]  = probe->excitationPhase();
        probeObj[QStringLiteral("excAmp")]    = probe->excitationAmp();
        probeObj[QStringLiteral("enabled")]   = probe->isEnabled();
        probeObj[QStringLiteral("lpEnabled")] = probe->filterLpEnabled();
        probeObj[QStringLiteral("lpCutoff")]  = static_cast<int>(probe->filterLpCutoffHz());
        probeObj[QStringLiteral("hpEnabled")] = probe->filterHpEnabled();
        probeObj[QStringLiteral("hpCutoff")]  = static_cast<int>(probe->filterHpCutoffHz());
        probesArray.append(probeObj);
    }
    root[QStringLiteral("probes")] = probesArray;

    const QString dirPath = QFileInfo(filePath).absolutePath();
    if (!QDir().mkpath(dirPath)) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool ProbeConfigDialog::loadProbeConfigFromFile(ProbeManager *pm, const QString &filePath)
{
    if (!pm) return false;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        return false;
    }

    const QJsonObject root = doc.object();
    const int count = qBound(1, root.value(QStringLiteral("probeCount")).toInt(1), 16);
    const QJsonArray probesArray = root.value(QStringLiteral("probes")).toArray();

    pm->setProbeCount(count);
    for (int i = 0; i < count && i < probesArray.size(); ++i) {
        Probe *probe = pm->probeAt(i);
        if (!probe) continue;
        const QJsonObject probeObj = probesArray[i].toObject();
        probe->setHardwareChannel(probeObj.value(QStringLiteral("hwChannel")).toInt(i + 1));
        probe->setExcitation(probeObj.value(QStringLiteral("excFreq")).toInt(10000),
                             probeObj.value(QStringLiteral("excPhase")).toInt(0),
                             probeObj.value(QStringLiteral("excAmp")).toInt(60));
        probe->setEnabled(probeObj.value(QStringLiteral("enabled")).toBool(true));
        probe->setFilterLpEnabled(probeObj.value(QStringLiteral("lpEnabled")).toBool(false));
        probe->setFilterLpCutoffHz(static_cast<float>(probeObj.value(QStringLiteral("lpCutoff")).toInt(5000)));
        probe->setFilterHpEnabled(probeObj.value(QStringLiteral("hpEnabled")).toBool(false));
        probe->setFilterHpCutoffHz(static_cast<float>(probeObj.value(QStringLiteral("hpCutoff")).toInt(100)));
    }
    return true;
}
