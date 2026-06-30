#pragma once

// Apply this to use dynamic enum values as template parameters.
// Generic systems utility, where deserialized enum values can be used to create different data types.
// Generates binary search logic tree in compile time.
// For example usage, see TestEnumTraits.cpp.
template<typename Enum>
struct EnumConstexpr
{
    using Interface = void;
};

#define ENABLE_ENUM_AS_CONSTEXPR(enum_type, upper_bound) \
template<short L, short R, short MID = (L + R) / 2, bool VALID = ((L <= R) && (R <= static_cast<short>(upper_bound)))> \
struct enum_type##Constexpr_Internal \
{ \
    template<typename FunctorType, typename... Args> \
    inline static decltype(auto) UseIn(enum_type e, Args&&... args) \
    { \
        if (static_cast<short>(e) < MID) \
        { \
            return enum_type##Constexpr_Internal<L, MID - 1>::template UseIn<FunctorType>(e, std::forward<Args>(args)...); \
        } \
        else if (static_cast<short>(e) > MID) \
        { \
            return enum_type##Constexpr_Internal<MID + 1, R>::template UseIn<FunctorType>(e, std::forward<Args>(args)...); \
        } \
        else \
        { \
            return FunctorType::template Action<static_cast<enum_type>(MID)>(std::forward<Args>(args)...); \
        } \
    } \
    \
    template<typename FunctorType, short I = 0, typename... Args> \
    inline static void UseAllIn(Args&&... args) \
    { \
        FunctorType::template Action<static_cast<enum_type>(I)>(std::forward<Args>(args)...); \
        \
        if constexpr (I < static_cast<short>(upper_bound)) \
            UseAllIn<FunctorType, I + 1>(std::forward<Args>(args)...); \
    } \
    \
    template<typename PredicateType, typename... Args> \
    inline static enum_type FindIf(Args&&... args) \
    { \
        if (PredicateType::template Check<static_cast<enum_type>(L)>(std::forward<Args>(args)...)) \
        { \
            return static_cast<enum_type>(L); \
        } \
        else \
        { \
            return enum_type##Constexpr_Internal<L+1, R>::template FindIf<PredicateType>(std::forward<Args>(args)...); \
        } \
    } \
    \
}; \
\
template<short L, short R, short MID> \
struct enum_type##Constexpr_Internal<L, R, MID, false> \
{ \
    template<typename FunctorType, typename... Args> \
    inline static decltype(auto) UseIn(enum_type, Args&&... args) \
    { \
        return FunctorType::template Action<upper_bound>(std::forward<Args>(args)...); \
    } \
    \
    template<typename PredicateType, typename... Args> \
    inline static enum_type FindIf(Args&&...) \
    { \
        return upper_bound; \
    } \
}; \
\
using enum_type##Constexpr = enum_type##Constexpr_Internal<0, static_cast<short>(upper_bound)>; \
template<> \
struct EnumConstexpr<enum_type> \
{ \
    using Interface = enum_type##Constexpr; \
};
