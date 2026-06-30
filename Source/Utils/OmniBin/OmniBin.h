#pragma once
#include <fstream>
#include <vector>
#include <tuple>

template<decltype(std::ios::in)>
class OmniBin;

// Big opt-in optimization
template<typename T>
constexpr bool serializeAsPOD = std::is_fundamental_v<T>;

// *******************************************************************************
// ALL non-POD types need to overload omniSave and omniLoad!
// *******************************************************************************

//////////////////////////////////////////////////////////////////////////////////
// Reader / Writer class
//////////////////////////////////////////////////////////////////////////////////
template<decltype(std::ios::in) mode>
class OmniBin
{
    constexpr int getFinalMode()
    {
        if constexpr (mode == std::ios::out)
            return mode | std::ios::binary | std::ios::trunc;
        else
            return mode | std::ios::binary;
    }

public:
    OmniBin(const std::string& filename)
    {
        stream.open(filename, getFinalMode());
        Q_ASSERT(stream.good());
    }

    ~OmniBin()
    {
        stream.close();
    }

    template<typename T>
    OmniBin<mode>& operator>>(T& object)
    {
        static_assert(mode == std::ios::in);
        omniLoad(object, *this);
        return *this;
    }

    template<typename T>
    OmniBin<mode>& operator<<(const T& object)
    {
        static_assert(mode == std::ios::out);
        omniSave(object, *this);
        return *this;
    }

    std::fstream stream;
};

//////////////////////////////////////////////////////////////////////////////////
// Base Save / Load implementation
//////////////////////////////////////////////////////////////////////////////////
template<typename T>
inline void omniSave(const T& object, OmniBin<std::ios::out>& omniBin)
{
    static_assert(!std::is_pointer_v<T>, "Can't serialize raw pointers! Use QSharedPointer or QScopedPointer instead.");

    if constexpr (!is_iterable_v<T>)
    {
        if constexpr (is_dereferenceable_v<T>)
        {
            bool hasValue = object;
            omniSave(hasValue, omniBin);

            if (hasValue)
                omniSave(*object, omniBin);
        }
        else
        {
            omniBin.stream.write(reinterpret_cast<const char*>(&object), sizeof(T));
        }
    }
    else
    {
        omniSave(object.size(), omniBin);
        for (auto&& it : object)
            omniSave(it, omniBin);
    }
}

template<typename T>
inline void omniLoad(T& object, OmniBin<std::ios::in>& omniBin)
{
    static_assert(!std::is_pointer_v<T>, "Can't serialize raw pointers! Use QSharedPointer or QScopedPointer instead.");
    static_assert(!std::is_const_v<T>, "Use const_cast to load const objects");

    if constexpr (!is_iterable_v<T>)
    {
        if constexpr (is_dereferenceable_v<T>)
        {
            // Smart pointers
            bool hasValue;
            omniLoad(hasValue, omniBin);

            if (hasValue)
            {
                object.reset(new typename std::pointer_traits<T>::element_type);
                omniLoad(*object, omniBin);
            }
        }
        else
        {
            // Non-iterable types
            omniBin.stream.read(reinterpret_cast<char*>(&object), sizeof(T));
        }
    }
    else
    {
        using size_type = decltype(object.size());
        size_type s;
        omniLoad(s, omniBin);

        for (size_type i = 0; i < s; ++i)
        {
            typename T::value_type t;
            omniLoad(t, omniBin);
            object.insert(std::move(t));
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////
// Overloads
//////////////////////////////////////////////////////////////////////////////////
// String
template<typename T>
inline void omniSave(const std::basic_string<T>& object, OmniBin<std::ios::out>& omniBin)
{
    // Cast size to int for QString compatibility
    omniSave(int(object.size()), omniBin);
    omniBin.stream.write(reinterpret_cast<const char*>(object.data()), object.size());
}

template<typename T>
inline void omniLoad(std::basic_string<T>& object, OmniBin<std::ios::in>& omniBin)
{
    int s;
    omniLoad(s, omniBin);
    object.resize(s);

    omniBin.stream.read(reinterpret_cast<char*>(object.data()), object.size());
}

// std::vector
template<typename T>
inline void omniSave(const std::vector<T>& object, OmniBin<std::ios::out>& omniBin)
{
    static_assert(!std::is_pointer_v<T>, "Can't serialize raw pointers! Use QSharedPointer or QScopedPointer instead.");

    omniSave(object.size(), omniBin);
    if constexpr (serializeAsPOD<T>)
    {
        if (object.size() > 0)
            omniBin.stream.write(reinterpret_cast<const char*>(&object[0]), sizeof(T) * object.size());
    }
    else
    {
        for (auto&& it : object)
            omniSave(it, omniBin);
    }
}

template<typename T>
inline void omniLoad(std::vector<T>& object, OmniBin<std::ios::in>& omniBin)
{
    static_assert(!std::is_pointer_v<T>, "Can't serialize raw pointers! Use QSharedPointer or QScopedPointer instead.");

    size_t s;
    omniLoad(s, omniBin);
    object.resize(s);

    if constexpr (serializeAsPOD<T>)
    {
        if (s > 0)
            omniBin.stream.read(reinterpret_cast<char*>(&object[0]), sizeof(T) * s);
    }
    else
    {
        for (size_t i = 0; i < s; ++i)
            omniLoad(object[i], omniBin);
    }
}

// std::array
template<typename T, size_t N>
inline void omniSave(const std::array<T, N>& object, OmniBin<std::ios::out>& omniBin)
{
    static_assert(!std::is_pointer_v<T>, "Can't serialize raw pointers! Use QSharedPointer or QScopedPointer instead.");

    omniSave(N, omniBin);
    if constexpr (serializeAsPOD<T>)
    {
        omniBin.stream.write(reinterpret_cast<const char*>(&object[0]), sizeof(T) * N);
    }
    else
    {
        for (auto&& it : object)
            omniSave(it, omniBin);
    }
}

template<typename T, size_t N>
inline void omniLoad(std::array<T, N>& object, OmniBin<std::ios::in>& omniBin)
{
    static_assert(!std::is_pointer_v<T>, "Can't serialize raw pointers! Use QSharedPointer or QScopedPointer instead.");

    size_t s;
    omniLoad(s, omniBin);

    if constexpr (serializeAsPOD<T>)
    {
        omniBin.stream.read(reinterpret_cast<char*>(&object[0]), sizeof(T) * s);
    }
    else
    {
        for (size_t i = 0; i < s; ++i)
            omniLoad(object[i], omniBin);
    }
}

// std::list
template<typename T>
inline void omniLoad(std::list<T>& object, OmniBin<std::ios::in>& omniBin)
{
	size_t s;
	omniLoad(s, omniBin);

	for (size_t i = 0; i < s; ++i)
	{
		T value;
        omniLoad(value , omniBin);
        object.push_back(std::move(value));
	}
}

// std::map
template<typename K, typename V>
inline void omniSave(const std::map<K, V>& object, OmniBin<std::ios::out>& omniBin)
{
    size_t s = object.size();
    omniSave(s, omniBin);

    for (auto&& [k, v] : object)
    {
        omniSave(k, omniBin);
        omniSave(v, omniBin);
    }
}

template<typename K, typename V>
inline void omniLoad(std::map<K, V>& object, OmniBin<std::ios::in>& omniBin)
{
    size_t s;
    omniLoad(s, omniBin);

    for (size_t i = 0; i < s; ++i)
    {
        std::pair<K, V> keyValue;
        auto&& [k, v] = keyValue;
        omniLoad(k, omniBin);
        omniLoad(v, omniBin);
        object[k] = std::move(v);
    }
}

// Pair
template<typename T0, typename T1>
inline void omniSave(const std::pair<T0, T1>& object, OmniBin<std::ios::out>& omniBin)
{
    omniSave(const_cast<std::add_const_t<T0>&>(object.first), omniBin);
    omniSave(const_cast<std::add_const_t<T1>&>(object.second), omniBin);
}

template<typename T0, typename T1>
inline void omniLoad(std::pair<T0, T1>& object, OmniBin<std::ios::in>& omniBin)
{
    omniLoad(const_cast<std::remove_const_t<T0>&>(object.first), omniBin);
    omniLoad(const_cast<std::remove_const_t<T1>&>(object.second), omniBin);
}

// Tuples
template<int id = 0, typename... Ts>
inline void omniSave(const std::tuple<Ts...>& object, OmniBin<std::ios::out>& omniBin)
{
    if constexpr (id < std::tuple_size_v<std::tuple<Ts...>>)
    {
        using TargetType = std::add_const_t<std::tuple_element_t<id, std::tuple<Ts...>>>;
        omniSave(const_cast<TargetType&>(std::get<id>(object)), omniBin);
        omniSave<id + 1>(object, omniBin);
    }
}

template<int id = 0, typename... Ts>
inline void omniLoad(std::tuple<Ts...>& object, OmniBin<std::ios::in>& omniBin)
{
    if constexpr (id < std::tuple_size_v<std::tuple<Ts...>>)
    {
        using TargetType = std::remove_const_t<std::tuple_element_t<id, std::tuple<Ts...>>>;
        omniLoad(const_cast<TargetType&>(std::get<id>(object)), omniBin);
        omniLoad<id + 1>(object, omniBin);
    }
}

// std::optional
template<typename T>
inline void omniSave(const std::optional<T>& object, OmniBin<std::ios::out>& omniBin)
{
    omniSave(bool(object), omniBin);
    if (object)
        omniSave(*object, omniBin);
}

template<typename T>
inline void omniLoad(std::optional<T>& object, OmniBin<std::ios::in>& omniBin)
{
    bool hasValue;
    omniLoad(hasValue, omniBin);
    if (hasValue)
    {
        T value;
        omniLoad(value, omniBin);
        object = std::move(value);
    }
}

#define FRIEND_OMNIBIN(Type) \
template<typename T> \
friend void omniSave(const T&, OmniBin<std::ios::out>&); \
template<typename T> \
friend void omniLoad(T&, OmniBin<std::ios::in>&); \
\
friend void omniSave(const Type&, OmniBin<std::ios::out>&); \
friend void omniLoad(Type&, OmniBin<std::ios::in>&);

#define FRIEND_OMNIBIN_NS(Type) \
template<typename T> \
friend void ::omniSave(const T&, OmniBin<std::ios::out>&); \
template<typename T> \
friend void ::omniLoad(T&, OmniBin<std::ios::in>&); \
\
friend void ::omniSave(const Type&, OmniBin<std::ios::out>&); \
friend void ::omniLoad(Type&, OmniBin<std::ios::in>&);

#define FRIEND_OMNIBIN_T(Type) \
template<typename T> \
friend void omniSave(const T&, OmniBin<std::ios::out>&); \
template<typename T> \
friend void omniLoad(T&, OmniBin<std::ios::in>&); \
\
template<typename T> \
friend void omniSave(const Type<##T##>&, OmniBin<std::ios::out>&); \
template<typename T> \
friend void omniLoad(Type<##T##>&, OmniBin<std::ios::in>&); \

#define FRIEND_OMNIBIN_T_NS(Type) \
template<typename T> \
friend void ::omniSave(const T&, OmniBin<std::ios::out>&); \
template<typename T> \
friend void ::omniLoad(T&, OmniBin<std::ios::in>&); \
\
template<typename T> \
friend void ::omniSave(const Type<##T##>&, OmniBin<std::ios::out>&); \
template<typename T> \
friend void ::omniLoad(Type<##T##>&, OmniBin<std::ios::in>&); \