#pragma once
#include <QWidget>
#include "Omnigen.h"

class QOmnigenViewport;

class QOmnigenViewportSection : public QWidget
{
    static inline QMap<int, QOmnigenViewport*> viewports;
    static inline int activeIdx = int(EOmnigenSection::Viewport1);

    template<typename... Ts>
    static inline std::array<QCheckBox*, 4> renderToggles = { nullptr, nullptr, nullptr, nullptr };

public:
    QOmnigenViewportSection(QWidget* parent, EOmnigenSection inSection);

    static const auto& getAllViewports() { return viewports; }
    static QOmnigenViewport* getActiveViewport() { return viewports[activeIdx]; };
    static void changeActiveViewport(QOmnigenViewport* viewport);
    static void repaintAll(bool forceRefresh = false);
    
    static void saveToggles(OmniBin<std::ios::out>& omniBin);
    static void loadToggles(OmniBin<std::ios::in>& omniBin);

private:
    QWidget* createViewportMenuBar();
    EOmnigenSection section;

    template<typename... Ts>
    friend void makeToggle(QOmnigenViewportSection*, const QString&, QMenu*, bool);
};