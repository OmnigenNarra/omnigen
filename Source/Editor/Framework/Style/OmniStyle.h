#pragma once
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QProxyStyle>
#include <QStyleFactory>

class OmniStyle : public QProxyStyle 
{
    Q_OBJECT

public:
    OmniStyle();
    explicit OmniStyle(QStyle* style);

    QStyle* baseStyle() const;

    void polish(QPalette& palette) override;
    void polish(QApplication* app) override;

private:
    QStyle* styleBase(QStyle* style = Q_NULLPTR) const;
};
