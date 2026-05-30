#include "mainwindow.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // Windows 原生样式下 QSpinBox 子控件（加减按钮/箭头）不响应 stylesheet，
    // 统一使用 Fusion 样式保证跨平台一致的渲染效果。
    a.setStyle(QStyleFactory::create("Fusion"));
    MainWindow w;
    // 设置窗口启动时最大化


    w.show();
    return a.exec();
}
