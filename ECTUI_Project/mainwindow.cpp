#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "devicemanager.h"
#include "probeconfigdialog.h"

#include <QCloseEvent>
#include <QDialog>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QTimer>

#include <cmath>
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief 构造主窗口
 * @param parent 父控件，一般为 nullptr
 *
 * 创建 Ui 布局、DeviceManager，并调用 setupUI() 完成界面与图表初始化。
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_deviceManager(new DeviceManager(this))        // 初始化设备管理器
    , m_probeManager(new ProbeManager(this))          // 初始化探头管理器
    , m_saveManager(new SaveManager(m_probeManager, this)) // 初始化数据保存管理器
    , m_acquisitionThread(new DataAcquisitionThread(m_deviceManager, m_probeManager, this))
{
    ui->setupUi(this);
    m_probeManager->setProbeCount(8);  // 默认 8 个探头通道

    // 尝试加载默认配置文件
    {
        const QString defaultPath = ProbeConfigDialog::defaultConfigFilePath();
        if (QFileInfo::exists(defaultPath)) {
            ProbeConfigDialog::loadProbeConfigFromFile(m_probeManager, defaultPath);
        }
    }

    setupUI();
}

/**
 * @brief 析构主窗口，释放 ui 资源
 */
MainWindow::~MainWindow()
{
    for (auto &c : m_probeCurves) {
        delete c.impedance;
        delete c.amplitude;
        delete c.phase;
    }
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 关闭前自动保存当前探头配置为默认配置文件
    const QString configPath = ProbeConfigDialog::defaultConfigFilePath();
    ProbeConfigDialog::saveProbeConfigToFile(m_probeManager, configPath);
    QMainWindow::closeEvent(event);
}

/**
 * @brief 搭建主界面整体结构
 *
 * 设置窗口标题与暗色主题样式，按三行布局依次调用 setupFirstRow、setupSecondRow、
 * setupThirdRow，再初始化 Tab/Stack 菜单、图表与信号槽，最后最大化显示。
 */
void MainWindow::setupUI()
{
    // 设置窗口基本属性
    setWindowTitle(tr("ECT 涡流检测上位机"));
    // setMinimumSize(1200, 900);
    // resize(1400, 1000);

    // // 设置窗口启动时最大化
    // showMaximized();
    // this->showFullScreen();

    // 设置VS Code Modern Dark主题样式
    setStyleSheet(
        "QMainWindow {"
        "    background-color: #1e1e1e;"
        "    color: #cccccc;"
        "}"
        "QWidget {"
        "    background-color: #1e1e1e;"
        "    color: #cccccc;"
        "    border: none;"
        "}"
        "QFrame {"
        "    background-color: #252526;"
        "    border: 1px solid #3c3c3c;"
        "    border-radius: 3px;"
        "}"
        "QLabel {"
        "    background-color: #2d2d30;"
        "    color: rgba(147, 145, 145, 1);"
        "    border: 1px solid #3c3c3c;"
        "    padding: 5px;"
        "    border-radius: 3px;"
        "}"
        "QPushButton {"
        "    background-color: #0e639c;"
        "    color: white;"
        "    border: 1px solid #3c3c3c;"
        "    padding: 8px 16px;"
        "    border-radius: 3px;"
        "    font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "    background-color: #1177bb;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #094771;"
        "}"
        "QTabWidget::pane {"
        "    border: 1px solid #3c3c3c;"
        "    background-color: #252526;"
        "}"
        "QTabBar {"
        "    qproperty-expanding: true;"
        "}"
        "QTabBar::tab {"
        "    background-color: #2d2d30;"
        "    color: #cccccc;"
        "    border: 1px solid #3c3c3c;"
        "    padding: 15px 30px;"
        "    margin-right: 2px;"
        "    font-size: 30px;"
        "    font-weight: bold;"
        "    min-height: 30px;"
        "    min-width: 180px;"
        "}"
        "QTabBar::tab:selected {"
        "    background-color: #0e639c;"
        "    color: white;"
        "}"
        "QTabBar::tab:hover {"
        "    background-color: #1e1e1e;"
        "}"

        "QPushButton:hover {"
        "   background: #0088ff;"
        "   border-color: #0066cc;"
        "}"
    );

    // 创建中央控件和主布局 - 3行纵向布局
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setSpacing(0);
    m_mainLayout->setContentsMargins(1, 1, 1, 1);

    // 隐藏默认的菜单栏，我们使用自定义的Tab界面
    ui->menubar->setVisible(false);

    // 第一行：参数详情显示 + 模式信息
    setupFirstRow();

    // 第二行：四个绘图/控制区域
    setupSecondRow();

    // 第三行：连接状态 + 四个菜单分栏
    setupThirdRow();

    // 设置3行的拉伸比例
    m_mainLayout->setStretchFactor(m_mainLayout->itemAt(0)->widget(), 0); // 第一行不拉伸
    m_mainLayout->setStretchFactor(m_mainLayout->itemAt(1)->widget(), 1); // 第二行占主要空间
    m_mainLayout->setStretchFactor(m_mainLayout->itemAt(2)->widget(), 0); // 第三行不拉伸

    setupTabContents();
    setupStackMenuContents();

    // 初始化图表和连接信号槽
    initializePlots();

    setupConnections();

    // 更新参数显示
    updateParameterDisplay();
    updateProbeParameterDisplay();
    updateDeviceConnectionStatusText();

    showMaximized();

    // auto hwnd = reinterpret_cast<HWND>(winId());
    // setTitleBarColor(hwnd, RGB(67, 67, 68));   // 深灰标题栏
}

/**
 * @brief 构建第一行：参数详情区 + 模式信息区
 *
 * 12 个参数标签以 3×4 网格展示；右侧固定宽度显示检测模式（如 Reflection Mode）。
 */
void MainWindow::setupFirstRow()
{
    // 第一行：参数详情显示 + 模式信息
    QFrame *firstRowFrame = new QFrame(this);
    firstRowFrame->setFrameStyle(QFrame::Box);
    firstRowFrame->setFixedHeight(152);
    // firstRowFrame->setStyleSheet("QFrame{"
    //                            " border: 1px solid #eeeeee;"
    //                            "}"); // 2 像素 实线

    QHBoxLayout *firstRowLayout = new QHBoxLayout(firstRowFrame);
    firstRowLayout->setSpacing(0);      // 参数区和模式信息区之间的间距
    firstRowLayout->setContentsMargins(1, 1, 1, 1);  // 第一行内部边距

    // 参数详情显示区域
    m_parameterDisplayFrame = new QFrame(this);
    m_parameterDisplayFrame->setFrameStyle(QFrame::Box);
    m_parameterDisplayFrame->setStyleSheet("QFrame{"
                               " border: 1px solid #eeeeee;"
                               "}"); // 2 像素 实线
    m_parameterDisplayLayout = new QGridLayout(m_parameterDisplayFrame);
    m_parameterDisplayLayout->setSpacing(1);           // 参数标签之间的间距
    m_parameterDisplayLayout->setContentsMargins(1, 1, 1, 1);  // 参数区内部边距

    // 创建参数显示标签 - 参照ECT_UI.png的布局
    m_drivingFreqLabel = new QLabel(tr("Driving Freq: 1.000kHz"), this);
    m_refCurrentLabel = new QLabel(tr("Ref Current: 0.00mA"), this);
    m_driveCurrentLabel = new QLabel(tr("Drive Current: 100.00mA"), this);
    m_rotationAngleLabel = new QLabel(tr("Rotation Angle: -74.0dg"), this);

    m_acquisitionFreqLabel = new QLabel(tr("Acquisition Freq: 50Hz"), this);
    m_preGainLabel = new QLabel(tr("Pre Gain: 10dB"), this);
    m_postGainLabel = new QLabel(tr("Post Gain: 30dB"), this);
    m_alarmLabel = new QLabel(tr("Alarm Disabled"), this);

    m_realImaginaryLabel = new QLabel(tr("Real: 1.0 Imaginary: 1"), this);
    m_digFilterLabel = new QLabel(tr("Dig Filter:No Filter"), this);
    m_autoEraseLabel = new QLabel(tr("Auto erase after 300000 points"), this);
    m_shiftLabel = new QLabel(tr("Shift X/Y:-1000/-372mV"), this);

    // 设置字体和颜色
    QFont paramFont;
    paramFont.setPointSize(9);
    QList<QLabel*> paramLabels = {m_drivingFreqLabel, m_refCurrentLabel, m_driveCurrentLabel, m_rotationAngleLabel,
                                  m_acquisitionFreqLabel, m_preGainLabel, m_postGainLabel, m_alarmLabel,
                                  m_realImaginaryLabel, m_digFilterLabel, m_autoEraseLabel, m_shiftLabel};

    for (QLabel* label : paramLabels) {
        label->setFont(paramFont);
        // label->setStyleSheet("QLabel { color: white; background-color: #2b2b2b; padding: 5px; border: 1px solid #555; font-weight: bold; text-align: center; font-size: 30px}");
        label->setStyleSheet("QLabel { color: rgba(147, 145, 145, 1); background-color: #2b2b2b; padding: 5px; border: 1px solid #555; font-weight: bold; text-align: center; font-size: 20px}");
        label->setMinimumSize(150, 48);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        label->setAlignment(Qt::AlignCenter);
    }

    // 布局参数标签 - 3行4列
    m_parameterDisplayLayout->addWidget(m_drivingFreqLabel, 0, 0);
    m_parameterDisplayLayout->addWidget(m_refCurrentLabel, 0, 1);
    m_parameterDisplayLayout->addWidget(m_driveCurrentLabel, 0, 2);
    m_parameterDisplayLayout->addWidget(m_rotationAngleLabel, 0, 3);

    m_parameterDisplayLayout->addWidget(m_acquisitionFreqLabel, 1, 0);
    m_parameterDisplayLayout->addWidget(m_preGainLabel, 1, 1);
    m_parameterDisplayLayout->addWidget(m_postGainLabel, 1, 2);
    m_parameterDisplayLayout->addWidget(m_alarmLabel, 1, 3);

    m_parameterDisplayLayout->addWidget(m_realImaginaryLabel, 2, 0);
    m_parameterDisplayLayout->addWidget(m_digFilterLabel, 2, 1);
    m_parameterDisplayLayout->addWidget(m_autoEraseLabel, 2, 2);
    m_parameterDisplayLayout->addWidget(m_shiftLabel, 2, 3);

    // 模式信息显示区域
    m_modeInfoFrame = new QFrame(this);
    m_modeInfoFrame->setFrameStyle(QFrame::Box);
    m_modeInfoFrame->setFixedWidth(284);
    m_modeInfoLayout = new QVBoxLayout(m_modeInfoFrame);
    m_modeInfoLayout->setContentsMargins(QMargins(2,2,2,2));

    // m_modeInfoLabel = new QLabel(tr("Reflection\nMode"), this);
    m_modeInfoLabel = new QLabel(this);
    m_modeInfoLabel->setAlignment(Qt::AlignCenter);
    QFont modeFont;
    modeFont.setPointSize(10);
    modeFont.setBold(true);
    m_modeInfoLabel->setFont(modeFont);
    m_modeInfoLabel->setStyleSheet("QLabel { color: #f14c4c; background-color: #252526; border: 1px solid #3c3c3c; padding: 10px; border-radius: 3px; font-size: 50px; text-align: center; }");
    m_modeInfoLayout->addWidget(m_modeInfoLabel);

    // 添加到第一行布局：参数详情占大部分，模式信息占小部分
    firstRowLayout->addWidget(m_parameterDisplayFrame, 4); // 占4/5宽度
    firstRowLayout->addWidget(m_modeInfoFrame, 1);         // 占1/5宽度

    m_mainLayout->addWidget(firstRowFrame);
}

/**
 * @brief 构建第二行：绘图区与右侧控制区
 *
 * 左上/右上为阻抗平面图与 A 扫时序图，左下为预留波形区；右侧为 StackedWidget
 * 子菜单、模式标签及虚拟方向键与确认/取消按钮。
 */
void MainWindow::setupSecondRow()
{
    // 第二行：四个绘图/控制区域
    m_middleFrame = new QFrame(this);
    m_middleFrame->setFrameStyle(QFrame::Box);
    m_middleFrame->setContentsMargins(QMargins(1,1,1,1));

    // m_middleFrame_border = new QFrame(this);
    // m_middleLayout_border = new QVBoxLayout(m_middleFrame_border);
    m_middleLayout_border = new QHBoxLayout(m_middleFrame);

    m_plotArea_frame1 = new QFrame();

    m_middleLayout = new QGridLayout(m_plotArea_frame1);
    m_middleLayout->setSpacing(1);      // 绘图区域之间的间距
    m_middleLayout->setContentsMargins(1, 1, 1, 1);  // 中间区域内部边距

    // 绘图区1（左上）
    m_plotArea1 = new QFrame(this);
    m_plotArea1->setFrameStyle(QFrame::Box);
    m_plotArea1->setStyleSheet("QFrame{"
                               " border-left: 1px solid #eeeeee;"
                               " border-bottom: 1px solid #eeeeee;"
                               "}"); // 2 像素 红色实线
    m_plotArea1Layout = new QVBoxLayout(m_plotArea1);
    m_plotArea1Layout->setContentsMargins(0, 0, 0, 0);
    m_plotArea1Layout->setSpacing(0);
    // m_plot1 = new QCustomPlot(this);
    m_plot1 = new customplot(this);
    m_plot1->setMinimumSize(400, 400);
    m_plot1->setInteraction(QCP::iRangeZoom, false);
    m_plot1->xAxis2->setVisible(false);
    m_plot1->yAxis2->setVisible(false);
    m_plotArea1Layout->addWidget(m_plot1);

    // 绘图区2（右上）
    m_plotArea2Frame = new QFrame(this);
    m_plotArea2Frame->setFrameStyle(QFrame::Box);
    m_plotArea2Frame->setStyleSheet("QFrame{"
                               " border-left: 1px solid #eeeeee;"
                               " border-bottom: 1px solid #eeeeee;"
                               " border-right: 1px solid #eeeeee;"
                               "}"); // 2 像素 红色实线
    m_plotArea2Layout = new QVBoxLayout(m_plotArea2Frame);
    m_plotArea2Layout->setContentsMargins(0, 0, 0, 0);
    m_plotArea2Layout->setSpacing(0);

    m_plot2 = new QCustomPlot(this);
    m_plot2->setMinimumSize(400, 400);
    m_plot2->setInteraction(QCP::iRangeZoom, false);
    // m_plot2->yAxis->setVisible(false);
    m_plotArea2Layout->addWidget(m_plot2);

    // 绘图区3（左下）
    m_plotArea3 = new QFrame(this);
    m_plotArea3->setFrameStyle(QFrame::Box);
    m_plotArea3->setStyleSheet("QFrame{"
                               " border: 1px solid #eeeeee;"
                               // " border-bottom: 1px solid #eeeeee;"
                               "}"); // 2 像素 红色实线
    m_plotArea3Layout = new QVBoxLayout(m_plotArea3);
    m_plotArea3Layout->setContentsMargins(0, 0, 0, 0);
    m_plotArea3Layout->setSpacing(0);
    m_plot3 = new QCustomPlot(this);
    m_plot3->setMinimumSize(400, m_plot3_heigh-2);
    m_plotArea3Layout->addWidget(m_plot3);

    // 控制按键区域（右下）
    m_controlFrame = new QFrame(this);
    m_controlFrame->setFrameStyle(QFrame::Box);
    m_controlFrame->setFixedWidth(280);
    m_controlLayout = new QVBoxLayout(m_controlFrame);

    // 确认和取消按钮
    m_confirmCancelFrame = new QFrame(this);
    m_confirmCancelLayout = new QHBoxLayout(m_confirmCancelFrame);

    m_confirmBtn = new QPushButton(tr("✓"), this);
    m_cancelBtn = new QPushButton(tr("✗"), this);

    m_confirmBtn->setFixedSize(60, 60);
    m_cancelBtn->setFixedSize(60, 60);
    m_confirmBtn->setStyleSheet("QPushButton { font-size: 50px; background-color: #16825d; color: white; border: 1px solid #3c3c3c; border-radius: 5px; } QPushButton:hover { background-color: #1e9b6a; }");
    m_cancelBtn->setStyleSheet("QPushButton { font-size: 50px; background-color: #d73a49; color: white; border: 1px solid #3c3c3c; border-radius: 5px; } QPushButton:hover { background-color: #e55566; }");

    m_confirmCancelLayout->addWidget(m_confirmBtn);
    m_confirmCancelLayout->addWidget(m_cancelBtn);
    m_confirmCancelLayout->setSpacing(100);
    m_confirmCancelLayout->addStretch();

    // 虚拟方向按键
    m_virtualButtonFrame = new QFrame(this);
    m_virtualButtonLayout = new QGridLayout(m_virtualButtonFrame);
    m_virtualButtonLayout->setSpacing(5);

    m_upBtn = new QPushButton(tr("↑"), this);
    m_downBtn = new QPushButton(tr("↓"), this);
    m_leftBtn = new QPushButton(tr("←"), this);
    m_rightBtn = new QPushButton(tr("→"), this);

    // 设置按钮样式
    QList<QPushButton*> virtualBtns = {m_upBtn, m_downBtn, m_leftBtn, m_rightBtn};
    for (QPushButton* btn : virtualBtns) {
        btn->setFixedSize(70, 60);
        btn->setStyleSheet("QPushButton { font-size: 50px; font-weight: bold; }");
    }

    // 布局虚拟按键，形成十字形状
    m_virtualButtonLayout->addWidget(m_upBtn, 0, 1);
    m_virtualButtonLayout->addWidget(m_leftBtn, 1, 0);
    m_virtualButtonLayout->addWidget(m_rightBtn, 1, 2);
    m_virtualButtonLayout->addWidget(m_downBtn, 2, 1);

    // 添加控制区域布局
    m_controlLayout->addStretch();
    m_controlLayout->addWidget(m_confirmCancelFrame);
    m_controlLayout->addWidget(m_virtualButtonFrame);
    m_controlLayout->setSpacing(200);
    m_controlLayout->addStretch();
    // m_controlLayout->addStretch(1);

    // 设置网格布局的行列拉伸比例
    m_middleLayout->setRowStretch(0, 3); // 上排占3倍高度
    m_middleLayout->setColumnStretch(0, 4); // 左列占4倍宽度
    m_middleLayout->setColumnStretch(1, 4); // 中列占4倍宽度
    // m_middleLayout->setColumnStretch(2, 1); // 右列占1倍宽度

    // 添加四个区域到网格布局
    m_middleLayout->addWidget(m_plotArea1, 0, 0);
    m_middleLayout->addWidget(m_plotArea2Frame, 0, 1);
    m_middleLayout->setSpacing(5);
    // m_middleLayout->addWidget(m_controlFrame, 0, 2);



    // 绘图区
    m_plotArea_frame = new QFrame();
    m_middleLayout_1 = new QVBoxLayout(m_plotArea_frame); //

    stack_menu_frame = new QFrame();
    stack_menu_frame->setFixedWidth(180);
    stack_menu_frame->setFrameStyle(QFrame::Box);

    QVBoxLayout* stackFrameLayout = new QVBoxLayout(stack_menu_frame);
    stackFrameLayout->setContentsMargins(0, 0, 0, 0);

    stack_menu = new QStackedWidget();
    stack_menu->setFixedWidth(180);

    // m_modeInfoFrame2 = new QFrame(this);
    // m_modeInfoFrame2->setFrameStyle(QFrame::Box);
    // m_modeInfoFrame2->setFixedHeight(250);
    // m_modeInfoFrame2->setFixedWidth(180);
    // m_modeInfoLayout2 = new QVBoxLayout(m_modeInfoFrame2);

    // 模式信息显示
    m_modeInfoLabel2 = new QLabel(tr("Reflection\nMode"), this);
    m_modeInfoLabel2->setAlignment(Qt::AlignCenter);
    m_modeInfoLabel2->setFixedWidth(178);
    m_modeInfoLabel2->setFixedHeight(150);
    QFont modeFont;
    modeFont.setPointSize(10);
    modeFont.setBold(true);
    m_modeInfoLabel2->setFont(modeFont);
    m_modeInfoLabel2->setStyleSheet("QLabel { color: #f14c4c; background-color: #252526; border: 1px solid #3c3c3c; padding: 10px; border-radius: 3px; font-size: 30px; text-align: center; }");
    stackFrameLayout->addWidget(m_modeInfoLabel2);

    stackFrameLayout->addWidget(stack_menu);

    m_middleLayout_1->addWidget(m_plotArea_frame1);  // 绘图区1和2
    m_middleLayout_1->addWidget(m_plotArea3);        // 绘图区3

    m_middleLayout_1->setContentsMargins(QMargins(2,2,2,2));

    m_middleLayout_border->addWidget(m_plotArea_frame);
    m_middleLayout_border->addWidget(stack_menu_frame);
    m_middleLayout_border->addWidget(m_controlFrame);
    m_middleLayout_border->setContentsMargins(QMargins(4,4,4,4));
    // m_plotArea3->setFixedHeight(150);
    m_plotArea3->setFixedHeight(m_plot3_heigh);

    m_mainLayout->addWidget(m_middleFrame);
}

/**
 * @brief 构建第三行：连接状态栏 + 底部 Tab 菜单
 *
 * 显示本机 IP 与下位机连接状态（支持双击交互）；底部 Main/File/Scanners/Parameters 四个分栏。
 */
void MainWindow::setupThirdRow()
{
    // 第三行：连接状态 + 四个菜单分栏
    QFrame *thirdRowFrame = new QFrame(this);
    thirdRowFrame->setFrameStyle(QFrame::Box);

    QVBoxLayout *thirdRowLayout = new QVBoxLayout(thirdRowFrame);
    thirdRowLayout->setSpacing(2);      // 连接状态和Tab之间的间距
    thirdRowLayout->setContentsMargins(2, 2, 2, 2);  // 第三行内部边距

    // 设备连接状态显示
    m_connectionStatusFrame = new QFrame(this);
    m_connectionStatusFrame->setFrameStyle(QFrame::Box);
    m_connectionStatusFrame->setFixedHeight(60);
    m_connectionStatusLayout = new QHBoxLayout(m_connectionStatusFrame);

    m_connectionStatusLabel = new QLabel(tr("设备连接状态显示:"), this);
    m_connectionStatusLabel->setStyleSheet("QLabel { color: #ffcc02; background-color: #252526; border: 1px solid #3c3c3c; font-weight: bold; padding: 5px; font-size: 20px; border-radius: 3px; }");
    m_connectionStatusLabel->setCursor(Qt::PointingHandCursor);
    m_connectionStatusLabel->setToolTip(tr("双击选择要显示的以太网 IP"));
    m_connectionStatusLabel->installEventFilter(this);
    m_connectionStatusLayout->addWidget(m_connectionStatusLabel);
    // m_connectionStatusLayout->setMargin(2);
    m_connectionStatusLayout->setSpacing(2);

    m_connectionStatusLabel2 = new QLabel(tr("SSEC Board usb not connected"), this);
    m_connectionStatusLabel2->setStyleSheet("QLabel { color: #ffcc02; background-color: #252526; border: 1px solid #3c3c3c; font-weight: bold; padding: 5px; font-size: 20px; border-radius: 3px; }");
    m_connectionStatusLabel2->setCursor(Qt::PointingHandCursor);
    m_connectionStatusLabel2->setToolTip(tr("双击设置下位机 IP 和端口并连接"));
    m_connectionStatusLabel2->installEventFilter(this);
    m_connectionStatusLayout->addWidget(m_connectionStatusLabel2);

    // 设置连接状态布局间距
    m_connectionStatusLayout->setSpacing(8);        // 标签之间间距
    m_connectionStatusLayout->setContentsMargins(8, 5, 8, 5);  // 连接状态区内部边距

    m_connectionStatusLayout->addStretch(2);

    // 底部四个菜单分栏
    m_bottomTabWidget = new QTabWidget(this);
    m_bottomTabWidget->setMaximumHeight(70);

    // 创建四个Tab页面
    m_mainTab = new QWidget();
    m_fileTab = new QWidget();
    m_scannersTab = new QWidget();
    m_parametersTab = new QWidget();

    // 设置Tab标题和样式
    m_bottomTabWidget->addTab(m_mainTab, tr("Main"));
    m_bottomTabWidget->addTab(m_fileTab, tr("File"));
    m_bottomTabWidget->addTab(m_scannersTab, tr("Scanners"));
    m_bottomTabWidget->addTab(m_parametersTab, tr("Parameters"));

    // 设置Parameters为默认选中（参照图片中的蓝色高亮）
    m_bottomTabWidget->setCurrentIndex(3);

    // 设置TabBar的文字显示模式，确保文字不被截断
    m_bottomTabWidget->tabBar()->setElideMode(Qt::ElideNone);
    m_bottomTabWidget->tabBar()->setUsesScrollButtons(false);

    // 添加到第三行布局
    thirdRowLayout->addWidget(m_connectionStatusFrame);
    thirdRowLayout->addWidget(m_bottomTabWidget);

    m_mainLayout->addWidget(thirdRowFrame);
}


/**
 * @brief 为底部 QTabWidget 各页填充按钮（当前已禁用）
 *
 * 功能已迁移至 setupStackMenuContents() 的 StackedWidget；保留空实现以便日后恢复 Tab 内按钮布局。
 */
void MainWindow::setupTabContents()
{
/*
    // Main Tab 内容
    m_mainTabLayout = new QVBoxLayout(m_mainTab);
    m_mainButtonLayout = new QGridLayout();

    m_startAcquisitionBtn = new QPushButton(tr("开始采集"), this);
    m_clearAlarmsBtn = new QPushButton(tr("清除警报"), this);
    m_autoZeroBtn = new QPushButton(tr("自动清零"), this);
    m_autoCalibrateBtn = new QPushButton(tr("自动校准"), this);
    m_clearPhaseWindowBtn = new QPushButton(tr("清除相位窗口"), this);
    m_moreMainBtn = new QPushButton(tr("更多..."), this);

    m_mainButtonLayout->addWidget(m_startAcquisitionBtn, 0, 0);
    m_mainButtonLayout->addWidget(m_clearAlarmsBtn, 0, 1);
    m_mainButtonLayout->addWidget(m_autoZeroBtn, 1, 0);
    m_mainButtonLayout->addWidget(m_autoCalibrateBtn, 1, 1);
    m_mainButtonLayout->addWidget(m_clearPhaseWindowBtn, 2, 0);
    m_mainButtonLayout->addWidget(m_moreMainBtn, 2, 1);

    m_mainTabLayout->addLayout(m_mainButtonLayout);

    // File Tab 内容
    m_fileTabLayout = new QVBoxLayout(m_fileTab);
    m_fileButtonLayout = new QGridLayout();

    m_loadDataBtn = new QPushButton(tr("加载数据"), this);
    m_saveDataBtn = new QPushButton(tr("保存数据"), this);
    m_loadConfigBtn = new QPushButton(tr("加载配置"), this);
    m_saveDefaultConfigBtn = new QPushButton(tr("保存默认配置"), this);
    m_moreFileBtn = new QPushButton(tr("更多..."), this);

    m_fileButtonLayout->addWidget(m_loadDataBtn, 0, 0);
    m_fileButtonLayout->addWidget(m_saveDataBtn, 0, 1);
    m_fileButtonLayout->addWidget(m_loadConfigBtn, 1, 0);
    m_fileButtonLayout->addWidget(m_saveDefaultConfigBtn, 1, 1);
    m_fileButtonLayout->addWidget(m_moreFileBtn, 2, 0);

    m_fileTabLayout->addLayout(m_fileButtonLayout);

    // Scanners Tab 内容
    m_scannersTabLayout = new QVBoxLayout(m_scannersTab);
    m_scannersButtonLayout = new QGridLayout();

    m_connectDisconnectBtn = new QPushButton(tr("连接/断开"), this);
    m_showRealtimePositionBtn = new QPushButton(tr("实时位置显示"), this);

    // 暂时禁用扫查功能
    m_connectDisconnectBtn->setEnabled(false);
    m_showRealtimePositionBtn->setEnabled(false);

    m_scannersButtonLayout->addWidget(m_connectDisconnectBtn, 0, 0);
    m_scannersButtonLayout->addWidget(m_showRealtimePositionBtn, 0, 1);

    m_scannersTabLayout->addLayout(m_scannersButtonLayout);

    // Parameters Tab 内容
    m_parametersTabLayout = new QVBoxLayout(m_parametersTab);
    m_parametersButtonLayout = new QGridLayout();

    m_setExcitationFreqBtn = new QPushButton(tr("Driving\nFrequency"), this);
    m_setSampleRateBtn = new QPushButton(tr("Acquisition\nFrequency"), this);
    m_setPreGainBtn = new QPushButton(tr("Pre Gain"), this);
    m_setPostGainBtn = new QPushButton(tr("Post Gain"), this);
    m_setRotationAngleBtn = new QPushButton(tr("Rotation Angle"), this);
    m_moreParametersBtn = new QPushButton(tr("More..."), this);

    // 设置按钮样式，参照图片中的黑色按钮
    QList<QPushButton*> paramBtns = {m_setExcitationFreqBtn, m_setSampleRateBtn, m_setPreGainBtn,
                                    m_setPostGainBtn, m_setRotationAngleBtn, m_moreParametersBtn};
    for (QPushButton* btn : paramBtns) {
        btn->setStyleSheet("QPushButton { background-color: #3a3a3a; color: white; border: 1px solid #5a5a5a; padding: 10px; }");
        btn->setMinimumHeight(50);
    }

    m_parametersButtonLayout->addWidget(m_setExcitationFreqBtn, 0, 0);
    m_parametersButtonLayout->addWidget(m_setSampleRateBtn, 1, 0);
    m_parametersButtonLayout->addWidget(m_setPreGainBtn, 2, 0);
    m_parametersButtonLayout->addWidget(m_setPostGainBtn, 3, 0);
    m_parametersButtonLayout->addWidget(m_setRotationAngleBtn, 4, 0);
    m_parametersButtonLayout->addWidget(m_moreParametersBtn, 5, 0);

    m_parametersTabLayout->addLayout(m_parametersButtonLayout);

    */

}

/**
 * @brief 创建右侧 StackedWidget 的四个功能页
 *
 * 分别对应 Main（采集/校准）、File（数据/配置）、Scanners（连接/位置）、
 * Parameters（激励/采样/增益/旋转角等），默认显示 Parameters 页（索引 3）。
 */
void MainWindow::setupStackMenuContents()
{
    // 创建Main页面
    m_stackMainPage = new QWidget();
    m_stackMainLayout = new QVBoxLayout(m_stackMainPage);
    m_stackMainLayout->setContentsMargins(10, 10, 10, 10);
    m_stackMainLayout->setSpacing(5);

    // 添加顶部弹簧用于垂直居中
    m_stackMainLayout->addStretch();

    // 创建Main页面的按钮
    m_stackStartAcquisitionBtn = new QPushButton(tr("开始\n采集"), this);
    m_stackClearAlarmsBtn = new QPushButton(tr("清除\n警报"), this);
    m_stackAutoZeroBtn = new QPushButton(tr("自动\n清零"), this);
    m_stackAutoCalibrateBtn = new QPushButton(tr("自动\n校准"), this);
    m_stackClearPhaseWindowBtn = new QPushButton(tr("清除相位\n窗口"), this);
    m_stackMoreMainBtn = new QPushButton(tr("更多..."), this);

    // 设置按钮样式
    QList<QPushButton*> mainStackBtns = {m_stackStartAcquisitionBtn, m_stackClearAlarmsBtn,
                                        m_stackAutoZeroBtn, m_stackAutoCalibrateBtn,
                                        m_stackClearPhaseWindowBtn, m_stackMoreMainBtn};
    for (QPushButton* btn : mainStackBtns) {
        btn->setFixedHeight(80);
        btn->setStyleSheet("QPushButton { background-color: #0e639c; color: white; border: 1px solid #3c3c3c; "
                          "padding: 5px; border-radius: 3px; font-weight: bold; font-size: 20px; } QPushButton:hover { background-color: #1e9b6a; }");
        m_stackMainLayout->addWidget(btn);
    }

    // 添加底部弹簧用于垂直居中
    m_stackMainLayout->addStretch();

    // 创建File页面
    m_stackFilePage = new QWidget();
    m_stackFileLayout = new QVBoxLayout(m_stackFilePage);
    m_stackFileLayout->setContentsMargins(5, 5, 5, 5);
    m_stackFileLayout->setSpacing(5);

    m_stackFileLayout->addStretch();

    m_stackLoadDataBtn = new QPushButton(tr("加载\n数据"), this);
    m_stackSaveDataBtn = new QPushButton(tr("保存\n数据"), this);
    m_stackLoadConfigBtn = new QPushButton(tr("加载\n配置"), this);
    m_stackSaveDefaultConfigBtn = new QPushButton(tr("保存默认\n配置"), this);
    m_stackMoreFileBtn = new QPushButton(tr("更多..."), this);

    QList<QPushButton*> fileStackBtns = {m_stackLoadDataBtn, m_stackSaveDataBtn,
                                        m_stackLoadConfigBtn, m_stackSaveDefaultConfigBtn,
                                        m_stackMoreFileBtn};
    for (QPushButton* btn : fileStackBtns) {
        btn->setFixedHeight(80);
        btn->setStyleSheet("QPushButton { background-color: #0e639c; color: white; border: 1px solid #3c3c3c; "
                          "padding: 5px; border-radius: 3px; font-weight: bold; font-size: 20px; } QPushButton:hover { background-color: #1e9b6a; }");
        m_stackFileLayout->addWidget(btn);
    }

    m_stackFileLayout->addStretch();

    // 创建Scanners页面
    m_stackScannersPage = new QWidget();
    m_stackScannersLayout = new QVBoxLayout(m_stackScannersPage);
    m_stackScannersLayout->setContentsMargins(5, 5, 5, 5);
    m_stackScannersLayout->setSpacing(5);

    m_stackScannersLayout->addStretch();

    m_stackConnectDisconnectBtn = new QPushButton(tr("连接/\n断开"), this);
    m_stackShowRealtimePositionBtn = new QPushButton(tr("实时位置\n显示"), this);

    // 暂时禁用扫查功能
    // m_stackConnectDisconnectBtn->setEnabled(false);
    // m_stackShowRealtimePositionBtn->setEnabled(false);

    QList<QPushButton*> scannersStackBtns = {m_stackConnectDisconnectBtn, m_stackShowRealtimePositionBtn};
    for (QPushButton* btn : scannersStackBtns) {
        btn->setFixedHeight(80);
        btn->setStyleSheet("QPushButton { background-color: #0e639c; color: white; border: 1px solid #3c3c3c; "
                          "padding: 5px; border-radius: 3px; font-weight: bold; font-size: 20px; } QPushButton:hover { background-color: #1e9b6a; }");
        m_stackScannersLayout->addWidget(btn);
    }

    m_stackScannersLayout->addStretch();

    // 创建Parameters页面
    m_stackParametersPage = new QWidget();
    m_stackParametersLayout = new QVBoxLayout(m_stackParametersPage);
    m_stackParametersLayout->setContentsMargins(5, 5, 5, 5);
    m_stackParametersLayout->setSpacing(5);

    m_stackParametersLayout->addStretch();

    m_stackSetExcitationFreqBtn = new QPushButton(tr("Driving\nFrequency"), this);
    m_stackSetSampleRateBtn = new QPushButton(tr("Acquisition\nFrequency"), this);
    m_stackSetPreGainBtn = new QPushButton(tr("Pre Gain"), this);
    m_stackSetPostGainBtn = new QPushButton(tr("Post Gain"), this);
    m_stackSetRotationAngleBtn = new QPushButton(tr("Rotation\nAngle"), this);
    m_stackMoreParametersBtn = new QPushButton(tr("More..."), this);

    QList<QPushButton*> parametersStackBtns = {m_stackSetExcitationFreqBtn, m_stackSetSampleRateBtn,
                                              m_stackSetPreGainBtn, m_stackSetPostGainBtn,
                                              m_stackSetRotationAngleBtn, m_stackMoreParametersBtn};
    for (QPushButton* btn : parametersStackBtns) {
        btn->setFixedHeight(80);
        btn->setStyleSheet("QPushButton { background-color: #0e639c; color: white; border: 1px solid #3c3c3c; "
                          "padding: 5px; border-radius: 3px; font-weight: bold; font-size: 20px; } QPushButton:hover { background-color: #1e9b6a; }");
        m_stackParametersLayout->addWidget(btn);
    }

    m_stackParametersLayout->addStretch();

    // 将四个页面添加到StackedWidget
    stack_menu->addWidget(m_stackMainPage);      // 索引0 - Main
    stack_menu->addWidget(m_stackFilePage);      // 索引1 - File
    stack_menu->addWidget(m_stackScannersPage);  // 索引2 - Scanners
    stack_menu->addWidget(m_stackParametersPage); // 索引3 - Parameters

    // 设置默认显示Parameters页面（与底部Tab保持一致）
    stack_menu->setCurrentIndex(3);
}

/**
 * @brief 绑定 UI 控件与 DeviceManager 信号槽
 *
 * 连接虚拟按键、底部 Tab 切换、plot1 坐标轴变化（更新参考圆）、设备连接状态与错误提示。
 */
void MainWindow::setupConnections()
{
    // 虚拟按键连接
    connect(m_upBtn, &QPushButton::clicked, this, &MainWindow::onVirtualUpClicked);
    connect(m_downBtn, &QPushButton::clicked, this, &MainWindow::onVirtualDownClicked);
    connect(m_leftBtn, &QPushButton::clicked, this, &MainWindow::onVirtualLeftClicked);
    connect(m_rightBtn, &QPushButton::clicked, this, &MainWindow::onVirtualRightClicked);
    connect(m_confirmBtn, &QPushButton::clicked, this, &MainWindow::onConfirmClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancelClicked);

    // 底部Tab切换连接
    connect(m_bottomTabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    // 采样率设置按钮
    // connect(m_setSampleRateBtn, &QPushButton::clicked,
    //         this, &MainWindow::onSetSampleRateClicked);
    connect(m_stackSetSampleRateBtn, &QPushButton::clicked,
            this, &MainWindow::onSetSampleRateClicked);

    // 探头参数配置按钮
    connect(m_stackMoreParametersBtn, &QPushButton::clicked,
            this, &MainWindow::onMoreParametersClicked);

    // 连接坐标轴范围变化信号到圆形曲线更新
    connect(m_plot1->xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, &MainWindow::updateCircleCurve);
    connect(m_plot1->yAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, &MainWindow::updateCircleCurve);

    connect(m_deviceManager,
            &DeviceManager::connectionStateChanged,
            this,
            [this](ConnectionState state) {
                updateDeviceConnectionStatusText();

                if (state != ConnectionState::Connected)
                    return;

                // 手动连接设备提示
                if (m_deviceConnectionPending) {
                    m_deviceConnectionPending = false;
                    QMessageBox::information(this,
                                             tr("连接设备成功"),
                                             tr("已成功连接到下位机 %1:%2。")
                                                 .arg(m_deviceHost)
                                                 .arg(m_devicePort));
                }

                // 因用户点击"开始采集"而触发的连接 → 自动开始采集
                if (m_acquisitionPending) {
                    m_acquisitionPending = false;
                    startAcquisition();
                }

                // 连接成功后自动询问开发板信息
                m_deviceManager->queryBoardInfo();
            });

    connect(m_deviceManager,
            &DeviceManager::errorOccurred,
            this,
            [this](const QString &message) {
                updateDeviceConnectionStatusText();

                if (m_deviceConnectionPending) {
                    // 用户主动连接失败 → 弹窗告知
                    m_deviceConnectionPending = false;
                    QMessageBox::warning(this,
                                         tr("连接设备失败"),
                                         tr("连接到下位机 %1:%2 失败：\n%3")
                                             .arg(m_deviceHost)
                                             .arg(m_devicePort)
                                             .arg(message));
                } else {
                    // 非主动连接场景（如采集过程中连接意外断开）
                    qWarning() << "[DeviceManager] 错误:" << message;

                    // 如果正在采集，自动停止
                    if (m_acquisitionThread->isAcquiring()) {
                        m_acquisitionThread->stop();
                        m_plotRefreshTimer->stop();
                        m_saveManager->onAcquisitionStopped();
                        m_stackStartAcquisitionBtn->setText(tr("开始\n采集"));
                    }
                }

                // 因采集触发的连接失败 → 清除标志，不开始采集
                if (m_acquisitionPending) {
                    m_acquisitionPending = false;
                }
            });

    // ── 开发板信息回应（暂由连接对话框中按钮自行处理） ─────
    // connect(m_deviceManager, &DeviceManager::boardInfoReceived,
    //         this, &MainWindow::onBoardInfoReceived);

    // ── 加载/保存探头配置文件 ──────────────────
    connect(m_stackLoadConfigBtn, &QPushButton::clicked, this, [this]() {
        const QString filePath = QFileDialog::getOpenFileName(
            this, tr("加载探头配置"), QString(), tr("JSON 文件 (*.json)"));
        if (filePath.isEmpty()) return;
        if (ProbeConfigDialog::loadProbeConfigFromFile(m_probeManager, filePath)) {
            qDebug() << "[MainWindow] 探头配置已从文件加载:" << filePath;
        }
    });
    connect(m_stackSaveDefaultConfigBtn, &QPushButton::clicked, this, [this]() {
        const QString filePath = QFileDialog::getSaveFileName(
            this, tr("保存探头配置"), QString(), tr("JSON 文件 (*.json)"));
        if (filePath.isEmpty()) return;
        if (ProbeConfigDialog::saveProbeConfigToFile(m_probeManager, filePath)) {
            qDebug() << "[MainWindow] 探头配置已保存到文件:" << filePath;
        }
    });

    // ── 加载采集数据（CSV） ──
    connect(m_stackLoadDataBtn, &QPushButton::clicked,
            this, &MainWindow::onLoadDataClicked);

    // ── SaveManager 相关连接 ─────────────────────

    // 保存按钮：手动设置数据目录
    connect(m_stackSaveDataBtn, &QPushButton::clicked, this, [this]() {
        QString folder = QDir::currentPath() + "/data";
        m_saveManager->setDataFolder(folder);
        qDebug() << "[MainWindow] 数据保存目录:" << folder;
    });

    // 新文件创建提示
    connect(m_saveManager, &SaveManager::newFileCreated, this,
            [this](int probeIndex, const QString &filePath) {
                qDebug() << "[MainWindow] 新建保存文件:" << filePath;
            });

    // 保存错误提示
    connect(m_saveManager, &SaveManager::saveError, this,
            [this](const QString &msg) {
                qWarning() << "[MainWindow] 保存错误:" << msg;
            });

    // ── DataAcquisitionThread → SaveManager 连接 ──
    connect(m_acquisitionThread, &DataAcquisitionThread::saveDataReady,
            m_saveManager, &SaveManager::onSaveDataReady);

    // ── 采集开始/停止按钮（toggle 模式） ──
    connect(m_stackStartAcquisitionBtn, &QPushButton::clicked,
            this, &MainWindow::onStartAcquisitionBtnClicked);

    // ── 采集线程完全退出后复位 UI ──
    connect(m_acquisitionThread, &DataAcquisitionThread::acquisitionStopped, this, [this]() {
        m_plotRefreshTimer->stop();
        m_stackStartAcquisitionBtn->setText(tr("开始\n采集"));
    });

    // ── 自动清零（平衡点）按钮 ──
    connect(m_stackAutoZeroBtn, &QPushButton::clicked,
            this, &MainWindow::onAutoZeroClicked);

    // ── 旋转角度按钮 ──
    connect(m_stackSetRotationAngleBtn, &QPushButton::clicked,
            this, &MainWindow::onRotationAngleClicked);

    // ── 后置增益按钮 ──
    connect(m_stackSetPostGainBtn, &QPushButton::clicked,
            this, &MainWindow::onPostGainClicked);

    // ── 同步探头曲线容器到采集线程（初始 + 探头数变化时重注册） ──
    syncProbeCurves();
    connect(m_probeManager, &ProbeManager::probeCountChanged,
            this, &MainWindow::syncProbeCurves);

    // ── 定时器刷新绘图曲线（~25 fps），启动/停止跟随采集 ──
    m_plotRefreshTimer = new QTimer(this);
    m_plotRefreshTimer->setInterval(20);
    connect(m_plotRefreshTimer, &QTimer::timeout, this, &MainWindow::refreshPlots);
}

/**
 * @brief 初始化三个 QCustomPlot 绘图区的样式与坐标轴
 *
 * plot1：阻抗平面（李萨如图）+ 参考圆；plot2：A 扫时序双 X 轴；plot3：预留波形区。
 * 安装 eventFilter 以在窗口缩放时同步轴偏移与范围。
 */
void MainWindow::initializePlots()
{
    // 初始化绘图区1 - 阻抗平面图（李萨如图）
    // m_plot1->xAxis->setLabel(tr("Real"));
    // m_plot1->yAxis->setLabel(tr("Imaginary"));
    m_plot1->xAxis->setRange(-2100, 2100);
    m_plot1->yAxis->setRange(-2100, 2100);
    // m_plot1->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot1->axisRect()->setupFullAxesBox(true);
    m_plot1->addGraph();
    m_plot1->graph(0)->setScatterStyle(QCPScatterStyle::ssCircle);
    m_plot1->graph(0)->setPen(QPen(Qt::green));

    // 阻抗曲线（QCPCurve：由采集线程实时写入）
    m_impedance_curve = new QCPCurve(m_plot1->xAxis, m_plot1->yAxis);
    m_impedance_curve->setPen(QPen(Qt::green, 1));
    m_impedance_curve->setLineStyle(QCPCurve::lsNone);
    m_impedance_curve->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 2));

    // 添加圆形曲线
    m_circleCurve = new QCPCurve(m_plot1->xAxis, m_plot1->yAxis);
    // m_plot1->addPlottable(m_circleCurve);
    m_circleCurve->setPen(QPen(Qt::red, 2)); // 红色圆圈，线宽2

    //
    m_plot1->xAxis->grid()->setVisible(true);
    m_plot1->yAxis->grid()->setVisible(true);
    // m_plot1->xAxis->setVisible(false);
    // m_plot1->yAxis->setVisible(false);
    //

    m_plot1->setBackground(QBrush(QColor("#1e1e1e")));
    m_plot1->xAxis->setBasePen(QPen(QColor("#cccccc")));
    m_plot1->yAxis->setBasePen(QPen(QColor("#cccccc")));
    m_plot1->xAxis->setTickPen(QPen(QColor("#cccccc")));
    m_plot1->yAxis->setTickPen(QPen(QColor("#cccccc")));
    m_plot1->xAxis->setTickLabelColor(QColor("#cccccc"));
    m_plot1->yAxis->setTickLabelColor(QColor("#cccccc"));
    m_plot1->xAxis->setLabelColor(QColor("#cccccc"));
    m_plot1->yAxis->setLabelColor(QColor("#cccccc"));
    QPen pen1, pen2,pen3;
    pen3 = m_plot1->xAxis->grid()->pen();
    // m_plot1->xAxis->grid()->setZeroLinePen(pen3);
    m_plot1->xAxis->grid()->setZeroLinePen(Qt::NoPen);
    m_plot1->xAxis2->grid()->setZeroLinePen(Qt::NoPen);
    // m_plot1->yAxis->grid()->setZeroLinePen(pen3);
    m_plot1->yAxis->grid()->setZeroLinePen(Qt::NoPen);
    m_plot1->yAxis2->grid()->setZeroLinePen(Qt::NoPen);

    m_plot1->xAxis2->setVisible(false);
    m_plot1->yAxis2->setVisible(false);
    m_plot1->xAxis2->setTickLabels(false);
    m_plot1->yAxis2->setTickLabels(false);
    // m_plot1->xAxis2->setTickLabelColor(QColor("#cccccc"));
    // m_plot1->xAxis2->setTickPen(QPen(QColor("#cccccc")));
    // m_plot1->xAxis2->setSubTickPen(QPen(QColor("#cccccc")));
    // m_plot1->xAxis2->setBasePen(QPen(QColor("#cccccc")));

    // 隐藏子刻度线
    m_plot1->xAxis->setSubTickPen(QPen(Qt::transparent));
    m_plot1->yAxis->setSubTickPen(QPen(Qt::transparent));
    m_plot1->xAxis->setSubTickLengthIn(0);
    m_plot1->xAxis->setSubTickLengthOut(0);
    m_plot1->yAxis->setSubTickLengthIn(0);
    m_plot1->yAxis->setSubTickLengthOut(0);

    QSharedPointer<QCPAxisTickerFixed> MyTicker0(new QCPAxisTickerFixed);
    MyTicker0->setTickStep(500);
    // MyTicker0->setTickCount(6);
    m_plot1->xAxis->setTicker(MyTicker0);
    m_plot1->yAxis->setTicker(MyTicker0);

    m_plot1->axisRect()->setMargins(QMargins(0,0,0,0));


    // 初始化圆形曲线
    updateCircleCurve();
    // 实时更新轴线的位置
    QObject::connect(this, &MainWindow::plotsize_changed, this, &MainWindow::updateplot1_zerotickerLine_0);
    m_plot1->installEventFilter(this);

    // ---------------------------------------------------------------------------------------------------------------------------------------
    // 初始化绘图区2 - A扫时序图
    m_plot2->xAxis->setRange(0, 150000);
    m_plot2->yAxis->setRange(-500, 500);
    // m_plot2->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot2->axisRect()->setupFullAxesBox(true);
    m_amplitude_curve = new QCPGraph(m_plot2->xAxis, m_plot2->yAxis);
    m_amplitude_curve->setPen(QPen(Qt::green));

    m_phase_curve = new QCPGraph(m_plot2->xAxis, m_plot2->yAxis);
    m_phase_curve->setPen(QPen(Qt::green));

    // 隐藏网格，只保留零线
    m_plot2->xAxis->grid()->setVisible(true);
    m_plot2->yAxis->grid()->setVisible(true);
    m_plot2->yAxis->setTickLabels(false);
    m_plot2->yAxis->setSubTicks(true);

    m_plot2->xAxis->setTickLabels(true);
    m_plot2->xAxis2->setTickLabels(true);

    // 设置X轴位置偏移

    m_plot2->xAxis2->setOffset(-200);
    m_plot2->xAxis->setOffset(-200);
    // 设置x轴样式
    m_plot2->xAxis->setTickLabelColor(QColor("#cccccc"));
    m_plot2->xAxis->setTickPen(QPen(QColor("#cc3e3e")));
    m_plot2->xAxis->setSubTickPen(QPen(QColor("#cc3e3e")));
    m_plot2->xAxis->setBasePen(QPen(QColor("#cc3e3e"),2));
    m_plot2->xAxis2->setTickLabelColor(QColor("#cccccc"));
    m_plot2->xAxis2->setTickPen(QPen(QColor("#cc3e3e")));
    m_plot2->xAxis2->setSubTickPen(QPen(QColor("#cc3e3e")));
    m_plot2->xAxis2->setBasePen(QPen(QColor("#cc3e3e"),2));



    // 设置零线为更明显的十字架
    // m_plot2->xAxis->grid()->setZeroLinePen(QPen(QColor("#ffffff"), 2));
    // m_plot2->yAxis->grid()->setZeroLinePen(QPen(QColor("#ffffff"), 2));
    m_plot2->setBackground(QBrush(QColor("#1e1e1e")));
    m_plot2->yAxis->setBasePen(QPen(QColor("#cccccc")));
    m_plot2->yAxis->setTickPen(QPen(QColor("#cccccc")));
    m_plot2->xAxis->setTickLabelColor(QColor("#cccccc"));
    m_plot2->yAxis->setTickLabelColor(QColor("#cccccc"));
    m_plot2->xAxis->setLabelColor(QColor("#cccccc"));
    m_plot2->yAxis->setLabelColor(QColor("#cccccc"));
    m_plot2->yAxis->setTickLabels(false);
    m_plot2->yAxis->setSubTicks(true);
    m_plot2->yAxis->setBasePen(QPen(Qt::transparent));
    m_plot2->yAxis->setTickPen(QPen(Qt::transparent));
    m_plot2->yAxis->setSubTickPen(QPen(QColor("#666666")));

    m_plot2->yAxis2->setTickLabels(false);
    m_plot2->yAxis2->setSubTicks(true);
    m_plot2->yAxis2->setBasePen(QPen(Qt::transparent));
    m_plot2->yAxis2->setTickPen(QPen(Qt::transparent));
    m_plot2->yAxis2->setSubTickPen(QPen(QColor("#666666")));

    m_plot2->xAxis->setSubTickLengthIn(2);
    m_plot2->xAxis->setSubTickLengthOut(0);
    m_plot2->yAxis->setSubTickLengthIn(2);
    m_plot2->yAxis->setSubTickLengthOut(0);
    m_plot2->yAxis2->setSubTickLengthIn(2);
    m_plot2->yAxis2->setSubTickLengthOut(0);

    m_plot2->axisRect()->setMargins(QMargins(0,0,0,0));
    m_plot2->yAxis->setLabelPadding(0);
    m_plot2->yAxis->setTickLabelPadding(0);

    QObject::connect(this, &MainWindow::plot2size_changed,
                     this, &MainWindow::updateplot2_Double_axis_line,
                     Qt::QueuedConnection);
    m_plot2->installEventFilter(this);

    // 隐藏子刻度线
    // m_plot2->xAxis->setSubTickPen(QPen(Qt::transparent));

/*
    m_plot2->yAxis->setBasePen(QPen(Qt::transparent));
    // 2. 刻度线透明
    m_plot2->yAxis->setTickPen(QPen(Qt::transparent));
    m_plot2->yAxis->setSubTickPen(QPen(QColor("#666666")));
    // 3. 刻度文字隐藏
    m_plot2->yAxis->setTickLabels(false);
    // 4. 可选：把刻度长度也清零，确保不占像素
    m_plot2->yAxis->setTickLengthIn(0);
    m_plot2->yAxis->setTickLengthOut(0);
    m_plot2->yAxis->setSubTickLengthIn(0);
    m_plot2->yAxis->setSubTickLengthOut(0);

    m_plot2->yAxis->setSubTickPen(QPen(QColor("#666666")));
    m_plot2->xAxis->setSubTickLengthIn(2);
    m_plot2->xAxis->setSubTickLengthOut(0);
    m_plot2->yAxis->setSubTickLengthIn(0);
    m_plot2->yAxis->setSubTickLengthOut(0);
*/

    // 设置y轴零刻度线样式
    QPen pen4 = m_plot2->yAxis->grid()->pen();
    m_plot2->yAxis->grid()->setZeroLinePen(pen4);

    m_plot2->xAxis->setSubTicks(true);
    m_plot2->xAxis2->setSubTicks(true);

    m_plot2->axisRect()->setMargins(QMargins(0,0,0,0));

    // 初始化绘图区3 - 预留给后期功能
    m_plot3->xAxis->setRange(0, 500);
    m_plot3->yAxis->setRange(-50, 50);
    m_plot3->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot3->axisRect()->setupFullAxesBox(true);

    m_plot3->addGraph();
    m_plot3->graph(0)->setPen(QPen(Qt::blue));

    // 隐藏网格，只保留零线
    m_plot3->xAxis->grid()->setVisible(true);
    m_plot3->yAxis->grid()->setVisible(true);
    m_plot3->xAxis->setTicks(false);
    m_plot3->yAxis->setTicks(false);
    m_plot3->xAxis->setTickLabels(false);
    m_plot3->yAxis->setTickLabels(false);
    m_plot3->xAxis2->setTickLabels(false);
    m_plot3->xAxis->setVisible(true);
    m_plot3->xAxis2->setVisible(false);
    m_plot3->yAxis->setVisible(false);
    m_plot3->yAxis2->setVisible(false);
    // 设置零线为更明显的十字架
    // m_plot3->xAxis->grid()->setZeroLinePen(QPen(QColor("#ffffff"), 2));
    // m_plot3->yAxis->grid()->setZeroLinePen(QPen(QColor("#ffffff"), 2));
    m_plot3->setBackground(QBrush(QColor("#1e1e1e")));
    m_plot3->xAxis->setBasePen(QPen(QColor("#cccccc")));
    m_plot3->yAxis->setBasePen(QPen(QColor("#cccccc")));
    m_plot3->xAxis->setTickPen(QPen(QColor("#cccccc")));
    m_plot3->yAxis->setTickPen(QPen(QColor("#cccccc")));
    m_plot3->xAxis->setTickLabelColor(QColor("#cccccc"));
    m_plot3->yAxis->setTickLabelColor(QColor("#cccccc"));
    m_plot3->xAxis->setLabelColor(QColor("#cccccc"));
    m_plot3->yAxis->setLabelColor(QColor("#cccccc"));

    // 隐藏子刻度线
    m_plot3->xAxis->setSubTickPen(QPen(Qt::transparent));
    m_plot3->yAxis->setSubTickPen(QPen(Qt::transparent));
    m_plot3->xAxis->setSubTickLengthIn(0);
    m_plot3->xAxis->setSubTickLengthOut(0);
    m_plot3->yAxis->setSubTickLengthIn(0);
    m_plot3->yAxis->setSubTickLengthOut(0);

    m_plot1->replot();
    m_plot2->replot();
}

/**
 * @brief 枚举本机可用以太网 IPv4 接口
 * @return 接口列表；优先返回名称含「以太网」/「Ethernet」的网卡，否则返回类型为 Ethernet 的网卡
 */
QVector<MainWindow::EthernetInterfaceInfo> MainWindow::getAvailableEthernetInterfaces() const
{
    QVector<EthernetInterfaceInfo> preferredInterfaces;
    QVector<EthernetInterfaceInfo> fallbackInterfaces;

    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const bool looksLikeNamedEthernet =
            iface.humanReadableName().startsWith(QStringLiteral("以太网")) ||
            iface.humanReadableName().startsWith(QStringLiteral("Ethernet"), Qt::CaseInsensitive);
        const bool looksLikeEthernetType = iface.type() == QNetworkInterface::Ethernet;

        if (!looksLikeNamedEthernet && !looksLikeEthernetType) {
            continue;
        }

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress addr = entry.ip();
            if (addr.protocol() != QAbstractSocket::IPv4Protocol || addr.isLoopback()) {
                continue;
            }

            const EthernetInterfaceInfo info{iface.name(), iface.humanReadableName(), addr.toString()};
            fallbackInterfaces.append(info);

            if (looksLikeNamedEthernet) {
                preferredInterfaces.append(info);
            }
        }
    }

    return preferredInterfaces.isEmpty() ? fallbackInterfaces : preferredInterfaces;
}

/**
 * @brief 获取当前应显示的本机 IPv4 地址
 * @return 用户已选网卡的 IP；未选择时取列表首项；无可用网卡时返回 "0.0.0.0"
 */
QString MainWindow::getLocalIPv4Address() const
{
    const QVector<EthernetInterfaceInfo> interfaces = getAvailableEthernetInterfaces();
    if (interfaces.isEmpty()) {
        return QStringLiteral("0.0.0.0");
    }

    if (!m_selectedInterfaceId.isEmpty()) {
        for (const EthernetInterfaceInfo &info : interfaces) {
            if (info.interfaceId == m_selectedInterfaceId) {
                return info.ipAddress;
            }
        }
    }

    return interfaces.constFirst().ipAddress;
}

/**
 * @brief 弹出对话框供用户选择本机以太网接口
 *
 * 更新 m_selectedInterfaceId 后刷新顶部「本机 IP」显示（updateParameterDisplay）。
 */
void MainWindow::chooseLocalInterfaceIp()
{
    const QVector<EthernetInterfaceInfo> interfaces = getAvailableEthernetInterfaces();
    if (interfaces.isEmpty()) {
        QMessageBox::information(this,
                                 tr("选择本机以太网"),
                                 tr("当前未检测到可用的以太网 IPv4 地址。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("选择本机以太网"));

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *promptLabel = new QLabel(tr("请选择主界面要显示的以太网名称及其 IP："), &dialog);
    QComboBox *interfaceComboBox = new QComboBox(&dialog);
    interfaceComboBox->setMinimumWidth(320);

    int currentIndex = 0;
    for (int index = 0; index < interfaces.size(); ++index) {
        const EthernetInterfaceInfo &info = interfaces[index];
        interfaceComboBox->addItem(tr("%1 (%2)").arg(info.displayName, info.ipAddress), info.interfaceId);

        if (!m_selectedInterfaceId.isEmpty() && info.interfaceId == m_selectedInterfaceId) {
            currentIndex = index;
        }
    }
    interfaceComboBox->setCurrentIndex(currentIndex);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                                       Qt::Horizontal,
                                                       &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(promptLabel);
    layout->addWidget(interfaceComboBox);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_selectedInterfaceId = interfaceComboBox->currentData().toString();
    qDebug() << "m_selectedInterfaceId : " << m_selectedInterfaceId;
    updateParameterDisplay();
}

/**
 * @brief 弹出对话框输入下位机 IP/端口并发起 TCP 连接
 *
 * 校验 IP 格式后调用 DeviceManager::connectToDevice；连接结果通过
 * connectionStateChanged / errorOccurred 回调并弹出提示框。
 */
void MainWindow::connectToRemoteDevice()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("连接下位机设备"));
    dialog.setMinimumSize(400, 160);
    dialog.resize(400, 200);

    constexpr int kFieldHeight = 32;
    constexpr int kFieldMinWidth = 200;
    constexpr int kButtonHeight = 32;
    constexpr int kButtonMinWidth = 100;

    dialog.setStyleSheet(
        "QDialog { background-color: #1e1e1e; }"
        "QLabel { color: #cccccc; font-size: 13px; background: transparent; border: none; }"
        "QLineEdit {"
        "    background-color: #2d2d30; color: #ffffff; border: 1px solid #3c3c3c;"
        "    border-radius: 3px; padding: 4px 8px; font-size: 13px;"
        "}"
        "QSpinBox {"
        "    background-color: #2d2d30; color: #ffffff; border: 1px solid #3c3c3c;"
        "    border-radius: 3px; padding-left: 8px; padding-right: 22px; font-size: 13px;"
        "}"
        "QSpinBox::up-button {"
        "    subcontrol-origin: border; subcontrol-position: top right;"
        "    width: 22px; height: 14px; background-color: #3c3c3c;"
        "    border-left: 1px solid #555; border-top-right-radius: 2px;"
        "}"
        "QSpinBox::down-button {"
        "    subcontrol-origin: border; subcontrol-position: bottom right;"
        "    width: 22px; height: 14px; background-color: #3c3c3c;"
        "    border-left: 1px solid #555; border-bottom-right-radius: 2px;"
        "}"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover { background-color: #0e639c; }"
        "QSpinBox::up-arrow {"
        "    width: 8px; height: 8px;"
        "}"
        "QSpinBox::down-arrow {"
        "    width: 8px; height: 8px;"
        "}"
        "QPushButton {"
        "    background-color: #0e639c; color: white; border: 1px solid #3c3c3c;"
        "    border-radius: 3px; padding: 6px 14px; font-size: 13px; font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #1177bb; }"
        "QPushButton:pressed { background-color: #094771; }"
    );

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);

    QLabel *promptLabel = new QLabel(tr("请输入下位机的 IP Address 和 Port："), &dialog);
    promptLabel->setWordWrap(true);

    QFormLayout *formLayout = new QFormLayout();
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setHorizontalSpacing(12);
    formLayout->setVerticalSpacing(10);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    formLayout->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);

    QLineEdit *ipEdit = new QLineEdit(&dialog);
    QSpinBox *portSpinBox = new QSpinBox(&dialog);

    ipEdit->setPlaceholderText(QStringLiteral("192.168.1.10"));
    ipEdit->setText(m_deviceHost);
    ipEdit->setMinimumHeight(kFieldHeight);
    ipEdit->setMinimumWidth(kFieldMinWidth);
    ipEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    portSpinBox->setRange(1, 65535);
    portSpinBox->setValue(m_devicePort);
    portSpinBox->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    portSpinBox->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    portSpinBox->setAccelerated(true);
    portSpinBox->setKeyboardTracking(true);
    portSpinBox->setMinimumHeight(kFieldHeight);
    portSpinBox->setMinimumWidth(kFieldMinWidth);
    portSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QLabel *ipLabel = new QLabel(tr("IP Address:"), &dialog);
    QLabel *portLabel = new QLabel(tr("Port:"), &dialog);
    ipLabel->setMinimumWidth(88);
    portLabel->setMinimumWidth(88);
    formLayout->addRow(ipLabel, ipEdit);
    formLayout->addRow(portLabel, portSpinBox);

    QHBoxLayout *buttonRowLayout = new QHBoxLayout();
    buttonRowLayout->setSpacing(12);
    buttonRowLayout->addStretch();

    QPushButton *connectButton = new QPushButton(tr("连接设备"), &dialog);
    QPushButton *queryBoardBtn = new QPushButton(tr("查询开发板"), &dialog);
    QPushButton *cancelButton = new QPushButton(tr("取消"), &dialog);
    connectButton->setMinimumSize(kButtonMinWidth, kButtonHeight);
    queryBoardBtn->setMinimumSize(kButtonMinWidth, kButtonHeight);
    cancelButton->setMinimumSize(kButtonMinWidth, kButtonHeight);
    connectButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    queryBoardBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    cancelButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    buttonRowLayout->addWidget(connectButton);
    buttonRowLayout->addWidget(queryBoardBtn);
    buttonRowLayout->addWidget(cancelButton);

    connect(connectButton,
            &QPushButton::clicked,
            &dialog,
            [&dialog, ipEdit]() {
                QHostAddress hostAddress;
                if (!hostAddress.setAddress(ipEdit->text().trimmed())) {
                    QMessageBox::warning(&dialog,
                                         QObject::tr("IP 地址无效"),
                                         QObject::tr("请输入有效的下位机 IP 地址。"));
                    return;
                }

                dialog.accept();
            });

    // 查询开发板信息按钮：发送询问命令，收到回应后弹窗展示
    connect(queryBoardBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        if (m_deviceManager->connectionState() != ConnectionState::Connected) {
            QMessageBox::information(&dialog, tr("提示"),
                tr("请先连接设备后再查询开发板信息。"));
            return;
        }

        // 一次性连接：收到一次回应后自动断开，避免重复弹窗
        auto active = std::make_shared<bool>(true);
        connect(m_deviceManager, &DeviceManager::boardInfoReceived,
                &dialog, [&dialog, active](BoardInfo info) {
            if (!*active)
                return;
            *active = false;

            if (!info.valid) {
                QMessageBox::warning(&dialog, tr("查询失败"),
                    tr("未收到有效的开发板信息回应。"));
                return;
            }

            const QString msg = QObject::tr(
                "MAC 地址: %1\n"
                "IP 地址: %2\n"
                "数据格式: %3\n"
                "有效位数: %4 bit\n"
                "采样率: %5 Hz\n"
                "每包字节数: %6")
                .arg(info.macAddress)
                .arg(info.ipAddress)
                .arg(info.signedData ? QObject::tr("有符号") : QObject::tr("无符号"))
                .arg(info.dataBits)
                .arg(info.sampleRateHz)
                .arg(info.bytesPerSend);

            QMessageBox::information(&dialog, QObject::tr("开发板信息"), msg);
        });

        m_deviceManager->queryBoardInfo();
    });

    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    layout->addWidget(promptLabel);
    layout->addLayout(formLayout);
    layout->addSpacing(4);
    layout->addLayout(buttonRowLayout);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_deviceHost = ipEdit->text().trimmed();
    m_devicePort = static_cast<quint16>(portSpinBox->value());
    m_deviceConnectionPending = true;
    updateDeviceConnectionStatusText();
    m_deviceManager->connectToDevice(m_deviceHost, m_devicePort);
}

/**
 * @brief 根据 DeviceManager 连接状态刷新下位机状态标签文案
 */
void MainWindow::updateDeviceConnectionStatusText()
{
    if (m_deviceHost.isEmpty()) {
        m_connectionStatusLabel2->setText(tr("下位机未连接，双击设置 IP/Port"));
        return;
    }

    const QString endpoint = tr("%1:%2").arg(m_deviceHost).arg(m_devicePort);
    switch (m_deviceManager->connectionState()) {
    case ConnectionState::Connecting:
        m_connectionStatusLabel2->setText(tr("下位机连接中: %1").arg(endpoint));
        break;
    case ConnectionState::Connected:
        m_connectionStatusLabel2->setText(tr("下位机已连接: %1").arg(endpoint));
        break;
    case ConnectionState::Disconnected:
        m_connectionStatusLabel2->setText(tr("下位机未连接: %1").arg(endpoint));
        break;
    }
}

/**
 * @brief 刷新顶部参数栏中的本机 IP 显示
 */
void MainWindow::updateParameterDisplay()
{
    // 更新连接状态中的IP信息
    const QString ip = getLocalIPv4Address();
    m_connectionStatusLabel->setText(tr("本机IP: %1 ").arg(ip));
}

/**
 * @brief 用当前选中探头的实际参数刷新顶部 12 个参数标签
 *
 * 从 m_displayProbeIndex 对应的 Probe 读取激励频率/相位/幅度、
 * 旋转角度、平衡点、滤波器配置、故障状态及最新数据点，
 * 写入 m_drivingFreqLabel ~ m_shiftLabel 共 12 个标签。
 * 若无有效探头则显示占位符 "--"。
 */
void MainWindow::updateProbeParameterDisplay()
{
    Probe *probe = nullptr;
    if (m_displayProbeIndex >= 0 && m_displayProbeIndex < m_probeManager->probeCount()) {
        probe = m_probeManager->probeAt(m_displayProbeIndex);
    }

    if (!probe) {
        m_drivingFreqLabel->setText(tr("Driving Freq: --"));
        m_refCurrentLabel->setText(tr("Ref Phase: --"));
        m_driveCurrentLabel->setText(tr("Excitation Amp: --"));
        m_rotationAngleLabel->setText(tr("Rotation Angle: --"));
        m_alarmLabel->setText(tr("Alarm: --"));
        m_realImaginaryLabel->setText(tr("Real: -- Imaginary: --"));
        m_digFilterLabel->setText(tr("Dig Filter: --"));
        m_shiftLabel->setText(tr("Shift X/Y: --"));
        m_postGainLabel->setText(tr("Post Gain: %1 (range: %2)")
            .arg(m_postGain)
            .arg(qMax(100.0, 2000.0 - (m_postGain - 20) * 100.0), 0, 'f', 0));
        return;
    }

    // 1. Driving Freq (激励频率)
    m_drivingFreqLabel->setText(tr("[P%1] Driving Freq: %2kHz")
        .arg(m_displayProbeIndex + 1)
        .arg(probe->excitationFreq() / 1000.0, 0, 'f', 3));

    // 2. Ref Phase (激励相位)
    m_refCurrentLabel->setText(tr("[P%1] Ref Phase: %2°")
        .arg(m_displayProbeIndex + 1)
        .arg(probe->excitationPhase()));

    // 3. Excitation Amp (激励幅度)
    m_driveCurrentLabel->setText(tr("[P%1] Excitation Amp: %2%")
        .arg(m_displayProbeIndex + 1)
        .arg(probe->excitationAmp()));

    // 4. Rotation Angle
    m_rotationAngleLabel->setText(tr("[P%1] Rotation Angle: %2dg")
        .arg(m_displayProbeIndex + 1)
        .arg(static_cast<double>(probe->rotationAngle()), 0, 'f', 1));

    // 5. Acquisition Freq (系统采样率，非探头参数)
    {
        const SampleRate rate = m_deviceManager->currentSampleRate();
        QString rateText;
        switch (rate) {
        case SampleRate::SR_1K:   rateText = QStringLiteral("1kHz");   break;
        case SampleRate::SR_50K:  rateText = QStringLiteral("50kHz");  break;
        case SampleRate::SR_100K: rateText = QStringLiteral("100kHz"); break;
        case SampleRate::SR_5M:   rateText = QStringLiteral("5MHz");   break;
        case SampleRate::SR_25M:  rateText = QStringLiteral("25MHz");  break;
        default:                  rateText = QStringLiteral("--");     break;
        }
        m_acquisitionFreqLabel->setText(tr("Acquisition Freq: %1").arg(rateText));
    }

    // 8. Alarm (故障状态)
    m_alarmLabel->setText(tr("[P%1] Alarm: %2")
        .arg(m_displayProbeIndex + 1)
        .arg(probe->hasFault() ? tr("Fault") : tr("Normal")));

    // 9. Real / Imaginary (最新数据点)
    {
        ProbeData *sd = probe->saveData();
        if (sd && !sd->isEmpty()) {
            const int last = sd->ampSize() - 1;
            if (last >= 0) {
                const auto &ampArr   = *sd->m_rawData_amp;
                const auto &phaseArr = *sd->m_rawData_phase;
                m_realImaginaryLabel->setText(tr("[P%1] Real:%2 Imag:%3")
                    .arg(m_displayProbeIndex + 1)
                    .arg(static_cast<double>(ampArr[last]), 0, 'f', 1)
                    .arg(static_cast<double>(phaseArr[last]), 0, 'f', 1));
            }
        }
    }

    // 10. Digital Filter (滤波器配置)
    {
        QString filterText;
        if (probe->filterLpEnabled())
            filterText += tr("LP:%1Hz ").arg(probe->filterLpCutoffHz(), 0, 'f', 0);
        if (probe->filterHpEnabled())
            filterText += tr("HP:%1Hz ").arg(probe->filterHpCutoffHz(), 0, 'f', 0);
        if (filterText.isEmpty())
            filterText = tr("No Filter");
        m_digFilterLabel->setText(tr("[P%1] Dig Filter: %2")
            .arg(m_displayProbeIndex + 1)
            .arg(filterText.trimmed()));
    }

    // 12. Shift X/Y (平衡点偏移)
    if (probe->isBalanceSet()) {
        m_shiftLabel->setText(tr("[P%1] Shift X/Y:%2/%3mV")
            .arg(m_displayProbeIndex + 1)
            .arg(static_cast<double>(probe->balanceAmp()), 0, 'f', 0)
            .arg(static_cast<double>(probe->balancePhase()), 0, 'f', 0));
    } else {
        m_shiftLabel->setText(tr("[P%1] Shift X/Y:0/0mV")
            .arg(m_displayProbeIndex + 1));
    }

    // 后置增益（全局参数，非探头独立）
    m_postGainLabel->setText(tr("Post Gain: %1 (range: %2)")
        .arg(m_postGain)
        .arg(qMax(100.0, 2000.0 - (m_postGain - 20) * 100.0), 0, 'f', 0));
}

/** @brief 虚拟方向键「上」——菜单/参数导航（待实现） */
void MainWindow::onVirtualUpClicked()
{
    // 实现向上操作逻辑
}

/** @brief 虚拟方向键「下」——菜单/参数导航（待实现） */
void MainWindow::onVirtualDownClicked()
{
    // 实现向下操作逻辑
}

/** @brief 虚拟方向键「左」——切换到上一个探头 */
void MainWindow::onVirtualLeftClicked()
{
    const int count = m_probeManager->probeCount();
    if (count <= 0) return;
    m_displayProbeIndex = (m_displayProbeIndex - 1 + count) % count;
    updateProbeParameterDisplay();
}

/** @brief 虚拟方向键「右」——切换到下一个探头 */
void MainWindow::onVirtualRightClicked()
{
    const int count = m_probeManager->probeCount();
    if (count <= 0) return;
    m_displayProbeIndex = (m_displayProbeIndex + 1) % count;
    updateProbeParameterDisplay();
}

/** @brief 确认键——应用当前菜单选择（待实现） */
void MainWindow::onConfirmClicked()
{
    // 实现确认操作逻辑
}

/** @brief 取消键——放弃当前操作并返回（待实现） */
void MainWindow::onCancelClicked()
{
    // 实现取消操作逻辑
}

/**
 * @brief 打开探头通道参数配置对话框
 *
 * 弹出 ProbeConfigDialog，用户可设置探头数量、各通道硬件映射、
 * 激励频率/相位/幅度及启用状态；确认后自动同步到 ProbeManager。
 */
void MainWindow::onSetSampleRateClicked()
{
    // 未连接时提示用户先连接设备
    if (m_deviceManager->connectionState() != ConnectionState::Connected) {
        QMessageBox::information(this, tr("设备未连接"),
            tr("请先连接下位机设备后再设置采样率。"));
        return;
    }

    // 采样率选项（所有通道统一使用同一采样率）
    const QStringList rateNames = {
        QStringLiteral("1 kHz"), QStringLiteral("50 kHz"), QStringLiteral("100 kHz"),
        QStringLiteral("5 MHz"), QStringLiteral("25 MHz")
    };
    const QVector<SampleRate> rateValues = {
        SampleRate::SR_1K, SampleRate::SR_50K, SampleRate::SR_100K,
        SampleRate::SR_5M, SampleRate::SR_25M
    };

    // 当前采样率作为默认选中项
    const SampleRate current = m_deviceManager->currentSampleRate();
    int defaultIndex = 2;  // 默认 100kHz
    for (int i = 0; i < rateValues.size(); ++i) {
        if (rateValues[i] == current) {
            defaultIndex = i;
            break;
        }
    }

    bool ok = false;
    const QString selected = QInputDialog::getItem(this, tr("设置采样率"),
        tr("选择 ADC 采样率（所有通道统一）："), rateNames, defaultIndex, false, &ok);
    if (!ok || selected.isEmpty())
        return;

    const int idx = rateNames.indexOf(selected);
    if (idx < 0 || idx >= rateValues.size())
        return;

    m_deviceManager->sendSampleRateConfig(rateValues[idx]);
}

void MainWindow::onMoreParametersClicked()
{
    ProbeConfigDialog dialog(m_probeManager, m_acquisitionThread, this);
    dialog.exec();
    updateProbeParameterDisplay();
}

void MainWindow::onAutoZeroClicked()
{
    const auto probes = m_probeManager->allProbes();
    if (probes.isEmpty()) return;

    // 选择探头
    QStringList probeNames;
    for (int i = 0; i < probes.size(); ++i) {
        if (probes[i]) {
            probeNames << tr("探头 %1").arg(i + 1);
        }
    }
    if (probeNames.isEmpty()) return;

    bool ok = false;
    const QString sel = QInputDialog::getItem(this, tr("自动清零"),
        tr("选择要设置平衡点的探头:"), probeNames, 0, false, &ok);
    if (!ok || sel.isEmpty()) return;

    const int idx = probeNames.indexOf(sel);
    if (idx < 0 || idx >= probes.size()) return;

    Probe *probe = probes[idx];
    if (!probe) return;

    // 取 saveData 中数据的均值作为平衡点
    ProbeData *sd = probe->saveData();
    if (!sd || sd->isEmpty()) {
        QMessageBox::information(this, tr("自动清零"),
            tr("探头 %1 暂无采集数据，请先开始采集。").arg(idx + 1));
        return;
    }

    float sumAmp = 0.0f, sumPhase = 0.0f;
    const int n = qMin(sd->ampSize(), sd->phaseSize());
    if (n <= 0) return;

    const auto &ampArr = *sd->m_rawData_amp;
    const auto &phaseArr = *sd->m_rawData_phase;
    for (int i = 0; i < n; ++i) {
        sumAmp += ampArr[i];
        sumPhase += phaseArr[i];
    }
    const float meanAmp = sumAmp / n;
    const float meanPhase = sumPhase / n;

    probe->setBalancePoint(meanAmp, meanPhase);

    qDebug() << "[MainWindow] 探头" << (idx + 1)
             << "平衡点:" << meanAmp << meanPhase;

    m_displayProbeIndex = idx;
    updateProbeParameterDisplay();
}

void MainWindow::onRotationAngleClicked()
{
    const auto probes = m_probeManager->allProbes();
    if (probes.isEmpty()) return;

    // 选择探头
    QStringList probeNames;
    for (int i = 0; i < probes.size(); ++i) {
        if (probes[i]) {
            probeNames << tr("探头 %1").arg(i + 1);
        }
    }
    if (probeNames.isEmpty()) return;

    bool ok = false;
    const QString sel = QInputDialog::getItem(this, tr("旋转角度"),
        tr("选择探头:"), probeNames, 0, false, &ok);
    if (!ok || sel.isEmpty()) return;

    const int idx = probeNames.indexOf(sel);
    if (idx < 0 || idx >= probes.size()) return;

    Probe *probe = probes[idx];
    if (!probe) return;

    const double angle = QInputDialog::getDouble(this, tr("旋转角度"),
        tr("探头 %1 的旋转角度（度）:").arg(idx + 1),
        static_cast<double>(probe->rotationAngle()),
        -360.0, 360.0, 1, &ok);
    if (!ok) return;

    probe->setRotationAngle(static_cast<float>(angle));

    qDebug() << "[MainWindow] 探头" << (idx + 1)
             << "旋转角:" << angle;

    m_displayProbeIndex = idx;
    updateProbeParameterDisplay();
}

void MainWindow::onPostGainClicked()
{
    bool ok = false;
    const int gain = QInputDialog::getInt(this, tr("后置增益"),
        tr("后置增益（基准值20，范围2000；每增减1，坐标轴范围增减100（暂用方案）："),
        m_postGain, 1, 100, 1, &ok);
    if (!ok) return;

    m_postGain = gain;
    updateProbeParameterDisplay();
    updateplot1_zerotickerLine_0();
}

/**
 * @brief 底部 Tab 切换时同步右侧 StackedWidget 页面
 * @param index 0=Main, 1=File, 2=Scanners, 3=Parameters
 */
void MainWindow::onTabChanged(int index)
{
    // 处理Tab切换逻辑，同步切换stack_menu的页面
    if (stack_menu) {
        stack_menu->setCurrentIndex(index);
    }

    switch (index) {
        case 0: // Main
            break;
        case 1: // File
            break;
        case 2: // Scanners
            break;
        case 3: // Parameters
            break;
        default:
            break;
    }
}

/**
 * @brief 按 plot1 当前坐标轴范围重绘阻抗平面参考圆
 *
 * 圆心在原点，半径取 X/Y 轴可视范围较小边的一半，随缩放/平移自动更新。
 */
void MainWindow::updateCircleCurve()
{
    if (!m_circleCurve) return;

    // 获取当前坐标轴范围
    double xMin = m_plot1->xAxis->range().lower;
    double xMax = m_plot1->xAxis->range().upper;
    double yMin = m_plot1->yAxis->range().lower;
    double yMax = m_plot1->yAxis->range().upper;

    // 计算坐标轴范围的宽度和高度
    // double xRange = xMax - xMin - 200;
    // double yRange = yMax - yMin - 200;
    double xRange = xMax - xMin;
    double yRange = yMax - yMin;

    // 圆的直径为横纵坐标范围的最小值
    double diameter = qMin(xRange, yRange);
    double radius = diameter / 2.0;

    // 清除之前的数据
    m_circleCurve->data()->clear();

    // 生成圆形的点数据
    int pointCount = 100; // 圆周上的点数
    QVector<double> t(pointCount), x(pointCount), y(pointCount);

    for (int i = 0; i < pointCount; ++i) {
        double angle = 2.0 * M_PI * i / (pointCount - 1);
        t[i] = i;
        x[i] = radius * cos(angle); // 圆心在原点(0, 0)
        y[i] = radius * sin(angle);
    }

    // 设置数据到曲线
    m_circleCurve->setData(t, x, y);

    // int pxY0 = m_plot1->yAxis->coordToPixel(0);   // 0 在屏幕上的 y 像素
    // int offsetx = pxY0 - m_plot1->axisRect()->top();
    // int pxY1 = m_plot1->xAxis->coordToPixel(0);   // 0 在屏幕上的 y 像素
    // int offsety = pxY1 - m_plot1->axisRect()->right();
    // m_plot1->xAxis->setOffset(offsetx);
    // m_plot1->yAxis->setOffset(offsety);
    // qDebug() << pxY0;
    // qDebug() << pxY1;
    // qDebug() << pxY0;
    // qDebug() << pxY0;
    // qDebug() << "offsetx: " << offsetx;
    // qDebug() << "offsety: " << offsety;
    // qDebug() << "m_plot1->yAxis->range().upper: " << m_plot1->yAxis->range().upper;
    // qDebug() << "m_plot1->xAxis->range().upper: " << m_plot1->xAxis->range().upper;
    // qDebug() << "m_plot1->xAxis->range().center: " << m_plot1->xAxis->range().center();
    // m_plot1->xAxis2->setTickLabelColor(QColor("#cccccc"));
    // m_plot1->xAxis2->setTickPen(QPen(QColor("#cccccc")));
    // m_plot1->xAxis2->setSubTickPen(QPen(QColor("#cccccc")));
    // m_plot1->xAxis2->setBasePen(QPen(QColor("#cccccc")));

    // 重新绘制
    m_plot1->replot();
}

/**
 * @brief 根据数据坐标 0 点在屏幕上的像素位置设置 plot 轴偏移
 * @param plot 目标 QCustomPlot（通常为 m_plot1）
 *
 * 使坐标原点与网格零线对齐；并恢复 xAxis2 刻度线可见样式。
 */
void MainWindow::updateplot1_zerotickerLine(QCustomPlot* plot)
{
    int pxY0 = m_plot1->yAxis->coordToPixel(0);   // 0 在屏幕上的 y 像素
    int offsetx = pxY0 - m_plot1->axisRect()->top();
    int pxY1 = m_plot1->xAxis->coordToPixel(0);   // 0 在屏幕上的 y 像素
    int offsety = pxY1 - m_plot1->axisRect()->right();
    plot->xAxis->setOffset(offsetx);
    plot->yAxis->setOffset(offsety);
    plot->xAxis2->setTickLabelColor(QColor("#cccccc"));
    plot->xAxis2->setTickPen(QPen(QColor("#cccccc")));
    plot->xAxis2->setSubTickPen(QPen(QColor("#cccccc")));
    plot->xAxis2->setBasePen(QPen(QColor("#cccccc")));
}

/**
 * @brief plot1 尺寸变化后居中零线并按宽高比调整坐标轴范围
 *
 * 由 plotsize_changed 信号触发：根据控件像素宽高设置轴偏移，保持阻抗图近似正方形比例。
 */
void MainWindow::updateplot1_zerotickerLine_0()
{
/*
    int pxY0 = m_plot1->yAxis->coordToPixel(0);   // 0 在屏幕上的 y 像素
    int offsetx = pxY0 - m_plot1->axisRect()->top();
    int pxY1 = m_plot1->xAxis->coordToPixel(0);   // 0 在屏幕上的 y 像素
    int offsety = pxY1 - m_plot1->axisRect()->right();
    m_plot1->xAxis->setOffset(-offsetx);
    m_plot1->yAxis->setOffset(offsety);
*/
    m_plot1->xAxis->setOffset(-m_plot1->size().height()/2);
    m_plot1->yAxis->setOffset(-m_plot1->size().width()/2);

    double plot1_size_width = -m_plot1->size().width();
    double plot1_size_height = -m_plot1->size().height();
    // int ratio = qMax(plot1_size_width, plot1_size_height) / qMin(plot1_size_width, plot1_size_height);
    double ratio = plot1_size_width / plot1_size_height;

    // 后置增益影响坐标轴范围：增益越大范围越小，曲线显示越大；基准值20→2000
    double baseRange = qMax(100.0, 2000.0 - (m_postGain - 20) * 100.0);

    if (ratio >= 1)
    {
        m_plot1->xAxis->setRange(- ratio*baseRange, ratio*baseRange);
        m_plot1->yAxis->setRange(- baseRange, baseRange);
        // qDebug() << " ratio1:  " << ratio;
    } else {
        m_plot1->yAxis->setRange(- baseRange/ratio, baseRange/ratio);
        m_plot1->xAxis->setRange(- baseRange, baseRange);
        // qDebug() << " ratio2:  " << ratio;
    }

    // m_plot1->xAxis->setRange()

    m_plot1->xAxis2->setTickLabelColor(QColor("#cccccc"));
    m_plot1->xAxis2->setTickPen(QPen(QColor("#cccccc")));
    m_plot1->xAxis2->setSubTickPen(QPen(QColor("#cccccc")));
    m_plot1->xAxis2->setBasePen(QPen(QColor("#cccccc")));

    m_plot1->replot(QCustomPlot::rpQueuedReplot);

    updateplot2_Double_axis_line();

}

/**
 * @brief 允许手动设定子刻度数的 QCPAxisTickerFixed 子类
 *
 * 原生 QCPAxisTickerFixed 的 getSubTickCount 由 tick step 自动推算，
 * 这里覆写为返回固定值，以便按页面尺寸动态调整子刻度密度。
 */
class QCPAxisTickerFixedWithSubTicks : public QCPAxisTickerFixed
{
public:
    void setSubTickCount(int count) { mSubTickCount = count; }

protected:
    int getSubTickCount(double tickStep) Q_DECL_OVERRIDE
    {
        Q_UNUSED(tickStep)
        return mSubTickCount;
    }

private:
    int mSubTickCount = 4;
};

/**
 * @brief plot2 尺寸变化后调整 A 扫图双 X 轴偏移、刻度步长与坐标范围
 *
 * 由 plot2size_changed 信号触发，使上下两条 X 轴与控件高度对齐。
 */
void MainWindow::updateplot2_Double_axis_line()
{
    m_plot2->xAxis->setOffset(-m_plot2->size().height()/4);
    m_plot2->xAxis2->setOffset(-m_plot2->size().height()/4);

    const double yRange = 400; // ±200
    m_plot2->yAxis->setRange(-200, 200);

    const double w = m_plot2->axisRect()->width();
    const double h = m_plot2->axisRect()->height();
    if (w <= 0 || h <= 0) return;

    // Y 轴步长 50，子刻度 8 根
    const double yStep = 50;
    const int ySubTickCount = 8;

    auto yTicker = QSharedPointer<QCPAxisTickerFixedWithSubTicks>::create();
    yTicker->setTickStep(yStep);
    yTicker->setSubTickCount(ySubTickCount);
    m_plot2->yAxis->setTicker(yTicker);
    m_plot2->yAxis2->setTicker(yTicker);

    // X 轴：先算子刻度步长使网格呈正方形，再让主刻度步长为其整数倍
    const double xRange = m_plot2->xAxis->range().size();
    const double ySubStep = yStep / (ySubTickCount + 1);
    const double desiredXSubStep = ySubStep * (xRange / yRange) * (h / w);
    const double xSubStep = qMax(100.0, qRound(desiredXSubStep / 100.0) * 100.0);

    const double rawStep = yStep * (h / w) * (xRange / yRange);
    const double xStep = qMax(xSubStep, qRound(rawStep / xSubStep) * xSubStep);
    const int xSubTickCount = qBound(0, qRound(xStep / xSubStep) - 1, 20);

    auto xTicker = QSharedPointer<QCPAxisTickerFixedWithSubTicks>::create();
    xTicker->setTickStep(xStep);
    xTicker->setSubTickCount(xSubTickCount);
    m_plot2->xAxis->setTicker(xTicker);
    m_plot2->xAxis2->setTicker(xTicker);

    m_plot2->replot();
}

/**
 * @brief 处理主窗口内部分控件的事件过滤
 * @param obj 事件源对象
 * @param event Qt 事件
 * @return 已处理返回 true，否则交给基类
 *
 * - 双击 m_connectionStatusLabel：选择本机以太网 IP
 * - 双击 m_connectionStatusLabel2：连接下位机
 * - m_plot1 / m_plot2 Resize：触发重绘并发出 plotsize_changed / plot2size_changed
 */
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // m_plot1->replot();
    if (obj == m_connectionStatusLabel && event->type() == QEvent::MouseButtonDblClick)
    {
        chooseLocalInterfaceIp();
        return true;
    }

    if (obj == m_connectionStatusLabel2 && event->type() == QEvent::MouseButtonDblClick)
    {
        if (m_deviceManager->connectionState() == ConnectionState::Connecting) {
            // 正在连接中 → 双击取消连接
            const auto result = QMessageBox::question(this, tr("取消连接"),
                tr("设备正在连接中，是否取消？"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (result == QMessageBox::Yes) {
                // 二次检查：弹窗期间连接可能已完成，只在仍 Connecting 时才断开
                if (m_deviceManager->connectionState() == ConnectionState::Connecting)
                    m_deviceManager->disconnectFromDevice();
                m_deviceConnectionPending = false;
                m_acquisitionPending = false;
                updateDeviceConnectionStatusText();
            }
        } else {
            connectToRemoteDevice();
        }
        return true;
    }

    if (obj == m_plot1 && event->type() == QEvent::Resize)
    {
        QSize newSize = static_cast<QResizeEvent*>(event)->size();
        // qDebug() << "customPlot resized to:" << newSize;
        // updateplot1_zerotickerLine_0();
        // 这里可以做自动更新坐标轴、重绘等
        // m_plot1->replot(QCustomPlot::rpQueuedReplot);
        // m_plot2->replot(QCustomPlot::rpQueuedReplot);
        m_plot1->replot();
        // m_plot2->replot();
        emit plotsize_changed();
    }

    if (obj == m_plot2 && event->type() == QEvent::Resize)
    {
        QSize newSize = static_cast<QResizeEvent*>(event)->size();
        // qDebug() << "customPlot2 resized to:" << newSize;

        emit plot2size_changed();
    }

    return QMainWindow::eventFilter(obj, event);
}

/**
 * @brief 从 CSV 文件加载历史采集数据并显示到绘图区
 *
 * 1. 选择 CSV 文件与目标探头
 * 2. 解析 #Balance / #Rotation 元数据头
 * 3. 读取原始数据点
 * 4. 将元数据恢复到探头（平衡点 + 旋转角）
 * 5. 应用与采集线程相同的变换管线（减平衡点 → 旋转）
 * 6. 写入 plot1（阻抗平面）与 plot2（A 扫图）
 *
 * 兼容旧格式：无 #Balance / #Rotation 头时，按零值处理。
 */
void MainWindow::onLoadDataClicked()
{
    // ── 1. 选择 CSV 文件 ──
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("加载采集数据"), QString(), tr("CSV 文件 (*.csv)"));
    if (filePath.isEmpty()) return;

    // ── 2. 选择目标探头 ──
    const auto probes = m_probeManager->allProbes();
    QStringList probeNames;
    for (int i = 0; i < probes.size(); ++i) {
        if (probes[i])
            probeNames << tr("探头 %1").arg(i + 1);
    }
    if (probeNames.isEmpty()) return;

    bool ok = false;
    const QString sel = QInputDialog::getItem(this, tr("加载数据"),
        tr("选择目标探头:"), probeNames, 0, false, &ok);
    if (!ok || sel.isEmpty()) return;

    const int probeIdx = probeNames.indexOf(sel);
    if (probeIdx < 0 || probeIdx >= probes.size()) return;
    Probe *probe = probes[probeIdx];
    if (!probe) return;

    // ── 3. 打开文件并解析 ──
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("加载失败"),
            tr("无法打开文件: %1").arg(filePath));
        return;
    }

    QTextStream in(&file);
    float fileBalanceAmp = 0.0f, fileBalancePhase = 0.0f;
    bool fileHasBalance = false;
    float fileRotation = 0.0f;

    // 预分配容量，避免频繁扩容
    QVector<float> rawAmp, rawPhase;
    rawAmp.reserve(500000);
    rawPhase.reserve(500000);

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        // # 开头的行为元数据注释行
        if (line.startsWith('#')) {
            if (line.startsWith(QStringLiteral("#Balance:"))) {
                const QString val = line.mid(9);
                const int comma = val.indexOf(',');
                if (comma > 0) {
                    fileBalanceAmp   = val.left(comma).toFloat();
                    fileBalancePhase = val.mid(comma + 1).toFloat();
                    fileHasBalance = true;
                }
            } else if (line.startsWith(QStringLiteral("#Rotation:"))) {
                fileRotation = line.mid(10).toFloat();
            }
            continue;
        }

        // 数据行格式: amp,phase
        const int comma = line.indexOf(',');
        if (comma <= 0) continue;

        rawAmp.append(line.left(comma).toFloat());
        rawPhase.append(line.mid(comma + 1).toFloat());
    }
    file.close();

    if (rawAmp.isEmpty()) {
        QMessageBox::information(this, tr("加载数据"),
            tr("文件中没有有效数据。"));
        return;
    }

    // ── 4. 恢复探头元数据 ──
    if (fileHasBalance) {
        probe->setBalancePoint(fileBalanceAmp, fileBalancePhase);
    }
    probe->setRotationAngle(fileRotation);

    // ── 5. 预计算旋转三角函数（角度为 0 时跳过） ──
    const float balAmp   = fileHasBalance ? fileBalanceAmp : 0.0f;
    const float balPhase = fileHasBalance ? fileBalancePhase : 0.0f;
    const bool needRotate = std::fabs(fileRotation) > 1e-6f;
    float cosA = 1.0f, sinA = 0.0f;
    if (needRotate) {
        const float rad = fileRotation * static_cast<float>(M_PI) / 180.0f;
        cosA = std::cos(rad);
        sinA = std::sin(rad);
    }

    // ── 7. 逐点变换：减平衡点 → 旋转 ──
    const int nPoints = rawAmp.size();
    QVector<double> plot1_x(nPoints), plot1_y(nPoints);
    QVector<double> plot2_keys(nPoints), plot2_amp(nPoints), plot2_phase(nPoints);

    for (int i = 0; i < nPoints; ++i) {
        float ampVal   = rawAmp[i];
        float phaseVal = rawPhase[i];

        // ① 减去平衡点（图像居中）
        if (fileHasBalance) {
            ampVal   -= balAmp;
            phaseVal -= balPhase;
        }

        // ② 二维旋转变换
        if (needRotate) {
            const float x = ampVal;
            const float y = phaseVal;
            ampVal   = x * cosA - y * sinA;
            phaseVal = x * sinA + y * cosA;
        }

        const double dKey = static_cast<double>(i);
        plot1_x[i] = static_cast<double>(ampVal);   // 阻抗图 X = 实部
        plot1_y[i] = static_cast<double>(phaseVal);  // 阻抗图 Y = 虚部
        plot2_keys[i]  = dKey;
        plot2_amp[i]   = static_cast<double>(ampVal);
        plot2_phase[i] = static_cast<double>(phaseVal);
    }

    // ── 8. 写入绘图区 ──

    // plot1：阻抗平面散点图（X=实部/amp，Y=虚部/phase）
    m_plot1->graph(0)->setData(plot1_x, plot1_y, true);
    m_plot1->rescaleAxes();
    m_plot1->replot();

    // plot2：A 扫时序图（红色=幅值，绿色=相位）
    if (m_amplitude_curve)
        m_amplitude_curve->setData(plot2_keys, plot2_amp, true);
    if (m_phase_curve)
        m_phase_curve->setData(plot2_keys, plot2_phase, true);
    m_plot2->rescaleAxes();
    m_plot2->replot();

    qDebug() << "[MainWindow] 加载数据:" << filePath
             << "点数:" << nPoints
             << "探头:" << (probeIdx + 1)
             << "平衡点:" << balAmp << balPhase
             << "旋转:" << fileRotation;

    m_displayProbeIndex = probeIdx;
    updateProbeParameterDisplay();
}

// 由 QTimer 周期性调用，将当前选中探头的数据容器刷入绘图曲线
void MainWindow::refreshPlots()
{
    if (m_displayProbeIndex < 0 || m_displayProbeIndex >= m_probeCurves.size())
        return;

    // // 每帧刷新实时数据标签（Real / Imaginary）
    // {
    //     Probe *probe = m_probeManager->probeAt(m_displayProbeIndex);
    //     if (probe) {
    //         ProbeData *sd = probe->saveData();
    //         if (sd && !sd->isEmpty()) {
    //             const int last = sd->ampSize() - 1;
    //             if (last >= 0) {
    //                 const auto &ampArr   = *sd->m_rawData_amp;
    //                 const auto &phaseArr = *sd->m_rawData_phase;
    //                 m_realImaginaryLabel->setText(tr("[P%1] Real:%2 Imag:%3")
    //                     .arg(m_displayProbeIndex + 1)
    //                     .arg(static_cast<double>(ampArr[last]), 0, 'f', 1)
    //                     .arg(static_cast<double>(phaseArr[last]), 0, 'f', 1));
    //             }
    //         }
    //     }
    // }

    const auto &c = m_probeCurves[m_displayProbeIndex];
    if (m_impedance_curve && c.impedance && !c.impedance->isEmpty())
        m_impedance_curve->data()->set(*c.impedance,true);
    if (m_amplitude_curve && c.amplitude && !c.amplitude->isEmpty())
        m_amplitude_curve->data()->set(*c.amplitude, true);
    if (m_phase_curve && c.phase && !c.phase->isEmpty())
        m_phase_curve->data()->set(*c.phase, true);

    // A-scan X 轴滚动：窗口宽度 150000，数据量超窗口时跟随最新 key 滚动
    constexpr int ASCAN_WINDOW = DataAcquisitionThread::CURVE_CLEAR_SIZE / 2;
    if (m_amplitude_curve && m_amplitude_curve->data()->size() > 0) {
        const double latestKey = (m_amplitude_curve->data()->constEnd() - 1)->key;
        if (latestKey <= ASCAN_WINDOW)
            m_plot2->xAxis->setRange(0, ASCAN_WINDOW);
        else
            m_plot2->xAxis->setRange(latestKey - ASCAN_WINDOW, latestKey);
    }

    m_plot1->replot(QCustomPlot::rpQueuedReplot);
    m_plot2->replot(QCustomPlot::rpQueuedReplot);
}

void MainWindow::syncProbeCurves()
{
    const int newCount = m_probeManager->probeCount();
    const int oldCount = m_probeCurves.size();

    // 收缩时释放多余容器
    for (int i = newCount; i < oldCount; ++i) {
        delete m_probeCurves[i].impedance;
        delete m_probeCurves[i].amplitude;
        delete m_probeCurves[i].phase;
    }

    m_probeCurves.resize(newCount);

    // 扩张时为新增探头创建容器并预分配容量
    for (int i = oldCount; i < newCount; ++i) {
        m_probeCurves[i].impedance = new QVector<QCPCurveData>();
        m_probeCurves[i].amplitude = new QVector<QCPGraphData>();
        m_probeCurves[i].phase     = new QVector<QCPGraphData>();
        m_probeCurves[i].impedance->reserve(DataAcquisitionThread::CURVE_CAPACITY);
        m_probeCurves[i].amplitude->reserve(DataAcquisitionThread::CURVE_CAPACITY);
        m_probeCurves[i].phase->reserve(DataAcquisitionThread::CURVE_CAPACITY);
    }

    // 全量重注册，确保采集线程的 m_curveRefs 与最新探头列表对齐
    const auto probes = m_probeManager->allProbes();
    for (int i = 0; i < probes.size() && i < m_probeCurves.size(); ++i) {
        if (probes[i]) {
            m_acquisitionThread->registerCurveData(
                i,
                m_probeCurves[i].impedance,
                m_probeCurves[i].amplitude,
                m_probeCurves[i].phase);
        }
    }

    // 若当前显示索引越界，回退到 0
    if (m_displayProbeIndex >= newCount)
        m_displayProbeIndex = 0;
}

void MainWindow::onBoardInfoReceived(BoardInfo info)
{
    if (!info.valid)
        return;

    // 用开发板返回的 IP 更新存储的地址，
    // 下次打开连接对话框时 ipEdit 会自动显示正确的 IP。
    if (!info.ipAddress.isEmpty() && info.ipAddress != QStringLiteral("0.0.0.0")) {
        // m_deviceHost = info.ipAddress;  // 暂不自动填写，由用户手动确认
        qDebug() << "[MainWindow] 开发板信息 | IP:" << info.ipAddress
                 << "MAC:" << info.macAddress
                 << "采样率:" << info.sampleRateHz << "Hz"
                 << "数据位数:" << info.dataBits;
    }
}

void MainWindow::onStartAcquisitionBtnClicked()
{
    if (m_acquisitionThread->isAcquiring()) {
        // 正在采集 → 停止
        m_deviceManager->sendStopAcquisition();  // 通知下位机停止上传
        m_acquisitionThread->stop();
        m_plotRefreshTimer->stop();
        m_saveManager->onAcquisitionStopped();
        return;
    }

    // 未采集 → 检查设备连接
    const auto cs = m_deviceManager->connectionState();
    if (cs == ConnectionState::Connected) {
        startAcquisition();
        return;
    }
    if (cs == ConnectionState::Connecting) {
        QMessageBox::information(this, tr("提示"),
            tr("设备正在连接中，请等待连接完成后再开始采集。"));
        return;
    }

    // 未连接 → 提示用户连接
    const auto result = QMessageBox::question(this, tr("设备未连接"),
        tr("下位机设备未连接，是否现在连接？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (result != QMessageBox::Yes)
        return;

    m_acquisitionPending = true;
    connectToRemoteDevice();
    // 用户在连接对话框中取消 → 清除等待标志
    if (!m_deviceConnectionPending)
        m_acquisitionPending = false;
}

void MainWindow::startAcquisition()
{
    // 下发 DA 配置（新协议：全局 DDS 频率/相位，所有通道共用）
    m_deviceManager->sendDaConfigNew(m_probeManager->buildDaConfig());
    // 设置帧长
    m_deviceManager->sendFrameLength(512);
    // 通知下位机开始上传数据
    m_deviceManager->sendStartAcquisition();

    m_saveManager->onAcquisitionStarted();
    m_acquisitionThread->start();
    m_plotRefreshTimer->start();
    m_stackStartAcquisitionBtn->setText(tr("停止\n采集"));
}
