#ifndef CUSTOMPLOT_H
#define CUSTOMPLOT_H

#include "qcustomplot.h"

class customplot : public QCustomPlot
{
public:
    customplot();

    public:
    explicit customplot(QWidget *parent = nullptr) : QCustomPlot(parent) {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        // setScaledContents(true);          // 让图片/背景跟着缩放
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override {
        return qRound(w * 1.0 / 1.0);    // 16:9
    }
};

#endif // CUSTOMPLOT_H
