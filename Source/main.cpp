#include "stdafx.h"
#include "Omnigen.h"
#include <QtWidgets/QApplication>
#include "Editor/Framework/Style/OmniStyle.h"
#include "CodeSandbox.h"

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication a(argc, argv);
    a.setStyle(new OmniStyle);

    auto* omnigen = Omnigen::get();
    omnigen->initialize();
    omnigen->setWindowIcon(QIcon("Resources/Icons/Omnigen.ico"));
    omnigen->maximize();

    return a.exec();
}
