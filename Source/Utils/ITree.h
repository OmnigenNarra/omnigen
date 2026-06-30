#pragma once
#include "Utils/OmniBin/OmniBinQt.h"
#include <QSharedPointer>
#include <QEnableSharedFromThis>

template<typename T>
class ITree : public QEnableSharedFromThis<T>
{
public:
    virtual ~ITree()
    {
        children.clear();
    }

    void removeChild(const QSharedPointer<T>& child) { auto it = std::find(children.begin(), children.end(), child); children.erase(it); };

    const auto& getParent() const { return parent; }
    const auto& getChildren() const { return children; }

    bool isRoot() const { return parent.isNull(); }

    template<typename L>
    void forEachChild(const L& lambda, QSharedPointer<T> current = nullptr)
    {
        if (current)
            lambda(current);
        else
            current = static_cast<T*>(this)->sharedFromThis();

        for (auto&& child : current->children)
            forEachChild(lambda, child);
    }

protected:
    QWeakPointer<T> parent;
    std::vector<QSharedPointer<T>> children;

    static void store(std::vector<T>* data, std::vector<int>* parents, const QSharedPointer<T>& currentNode)
    {
        data->push_back(*currentNode);
        parents->push_back(currentNode->parent ? indexOf(*data, *currentNode->parent.lock()) : -1);

        for (auto&& child : currentNode->children)
            store(data, parents, child);
    }

    static void restore(const std::vector<T>& data, const std::vector<int>& parents, const QSharedPointer<T>& rootNode)
    {
        std::vector<QSharedPointer<T>> nodes(data.size());
        nodes[0] = rootNode;
        *nodes[0] = data[0];

        for (size_t i = 1; i < data.size(); ++i)
        {
            nodes[i] = QSharedPointer<T>::create();
            *nodes[i] = data[i];
        }

        for (size_t i = 1; i < data.size(); ++i)
        {
            nodes[i]->parent = nodes[parents[i]];
            nodes[parents[i]]->children.push_back(nodes[i]);
        }
    }

    FRIEND_OMNIBIN(ITree);
};

#define DEFINE_TREE_SAVELOAD(T) \
inline void omniSave(const ITree<T>& object, OmniBin<std::ios::out>& omniBin) \
{ \
    static bool saving = false; \
    if (saving) \
        return; \
\
    std::vector<T> data; \
    std::vector<int> parents; \
    ITree<T>::store(&data, &parents, const_cast<ITree<T>&>(object).sharedFromThis()); \
\
    saving = true; \
    omniBin << data; \
    omniBin << parents; \
    saving = false; \
} \
\
inline void omniLoad(ITree<T>& object, OmniBin<std::ios::in>& omniBin) \
{ \
    static bool loading = false;\
    if (loading) \
        return; \
\
    std::vector<T> data; \
    std::vector<int> parents; \
\
    loading = true; \
    omniBin >> data; \
    omniBin >> parents; \
    loading = false; \
\
    ITree<T>::restore(data, parents, object.sharedFromThis()); \
}