#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSplitter>
#include <QFrame>
#include <QInputDialog>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QTabWidget>
#include <QStackedWidget>
#include "qcustomplot.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 虚拟按键槽函数
    void onVirtualUpClicked();
    void onVirtualDownClicked();
    void onVirtualLeftClicked();
    void onVirtualRightClicked();
    void onConfirmClicked();
    void onCancelClicked();

    // 底部菜单切换槽函数
    void onTabChanged(int index);

private:
    Ui::MainWindow *ui;

    // 主布局结构
    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;

    // 顶部参数详情显示区域
    QFrame *m_parameterDisplayFrame;
    QGridLayout *m_parameterDisplayLayout;
    QLabel *m_drivingFreqLabel;
    QLabel *m_refCurrentLabel;
    QLabel *m_driveCurrentLabel;
    QLabel *m_rotationAngleLabel;
    QLabel *m_acquisitionFreqLabel;
    QLabel *m_preGainLabel;
    QLabel *m_postGainLabel;
    QLabel *m_alarmLabel;
    QLabel *m_realImaginaryLabel;
    QLabel *m_digFilterLabel;
    QLabel *m_autoEraseLabel;
    QLabel *m_shiftLabel;

    // 中间四个区域的主框架
    QFrame *m_middleFrame;
    QGridLayout *m_middleLayout;
    QFrame *m_middleFrame_border;
    QHBoxLayout *m_middleLayout_border;
    QVBoxLayout *m_middleLayout_1;

    // 子菜单区域
    QFrame* stack_menu_frame = nullptr;
    QStackedWidget* stack_menu = nullptr;
    
    // stack_menu中的四个页面
    QWidget* m_stackMainPage = nullptr;
    QWidget* m_stackFilePage = nullptr;
    QWidget* m_stackScannersPage = nullptr;
    QWidget* m_stackParametersPage = nullptr;
    
    // stack_menu页面的布局
    QVBoxLayout* m_stackMainLayout = nullptr;
    QVBoxLayout* m_stackFileLayout = nullptr;
    QVBoxLayout* m_stackScannersLayout = nullptr;
    QVBoxLayout* m_stackParametersLayout = nullptr;
    
    // stack_menu页面中的按钮（复制自tab页面的按钮）
    QPushButton* m_stackStartAcquisitionBtn = nullptr;
    QPushButton* m_stackClearAlarmsBtn = nullptr;
    QPushButton* m_stackAutoZeroBtn = nullptr;
    QPushButton* m_stackAutoCalibrateBtn = nullptr;
    QPushButton* m_stackClearPhaseWindowBtn = nullptr;
    QPushButton* m_stackMoreMainBtn = nullptr;
    
    QPushButton* m_stackLoadDataBtn = nullptr;
    QPushButton* m_stackSaveDataBtn = nullptr;
    QPushButton* m_stackLoadConfigBtn = nullptr;
    QPushButton* m_stackSaveDefaultConfigBtn = nullptr;
    QPushButton* m_stackMoreFileBtn = nullptr;
    
    QPushButton* m_stackConnectDisconnectBtn = nullptr;
    QPushButton* m_stackShowRealtimePositionBtn = nullptr;
    
    QPushButton* m_stackSetExcitationFreqBtn = nullptr;
    QPushButton* m_stackSetSampleRateBtn = nullptr;
    QPushButton* m_stackSetPreGainBtn = nullptr;
    QPushButton* m_stackSetPostGainBtn = nullptr;
    QPushButton* m_stackSetRotationAngleBtn = nullptr;
    QPushButton* m_stackMoreParametersBtn = nullptr;

    QFrame *m_plotArea_frame;
    QFrame *m_plotArea_frame1;

    // 绘图区1（左上）
    QFrame *m_plotArea1;
    QVBoxLayout *m_plotArea1Layout;
    QCustomPlot *m_plot1;

    // 绘图区2（右上）+ 模式信息
    QFrame *m_plotArea2Frame;
    QVBoxLayout *m_plotArea2Layout;
    QCustomPlot *m_plot2;
    QFrame *m_modeInfoFrame;
    QVBoxLayout *m_modeInfoLayout;
    QLabel *m_modeInfoLabel;

    // 绘图区3（左下）
    QFrame *m_plotArea3;
    QVBoxLayout *m_plotArea3Layout;
    QCustomPlot *m_plot3;

    // 控制按键区域（右下）
    QFrame *m_controlFrame;
    QVBoxLayout *m_controlLayout;
    QFrame *m_virtualButtonFrame;
    QGridLayout *m_virtualButtonLayout;
    QPushButton *m_upBtn;
    QPushButton *m_downBtn;
    QPushButton *m_leftBtn;
    QPushButton *m_rightBtn;
    QFrame *m_confirmCancelFrame;
    QHBoxLayout *m_confirmCancelLayout;
    QPushButton *m_confirmBtn;
    QPushButton *m_cancelBtn;

    // 设备连接状态显示
    QFrame *m_connectionStatusFrame;
    QHBoxLayout *m_connectionStatusLayout;
    QLabel *m_connectionStatusLabel;
    QLabel *m_connectionStatusLabel2;

    // 底部四个菜单分栏
    QTabWidget *m_bottomTabWidget;
    QWidget *m_mainTab;
    QWidget *m_fileTab;
    QWidget *m_scannersTab;
    QWidget *m_parametersTab;

    // 各个Tab页面的内容
    QVBoxLayout *m_mainTabLayout;
    QVBoxLayout *m_fileTabLayout;
    QVBoxLayout *m_scannersTabLayout;
    QVBoxLayout *m_parametersTabLayout;

    // Main Tab 内容
    QGridLayout *m_mainButtonLayout;
    QPushButton *m_startAcquisitionBtn;
    QPushButton *m_clearAlarmsBtn;
    QPushButton *m_autoZeroBtn;
    QPushButton *m_autoCalibrateBtn;
    QPushButton *m_clearPhaseWindowBtn;
    QPushButton *m_moreMainBtn;

    // File Tab 内容
    QGridLayout *m_fileButtonLayout;
    QPushButton *m_loadDataBtn;
    QPushButton *m_saveDataBtn;
    QPushButton *m_loadConfigBtn;
    QPushButton *m_saveDefaultConfigBtn;
    QPushButton *m_moreFileBtn;

    // Scanners Tab 内容
    QGridLayout *m_scannersButtonLayout;
    QPushButton *m_connectDisconnectBtn;
    QPushButton *m_showRealtimePositionBtn;

    // Parameters Tab 内容
    QGridLayout *m_parametersButtonLayout;
    QPushButton *m_setExcitationFreqBtn;
    QPushButton *m_setSampleRateBtn;
    QPushButton *m_setPreGainBtn;
    QPushButton *m_setPostGainBtn;
    QPushButton *m_setRotationAngleBtn;
    QPushButton *m_moreParametersBtn;

    // 初始化函数
    void setupUI();
    void setupFirstRow();   // 第一行：参数详情显示 + 模式信息
    void setupSecondRow();  // 第二行：四个绘图/控制区域
    void setupThirdRow();   // 第三行：连接状态 + 四个菜单分栏
    void setupTabContents();
    void setupStackMenuContents();
    void setupConnections();
    void initializePlots();

    // 工具函数
    QString getLocalIPv4Address() const;
    void updateParameterDisplay();
};
#endif // MAINWINDOW_H
