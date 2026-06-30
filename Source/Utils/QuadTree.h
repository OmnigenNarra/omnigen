#pragma once
#include <type_traits>
#include <optional>

template<typename T>
class Box
{
    static_assert(std::is_arithmetic<T>::value);

public:
    explicit constexpr Box(T Left = 0, T Bottom = 0, T Width = 0, T Height = 0) noexcept :
        left(Left), bottom(Bottom), width(Width), height(Height)
    {

    }

    constexpr Box(const QVector2D& position, const QVector2D& size) noexcept :
        left(position.x()), bottom(position.y()), width(size.x()), height(size.y())
    {

    }

    constexpr T getRight() const noexcept
    {
        return left + width;
    }

    constexpr T getTop() const noexcept
    {
        return bottom + height;
    }

    [[nodiscard]] constexpr QVector2D getBottomLeft() const noexcept
    {
        return QVector2D(left, bottom);
    }

    [[nodiscard]] constexpr QVector2D getCenter() const noexcept
    {
        return QVector2D(left + width / 2, bottom + height / 2);
    }

    [[nodiscard]] constexpr QVector2D getSize() const noexcept
    {
        return QVector2D(width, height);
    }

    constexpr bool contains(const Box<T>& box) const noexcept
    {
        return left <= box.left && box.getRight() <= getRight() &&
            bottom <= box.bottom && box.getTop() <= getTop();
    }

    constexpr bool intersects(const Box<T>& box) const noexcept
    {
        return !(left >= box.getRight() || getRight() <= box.left ||
            bottom <= box.getTop() || getTop() >= box.bottom);
    }

    template <typename T, typename BoxGetter, typename EqualityTest, typename InternalPrimitive>
    friend class QuadTree;

private:
    T left;
    T bottom;
    T width;
    T height;
};

template<typename T, typename BoxGetter, typename EqualityTest = std::equal_to<T>, typename InternalPrimitive = double>
class QuadTree
{
    static_assert(std::is_convertible_v<std::invoke_result_t<BoxGetter, const T&>, Box<InternalPrimitive>>,
        "BoxGetter must be a callable of signature Box<InternalPrimitive>(const T&)");

    static_assert(std::is_convertible_v<std::invoke_result_t<EqualityTest, const T&, const T&>, bool>,
        "EqualityTest must be a callable of signature bool(const T&, const T&)");

    Q_STATIC_ASSERT(std::is_arithmetic_v<InternalPrimitive>);

public:
    QuadTree() = default;

    QuadTree(const QuadTree&) = default;
    QuadTree& operator=(const QuadTree&) = default;

    QuadTree(const Box<InternalPrimitive>& box, const BoxGetter& getBox = BoxGetter(),
        const EqualityTest& equal = EqualityTest()) :
        box(box), rootNode(std::make_shared<Node>()), getBox(getBox), isEqual(equal)
    {

    }

    // Add a value to the quad tree
    void addValue(const T& value)
    {
        add(rootNode.get(), 0, box, value);
    }

    // Remove a value from the quad tree
    void removeValue(const T& value)
    {
        remove(rootNode.get(), nullptr, box, value);
    }

    void resizeRootBox(const Box<InternalPrimitive>& inBox)
    {
        box = inBox;
    }

    std::vector<T> query(const Box<InternalPrimitive>& box) const
    {
        auto values = std::vector<T>();
        query(rootNode.get(), rootNode, box, values);
        return values;
    }

    std::vector<std::pair<T, T>> findAllIntersections() const
    {
        auto intersections = std::vector<std::pair<T, T>>();
        findAllIntersections(rootNode.get(), intersections);
        return intersections;
    }

private:
    // The maximum number of values a node can contain before a split is desirable
    static constexpr uint8_t threshold = 16;
    // The maximum depth of a node (the value at which we stop splitting it further)
    static constexpr uint8_t maxDepth = 8;

    struct Node
    {
        std::array<std::unique_ptr<Node>, 4> children;
        std::vector<T> values;
    };

    Box<InternalPrimitive> box;
    std::shared_ptr<Node> rootNode;
    BoxGetter getBox;
    EqualityTest isEqual;

    static bool isLeafNode(const Node* node)
    {
        return !static_cast<bool>(node->children[0]);
    }

    void query(Node* node, const Box<InternalPrimitive>& box, const Box<InternalPrimitive>& queryBox, std::vector<T>& values) const;

    void findAllIntersections(Node* node, std::vector<std::pair<T, T>>& intersections) const;
    void findIntersectionsInDescendants(Node* node, const T& value, std::vector<std::pair<T, T>>& intersections) const;

    // Compute the box of a child from the box of its parent and the index of its quadrant
    Box<InternalPrimitive> computeBox(const Box<InternalPrimitive>& inBox, int i) const;

    // Gets the quadrant in which a value exists (returns null if a value is not fully a part of any quadrant)
    [[nodiscard]] std::optional<int> getQuadrant(const Box<InternalPrimitive>& nodeBox, const Box<InternalPrimitive>& valueBox) const;

    // Internal addition of values
    void add(Node* node, uint8_t depth, const Box<InternalPrimitive>& box, const T& value);
    // Internal removal of values
    void remove(Node* node, Node* parent, const Box<InternalPrimitive>& box, const T& value);

    void splitNode(Node* node, const Box<InternalPrimitive>& box);

    // Attempt to merge a node
    void tryMerge(Node* node);

    void removalHelperInternal(Node* node, const T& value);
};

template <typename T, typename BoxGetter, typename EqualityTest, typename InternalPrimitive>
void QuadTree<T, BoxGetter, EqualityTest, InternalPrimitive>::query(Node* node, const Box<InternalPrimitive>& box,
    const Box<InternalPrimitive>& queryBox, std::vector<T>& values) const
{
    Q_ASSERT(node != nullptr);
    Q_ASSERT(queryBox.intersects(box));
    for (const auto& value : node->values)
    {
        if (queryBox.intersects(getBox(value)))
            values.push_back(value);
    }
    if (!isLeafNode(node))
    {
        for (auto i = std::size_t(0); i < node->children.size(); ++i)
        {
            auto childBox = computeBox(box, static_cast<int>(i));
            if (queryBox.intersects(childBox))
                query(node->children[i].get(), childBox, queryBox, values);
        }
    }
}

template <typename T, typename BoxGetter, typename EqualityTest, typename InternalPrimitive>
void QuadTree<T, BoxGetter, EqualityTest, InternalPrimitive>::findAllIntersections(Node* node,
    std::vector<std::pair<T, T>>& intersections) const
{
    // Find intersections between values stored in this node
    // Make sure to not report the same intersection twice
    for (auto i = std::size_t(0); i < node->values.size(); ++i)
    {
        for (auto j = std::size_t(0); j < i; ++j)
        {
            if (getBox(node->values[i]).intersects(getBox(node->values[j])))
                intersections.emplace_back(node->values[i], node->values[j]);
        }
    }
    if (!isLeafNode(node))
    {
        // Values in this node can intersect values in descendants
        for (const auto& child : node->children)
        {
            for (const auto& value : node->values)
                findIntersectionsInDescendants(child.get(), value, intersections);
        }
        // Find intersections in children
        for (const auto& child : node->children)
            findAllIntersections(child.get(), intersections);
    }
}

template <typename T, typename BoxGetter, typename EqualityTest, typename InternalPrimitive>
void QuadTree<T, BoxGetter, EqualityTest, InternalPrimitive>::findIntersectionsInDescendants(Node* node, const T& value,
    std::vector<std::pair<T, T>>& intersections) const
{
    // Test against the values stored in this node
    for (const auto& other : node->values)
    {
        if (getBox(value).intersects(getBox(other)))
            intersections.emplace_back(value, other);
    }
    // Test against values stored into descendants of this node
    if (!isLeafNode(node))
    {
        for (const auto& child : node->children)
            findIntersectionsInDescendants(child.get(), value, intersections);
    }
}

template<typename T, typename BoxGetter, typename EqualityTest, typename InternalPrimitive>
inline Box<InternalPrimitive> QuadTree<T, BoxGetter, EqualityTest, InternalPrimitive>::computeBox(const Box<InternalPrimitive>& inBox, int i) const
{
    auto origin = box.getBottomLeft();
    auto childSize = box.getSize() / static_cast<InternalPrimitive>(2);
    switch (i)
    {
        // North West
    case 0:
        return Box<InternalPrimitive>(origin, childSize);
        // North East
    case 1:
        return Box<InternalPrimitive>(QVector2D(origin.x() + childSize.x(), origin.y()), childSize);
        // South West
    case 2:
        return Box<InternalPrimitive>(QVector2D(origin.x(), origin.y() + childSize.y()), childSize);
        // South East
    case 3:
        return Box<InternalPrimitive>(origin + childSize, childSize);
    default:
        Q_ASSERT_X(false , "QuadTree::computeBox", "Invalid child index");
        return Box<InternalPrimitive>();
    }
}

template <typename T, typename BoxGetter, typename EqualityTest, typename InternalPrimitive>
std::optional<int> QuadTree<T, BoxGetter, EqualityTest, InternalPrimitive>::getQuadrant(const Box<InternalPrimitive>& nodeBox, const Box<InternalPrimitive>& valueBox) const
{
    auto center = nodeBox.getCenter();
    // West
    if (valueBox.getRight() < center.x())
    {
        // North West
        if (valueBox.bottom < center.y())
            return 0;

        // South West
        if (valueBox.getTop() >= center.y())
            return 2;

        return std::nullopt;
    }

    // East
    if (valueBox.left >= center.x())
    {
        // North East
        if (valueBox.bottom < center.y())
            return 1;

        // South East
        if (valueBox.getTop() >= center.y())
            return 3;

        return std::nullopt;
    }
    
    return std::nullopt;
}

template <typename T, typename BoxGetter, typename EqualityTest, typename InternalPrimitive>
void QuadTree<T, BoxGetter, EqualityTest, InternalPrimitive>::add(Node* node, uint8_t depth,
    const Box<InternalPrimitive>& box, const T& value)
{
    Q_ASSERT(node != nullptr);
    Q_ASSERT(box.contains(getBox(value)));

    if (QuadTree::isLeafNode(node))
    {
        // Insert the value in this node if possible
        if (depth >= maxDepth || node->values.size() < threshold)
            node->values.push_back(value);

        // Otherwise, we split and we try again
        else
        {
            splitNode(node, box);
            add(node, depth, box, value);
        }
    }
    else
    {
        const auto i = getQuadrant(box, getBox(value));
        // Add the value in a child if the value is entirely contained in it
        if (i)
        {
            add(node->children[i.value()].get(), depth + 1, computeBox(box, i.value()), value);
        }
            
        // Otherwise, we add the value in the current node
        else
        {
            node->values.push_back(value);
        }  
    }
}

template <typename T, typename BoxGetter, typename EqualityTest, typename InternalPrimitive>
void QuadTree<T, BoxGetter, EqualityTest, InternalPrimitive>::remove(Node* node, Node* parent,
    const Box<InternalPrimitive>& box, const T& value)
{
    Q_ASSERT(node != nullptr);
    Q_ASSERT(box.contains(getBox(value)));

    if (QuadTree::isLeafNode(node))
    {
        // Remove the value from node
        removalHelperInternal(node, value);

        // Try to merge the parent
        if (parent != nullptr)
            tryMerge(parent);
    }
    else
    {
        // Remove the value in a child if the value is entirely contained in it
        auto i = getQuadrant(box, getBox(value));
        if (i)
        {
            remove(node->children[i.value()].get(), node, computeBox(box, i.value()), value);
        }
        // Otherwise, we remove the value from the current node
        else
        {
            removalHelperInternal(node, value);
        }
    }
}

template <typename T, typename BoxGetter, typename EqualityTest, typename InternalPrimitive>
void QuadTree<T, BoxGetter, EqualityTest, InternalPrimitive>::splitNode(Node* node, const Box<InternalPrimitive>& box)
{
    Q_ASSERT(node != nullptr);
    Q_ASSERT_X(QuadTree::isLeafNode(node), "QuadTree::splitNode", "Only leaves can be split");

    // Create children
    for (auto& child : node->children)
    {
        child = std::make_unique<Node>();
    }
      
    // Assign values to children
    auto newValues = std::vector<T>(); // New values for this node
    for (const auto& value : node->values)
    {
        auto i = getQuadrant(box, getBox(value));
        if (i)
        {
            node->children[i.value()]->values.push_back(value);
        }
        else
        {
            newValues.push_back(value);
        }
    }

    node->values = std::move(newValues);
}

template <typename T, typename BoxGetter, typename EqualityTest, typename InternalPrimitive>
void QuadTree<T, BoxGetter, EqualityTest, InternalPrimitive>::tryMerge(Node* node)
{
    Q_ASSERT(node != nullptr);
    Q_ASSERT_X(!QuadTree::isLeafNode(node), "QuadTree::tryMerge", "Only interior nodes can be merged");

    auto nbValues = node->values.size();
    for (const auto& child : node->children)
    {
        if (!isLeaf(child.get()))
            return;
        nbValues += child->values.size();
    }

    if (nbValues <= threshold)
    {
        node->values.reserve(nbValues);
        // Merge the values of all the children
        for (const auto& child : node->children)
        {
            for (const auto& value : child->values)
                node->values.push_back(value);
        }
        // Remove the children
        for (auto& child : node->children)
            child.reset();
    }
}

template <typename T, typename BoxGetter, typename EqualityTest, typename InternalPrimitive>
void QuadTree<T, BoxGetter, EqualityTest, InternalPrimitive>::removalHelperInternal(Node* node, const T& value)
{
    // Find the value in node->values
    auto it = std::find_if(std::begin(node->values), std::end(node->values),
        [this, &value](const auto& rhs) { return isEqual(value, rhs); });

    Q_ASSERT_X(it != std::end(node->values), "QuadTree::removalHelperInternal", "Trying to remove a value that is not present in the node");

    // Swap with the last element and pop back
    *it = std::move(node->values.back());
    node->values.pop_back();
}
