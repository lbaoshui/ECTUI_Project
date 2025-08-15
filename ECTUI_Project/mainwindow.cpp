#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupUI();
}

MainWindow::~MainWindow()
{
    delete ui;
}

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
        "    color: #cccccc;"
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

    showMaximized();
}

void MainWindow::setupFirstRow()
{
    // 第一行：参数详情显示 + 模式信息
    QFrame *firstRowFrame = new QFrame(this);
    firstRowFrame->setFrameStyle(QFrame::Box);
    firstRowFrame->setFixedHeight(250);
    
    QHBoxLayout *firstRowLayout = new QHBoxLayout(firstRowFrame);
    firstRowLayout->setSpacing(0);      // 参数区和模式信息区之间的间距
    firstRowLayout->setContentsMargins(1, 1, 1, 1);  // 第一行内部边距
    
    // 参数详情显示区域
    m_parameterDisplayFrame = new QFrame(this);
    m_parameterDisplayFrame->setFrameStyle(QFrame::Box);
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
    m_autoEraseLabel = new QLabel(tr("Auto erase after 408 points"), this);
    m_shiftLabel = new QLabel(tr("Shift X/Y:-1000/-372mV"), this);

    // 设置字体和颜色
    QFont paramFont;
    paramFont.setPointSize(9);
    QList<QLabel*> paramLabels = {m_drivingFreqLabel, m_refCurrentLabel, m_driveCurrentLabel, m_rotationAngleLabel,
                                  m_acquisitionFreqLabel, m_preGainLabel, m_postGainLabel, m_alarmLabel,
                                  m_realImaginaryLabel, m_digFilterLabel, m_autoEraseLabel, m_shiftLabel};
    
    for (QLabel* label : paramLabels) {
        label->setFont(paramFont);
        label->setStyleSheet("QLabel { color: white; background-color: #2b2b2b; padding: 5px; border: 1px solid #555; font-weight: bold; text-align: center; font-size: 30px}");
        label->setMinimumSize(150, 50);
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
    m_modeInfoFrame->setFixedWidth(380);
    m_modeInfoLayout = new QVBoxLayout(m_modeInfoFrame);
    
    m_modeInfoLabel = new QLabel(tr("Reflection\nMode"), this);
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
    m_plotArea1Layout = new QVBoxLayout(m_plotArea1);
    m_plot1 = new QCustomPlot(this);
    m_plot1->setMinimumSize(400, 250);
    m_plotArea1Layout->addWidget(m_plot1);

    // 绘图区2（右上）
    m_plotArea2Frame = new QFrame(this);
    m_plotArea2Frame->setFrameStyle(QFrame::Box);
    m_plotArea2Layout = new QVBoxLayout(m_plotArea2Frame);
    
    m_plot2 = new QCustomPlot(this);
    m_plot2->setMinimumSize(400, 250);
    m_plotArea2Layout->addWidget(m_plot2);

    // 绘图区3（左下）
    m_plotArea3 = new QFrame(this);
    m_plotArea3->setFrameStyle(QFrame::Box);
    m_plotArea3Layout = new QVBoxLayout(m_plotArea3);
    m_plot3 = new QCustomPlot(this);
    m_plot3->setMinimumSize(400, 150);
    m_plotArea3Layout->addWidget(m_plot3);

    // 控制按键区域（右下）
    m_controlFrame = new QFrame(this);
    m_controlFrame->setFrameStyle(QFrame::Box);
    m_controlFrame->setFixedWidth(200);
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
        btn->setFixedSize(60, 60);
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
    // m_middleLayout->addWidget(m_controlFrame, 0, 2);



    // 绘图区
    m_plotArea_frame = new QFrame();
    m_middleLayout_1 = new QVBoxLayout(m_plotArea_frame); //

    stack_menu_frame = new QFrame();
    stack_menu_frame->setFixedWidth(150);
    stack_menu_frame->setFrameStyle(QFrame::Box);
    
    QVBoxLayout* stackFrameLayout = new QVBoxLayout(stack_menu_frame);
    stackFrameLayout->setContentsMargins(0, 0, 0, 0);
    
    stack_menu = new QStackedWidget();
    stack_menu->setFixedWidth(150);
    stackFrameLayout->addWidget(stack_menu);

    m_middleLayout_1->addWidget(m_plotArea_frame1);  // 绘图区1和2
    m_middleLayout_1->addWidget(m_plotArea3);        // 绘图区3

    m_middleLayout_border->addWidget(m_plotArea_frame);
    m_middleLayout_border->addWidget(stack_menu_frame);
    m_middleLayout_border->addWidget(m_controlFrame);
    m_plotArea3->setFixedHeight(150);
    
    m_mainLayout->addWidget(m_middleFrame);
}

void MainWindow::setupThirdRow()
{
    // 第三行：连接状态 + 四个菜单分栏
    QFrame *thirdRowFrame = new QFrame(this);
    thirdRowFrame->setFrameStyle(QFrame::Box);
    
    QVBoxLayout *thirdRowLayout = new QVBoxLayout(thirdRowFrame);
    thirdRowLayout->setSpacing(5);      // 连接状态和Tab之间的间距
    thirdRowLayout->setContentsMargins(5, 5, 5, 5);  // 第三行内部边距
    
    // 设备连接状态显示
    m_connectionStatusFrame = new QFrame(this);
    m_connectionStatusFrame->setFrameStyle(QFrame::Box);
    m_connectionStatusFrame->setFixedHeight(60);
    m_connectionStatusLayout = new QHBoxLayout(m_connectionStatusFrame);
    
    m_connectionStatusLabel = new QLabel(tr("设备连接状态显示:"), this);
    m_connectionStatusLabel->setStyleSheet("QLabel { color: #ffcc02; background-color: #252526; border: 1px solid #3c3c3c; font-weight: bold; padding: 5px; font-size: 20px; border-radius: 3px; }");
    m_connectionStatusLayout->addWidget(m_connectionStatusLabel);
    m_connectionStatusLayout->setMargin(2);

    m_connectionStatusLabel2 = new QLabel(tr("SSEC Board usb not connected"), this);
    m_connectionStatusLabel2->setStyleSheet("QLabel { color: #ffcc02; background-color: #252526; border: 1px solid #3c3c3c; font-weight: bold; padding: 5px; font-size: 20px; border-radius: 3px; }");
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

void MainWindow::setupTabContents()
{
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
}

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
                          "padding: 5px; border-radius: 3px; font-weight: bold; font-size: 20px; }");
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
                          "padding: 5px; border-radius: 3px; font-weight: bold; font-size: 20px; }");
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
    m_stackConnectDisconnectBtn->setEnabled(false);
    m_stackShowRealtimePositionBtn->setEnabled(false);
    
    QList<QPushButton*> scannersStackBtns = {m_stackConnectDisconnectBtn, m_stackShowRealtimePositionBtn};
    for (QPushButton* btn : scannersStackBtns) {
        btn->setFixedHeight(80);
        btn->setStyleSheet("QPushButton { background-color: #0e639c; color: white; border: 1px solid #3c3c3c; "
                          "padding: 5px; border-radius: 3px; font-weight: bold; font-size: 20px; }");
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
        btn->setStyleSheet("QPushButton { background-color: #3a3a3a; color: white; border: 1px solid #5a5a5a; "
                          "padding: 5px; border-radius: 3px; font-weight: bold; font-size: 20px; }");
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
}

void MainWindow::initializePlots()
{
    // 初始化绘图区1 - 阻抗平面图（丽萨如图）
    // m_plot1->xAxis->setLabel(tr("Real"));
    // m_plot1->yAxis->setLabel(tr("Imaginary"));
    m_plot1->xAxis->setRange(-2000, 2000);
    m_plot1->yAxis->setRange(-2000, 2000);
    m_plot1->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot1->axisRect()->setupFullAxesBox(true);
    m_plot1->addGraph();
    m_plot1->graph(0)->setScatterStyle(QCPScatterStyle::ssCircle);
    m_plot1->graph(0)->setPen(QPen(Qt::green));
    
    // 隐藏网格，只保留零线
    m_plot1->xAxis->grid()->setVisible(true);
    m_plot1->yAxis->grid()->setVisible(true);
    // 设置零线为更明显的十字架
    m_plot1->xAxis->grid()->setZeroLinePen(QPen(QColor("#ffffff"), 2));
    m_plot1->yAxis->grid()->setZeroLinePen(QPen(QColor("#ffffff"), 2));
    m_plot1->setBackground(QBrush(QColor("#1e1e1e")));
    m_plot1->xAxis->setBasePen(QPen(QColor("#cccccc")));
    m_plot1->yAxis->setBasePen(QPen(QColor("#cccccc")));
    m_plot1->xAxis->setTickPen(QPen(QColor("#cccccc")));
    m_plot1->yAxis->setTickPen(QPen(QColor("#cccccc")));
    m_plot1->xAxis->setTickLabelColor(QColor("#cccccc"));
    m_plot1->yAxis->setTickLabelColor(QColor("#cccccc"));
    m_plot1->xAxis->setLabelColor(QColor("#cccccc"));
    m_plot1->yAxis->setLabelColor(QColor("#cccccc"));
    
    // 隐藏子刻度线
    m_plot1->xAxis->setSubTickPen(QPen(Qt::transparent));
    m_plot1->yAxis->setSubTickPen(QPen(Qt::transparent));
    m_plot1->xAxis->setSubTickLengthIn(0);
    m_plot1->xAxis->setSubTickLengthOut(0);
    m_plot1->yAxis->setSubTickLengthIn(0);
    m_plot1->yAxis->setSubTickLengthOut(0);

    // 初始化绘图区2 - A扫时序图
    // m_plot2->xAxis->setLabel(tr("Time"));
    // m_plot2->yAxis->setLabel(tr("Amplitude"));
    m_plot2->xAxis->setRange(-500, 500);
    m_plot2->yAxis->setRange(-1, 1);
    m_plot2->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot2->axisRect()->setupFullAxesBox(true);
    m_plot2->addGraph();
    m_plot2->graph(0)->setPen(QPen(Qt::red));
    
    // 隐藏网格，只保留零线
    m_plot2->xAxis->grid()->setVisible(true);
    m_plot2->yAxis->grid()->setVisible(true);
    // 设置零线为更明显的十字架
    m_plot2->xAxis->grid()->setZeroLinePen(QPen(QColor("#ffffff"), 2));
    m_plot2->yAxis->grid()->setZeroLinePen(QPen(QColor("#ffffff"), 2));
    m_plot2->setBackground(QBrush(QColor("#1e1e1e")));
    m_plot2->xAxis->setBasePen(QPen(QColor("#cccccc")));
    m_plot2->yAxis->setBasePen(QPen(QColor("#cccccc")));
    m_plot2->xAxis->setTickPen(QPen(QColor("#cccccc")));
    m_plot2->yAxis->setTickPen(QPen(QColor("#cccccc")));
    m_plot2->xAxis->setTickLabelColor(QColor("#cccccc"));
    m_plot2->yAxis->setTickLabelColor(QColor("#cccccc"));
    m_plot2->xAxis->setLabelColor(QColor("#cccccc"));
    m_plot2->yAxis->setLabelColor(QColor("#cccccc"));
    
    // 隐藏子刻度线
    m_plot2->xAxis->setSubTickPen(QPen(Qt::transparent));
    m_plot2->yAxis->setSubTickPen(QPen(Qt::transparent));
    m_plot2->xAxis->setSubTickLengthIn(0);
    m_plot2->xAxis->setSubTickLengthOut(0);
    m_plot2->yAxis->setSubTickLengthIn(0);
    m_plot2->yAxis->setSubTickLengthOut(0);

    // 初始化绘图区3 - 预留给后期功能
    // m_plot3->xAxis->setLabel(tr("X"));
    // m_plot3->yAxis->setLabel(tr("Y"));
    m_plot3->xAxis->setRange(-50, 50);
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
    // m_plot3->xAxis->setVisible(false);
    m_plot3->yAxis->setVisible(false);
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
}

QString MainWindow::getLocalIPv4Address() const
{
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress addr = entry.ip();
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                return addr.toString();
            }
        }
    }
    return QStringLiteral("0.0.0.0");
}

void MainWindow::updateParameterDisplay()
{
    // 更新连接状态中的IP信息
    const QString ip = getLocalIPv4Address();
    m_connectionStatusLabel->setText(tr("本机IP: %1 ").arg(ip));
}

// 虚拟按键槽函数实现
void MainWindow::onVirtualUpClicked()
{
    // 实现向上操作逻辑
}

void MainWindow::onVirtualDownClicked()
{
    // 实现向下操作逻辑
}

void MainWindow::onVirtualLeftClicked()
{
    // 实现向左操作逻辑
}

void MainWindow::onVirtualRightClicked()
{
    // 实现向右操作逻辑
}

void MainWindow::onConfirmClicked()
{
    // 实现确认操作逻辑
}

void MainWindow::onCancelClicked()
{
    // 实现取消操作逻辑
}

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
