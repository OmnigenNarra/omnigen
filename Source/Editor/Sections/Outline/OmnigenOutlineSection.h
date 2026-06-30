#pragma once
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "OutlineTree.h"

class Omnigen;

// Stage-based tools section, usually consists of a toolbar and an object tree
// Filled by stage tools
class QOmnigenOutlineSection : public QWidget
{
    Q_OBJECT

public:
    QOmnigenOutlineSection(QWidget* parent, Omnigen*);

    void fillSection(const std::vector<QWidget*> widgets);
    void clearSection();

    void applyTreeStyle(QTreeView* treeView);

private:
    QLayout* mainLayout = nullptr;
};

class OutlineTreeView : public QTreeView
{
    Q_OBJECT

protected:
    virtual void mousePressEvent(QMouseEvent* event)    override;
    virtual void mouseReleaseEvent(QMouseEvent* event)    override;
};