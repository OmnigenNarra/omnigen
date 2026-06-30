#include "stdafx.h"
#include "OmniStyle.h"

OmniStyle::OmniStyle()
    : OmniStyle(styleBase())
{
}

OmniStyle::OmniStyle(QStyle* style)
    : QProxyStyle(style) 
{}

QStyle* OmniStyle::styleBase(QStyle* style) const
{
    static QStyle* base = style
        ? style
        : QStyleFactory::create(QStringLiteral("Fusion"));

    return base;
}

QStyle* OmniStyle::baseStyle() const
{ 
    return styleBase(); 
}

void OmniStyle::polish(QPalette& palette)
{
    // 0% #121212
    // 5% #1E1E1E
    // 10% #2B2B2B
    // 15% #383838
    // 20% #454545
    // 40% #787878
    // 80% #DEDEDE
    // 88% #F2F2F2
    //white 100% #ffffff

    // Additional (blue) #21AEF8

    // debug magenta #ff00ff

    palette.setColor(QPalette::Window, QColor("#2B2B2B"));
    palette.setColor(QPalette::WindowText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#DEDEDE"));
    palette.setColor(QPalette::Base, QColor("#454545"));
    palette.setColor(QPalette::AlternateBase, QColor("#21AEF8"));
    palette.setColor(QPalette::ToolTipBase, QColor("#2B2B2B"));
    palette.setColor(QPalette::ToolTipText, QColor("#ffffff"));
    palette.setColor(QPalette::Text, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#787878"));
    palette.setColor(QPalette::Dark, QColor("#1E1E1E"));
    palette.setColor(QPalette::Shadow, QColor("#121212"));
    palette.setColor(QPalette::Button, QColor("#454545"));
    palette.setColor(QPalette::ButtonText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#787878"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Link, QColor("#21AEF8"));
    palette.setColor(QPalette::Highlight, QColor("#21AEF8"));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#787878"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#787878"));
}

void OmniStyle::polish(QApplication* app)
{
    if (!app) 
        return;

    QFont font("Segoe UI", 10);
    app->setFont(font);

    // loadstylesheet
    QFile styleFile(QStringLiteral("Resources/OmniStyle.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        // set stylesheet
        QString qsStylesheet = QString::fromLatin1(styleFile.readAll());
        app->setStyleSheet(qsStylesheet);
        styleFile.close();
    }
}
