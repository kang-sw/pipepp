#pragma once
#include "pipepp/impl/pipeline.hxx"
#include "pipepp/options.hpp"
#include <sstream>

namespace pipepp {
using options = const detail::option_base;
}

#ifndef ___PIPEPP_CONCAT
#define ___PIPEPP_CONCAT_2(A, B) A##B
#define ___PIPEPP_CONCAT(A, B) ___PIPEPP_CONCAT_2(A, B)
#define ___PIPEPP_TO_STR_2(A) #A
#define ___PIPEPP_TO_STR(A) ___PIPEPP_TO_STR_2(A)
#endif

/**
 * PIPEPP_DEFINE_OPTION(TYPE, NAME, DEFAULT_VALUE [, CATEGORY[, DESCRIPTION]])
 */
#define PIPEPP_OPTION_FULL(TYPE, NAME, DEFAULT_VALUE, ...)                            \
    inline static const ::pipepp::detail::_option_instance<___executor_type___, TYPE> \
      NAME { ::pipepp::detail::path_tostr(__FILE__, __LINE__), (DEFAULT_VALUE), #NAME, ##__VA_ARGS__ }

#define PIPEPP_OPTION_AUTO(NAME, DEFAULT_VALUE, ...) \
    PIPEPP_OPTION_FULL(decltype(DEFAULT_VALUE), NAME, DEFAULT_VALUE, __VA_ARGS__)

#define PIPEPP_OPTION_CAT(NAME, DEFAULT_VALUE, ...) \
    PIPEPP_OPTION_AUTO(NAME, DEFAULT_VALUE, ___category___, __VA_ARGS__)

#define PIPEPP_OPTION_CAT_DESC(NAME, DEFAULT_VALUE, DESC, ...) \
    PIPEPP_OPTION_CAT(NAME, DEFAULT_VALUE, (const char*)(DESC), __VA_ARGS__)

#define PIPEPP_DECLARE_OPTION_CATEGORY(CATEGORY) inline static const std::string ___category___ = (CATEGORY)
#define PIPEPP_DECLARE_OPTION_CLASS(EXECUTOR) \
    using ___executor_type___ = EXECUTOR;     \
    PIPEPP_DECLARE_OPTION_CATEGORY("")

#define PIPEPP_CATEGORY(CLASS, CATEGORY)                                       \
    struct ___category_##CLASS {                                               \
    private:                                                                   \
        inline static const std::string ___outer_category___ = ___category___; \
                                                                               \
    public:                                                                    \
        inline static const std::string ___category___                         \
          = (___outer_category___.empty()                                      \
               ? ""                                                            \
               : ___outer_category___ + ".")                                   \
            + std::string(CATEGORY);                                           \
    };                                                                         \
    struct CLASS : ___category_##CLASS

#define ___PIPEPP_OPTION_4(NAME, VALUE, DESC, VERIFY) PIPEPP_OPTION_CAT_DESC(NAME, VALUE, DESC, VERIFY)
#define ___PIPEPP_OPTION_3(NAME, VALUE, DESC) PIPEPP_OPTION_CAT_DESC(NAME, VALUE, DESC)
#define ___PIPEPP_OPTION_2(NAME, VALUE) PIPEPP_OPTION_CAT(NAME, VALUE)
#define ___PIPEPP_OPTION_1(NAME) static_assert(false)

#ifndef ___PIPEPP_VARIADIC_MACRO_NAMES
#define ___PIPEPP_MSVC_BUG_REOLSVER(MACRO, ARGS) MACRO ARGS // name to remind that bug fix is due to MSVC :-)

#define ___PIPEPP_NUM_ARGS_2(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, TOTAL, ...) TOTAL
#define ___PIPEPP_NUM_ARGS_1(...) ___PIPEPP_MSVC_BUG_REOLSVER(___PIPEPP_NUM_ARGS_2, (__VA_ARGS__))
#define ___PIPEPP_NUM_ARGS(...) ___PIPEPP_NUM_ARGS_1(__VA_ARGS__, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define ___PIPEPP_VA_MACRO(MACRO, ...)                \
    MSVC_BUG(CONCATE, (MACRO, NUM_ARGS(__VA_ARGS__))) \
    (__VA_ARGS__)
#endif

#define ___PIPEPP_ELAPSE_SCOPE(NAME)                                               \
    constexpr kangsw::hash_pack ___PIPEPP_CONCAT(___TIMER_HASH_, __LINE__) = NAME; \
    auto ___PIPEPP_CONCAT(___TIMER_SCOPE_, __LINE__) = ___call_PIPEPP_REGISTER_CONTEXT.timer_scope(___PIPEPP_CONCAT(___TIMER_HASH_, __LINE__));

#define ___PIPEPP_ELAPSE_BLOCK(NAME)                                               \
    constexpr kangsw::hash_pack ___PIPEPP_CONCAT(___TIMER_HASH_, __LINE__) = NAME; \
    if (auto ___PIPEPP_CONCAT(___TIMER_SCOPE_, __LINE__) = ___call_PIPEPP_REGISTER_CONTEXT.timer_scope(___PIPEPP_CONCAT(___TIMER_HASH_, __LINE__)); true)

#define ___PIPEPP_ELAPSE_SCOPE_DYNAMIC(NAME) \
    auto ___PIPEPP_CONCAT(___TIMER_SCOPE_, __LINE__) = ___call_PIPEPP_REGISTER_CONTEXT.timer_scope(NAME);

#define ___PIPEPP_STORE_DEBUG_DATA(NAME, VALUE)                                   \
    constexpr kangsw::hash_pack ___PIPEPP_CONCAT(___DATA_HASH_, __LINE__) = NAME; \
    ___call_PIPEPP_REGISTER_CONTEXT.store_debug_data(___PIPEPP_CONCAT(___DATA_HASH_, __LINE__), (VALUE));

#define ___PIPEPP_STORE_DEBUG_DATA_COND(NAME, VALUE, COND)                                                    \
    if (COND) {                                                                                               \
        constexpr kangsw::hash_pack ___PIPEPP_CONCAT(___DATA_HASH_, __LINE__) = NAME;                         \
        ___call_PIPEPP_REGISTER_CONTEXT.store_debug_data(___PIPEPP_CONCAT(___DATA_HASH_, __LINE__), (VALUE)); \
    }

/**
 *
 * Declares new pipeline option for scope and category.
 */
#define PIPEPP_OPTION(...)                                  \
    ___PIPEPP_MSVC_BUG_REOLSVER(                            \
      ___PIPEPP_CONCAT,                                     \
      (___PIPEPP_OPTION_, ___PIPEPP_NUM_ARGS(__VA_ARGS__))) \
    (__VA_ARGS__)

#define PIPEPP_REGISTER_CONTEXT(CONTEXT) auto& ___call_PIPEPP_REGISTER_CONTEXT = (CONTEXT)
#define PIPEPP_ELAPSE_SCOPE(NAME) ___PIPEPP_ELAPSE_SCOPE(NAME)
#define PIPEPP_ELAPSE_BLOCK(NAME) ___PIPEPP_ELAPSE_BLOCK(NAME)
#define PIPEPP_ELAPSE_SCOPE_DYNAMIC(NAME) ___PIPEPP_ELAPSE_SCOPE_DYNAMIC(NAME)
#define PIPEPP_STORE_DEBUG_DATA_DYNAMIC(NAME, VALUE) ___call_PIPEPP_REGISTER_CONTEXT.store_debug_data(NAME, (VALUE));
#define PIPEPP_STORE_DEBUG_DATA(NAME, VALUE) ___PIPEPP_STORE_DEBUG_DATA(NAME, VALUE)
#define PIPEPP_STORE_DEBUG_DATA_DYNAMIC_STR(NAME, VALUE) ___call_PIPEPP_REGISTER_CONTEXT.store_debug_data(NAME, (std::stringstream{} << VALUE).str());
#define PIPEPP_STORE_DEBUG_STR(NAME, VALUE) ___PIPEPP_STORE_DEBUG_DATA(NAME, (std::stringstream{} << VALUE).str())
#define PIPEPP_CAPTURE_DEBUG_DATA(VALUE) ___PIPEPP_STORE_DEBUG_DATA(#VALUE, VALUE)
#define PIPEPP_STORE_DEBUG_DATA_COND(NAME, VALUE, COND) ___PIPEPP_STORE_DEBUG_DATA_COND(NAME, VALUE, COND)
#define PIPEPP_CAPTURE_DEBUG_DATA_COND(VALUE, COND) ___PIPEPP_STORE_DEBUG_DATA_COND(#VALUE, VALUE, COND)

#define PIPEPP_EXECUTOR(EXECUTOR_NAME) struct EXECUTOR_NAME : public pipepp::detail::___pipepp_executor_base<EXECUTOR_NAME>