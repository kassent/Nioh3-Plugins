#pragma once
#include <assert.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <string_view>


class RelocationManager {
public:
  RelocationManager();

  static uintptr_t s_baseAddr;
};

#define REL_MAKE_MEMBER_FUNCTION_POD_TYPE_HELPER_IMPL(a_nopropQual,            \
                                                      a_propQual, ...)         \
  template <class R, class Cls, class... Args>                                 \
  struct member_function_pod_type<R (Cls::*)(Args...)                          \
                                      __VA_ARGS__ a_nopropQual a_propQual> {   \
    using type = R(__VA_ARGS__ Cls *, Args...) a_propQual;                     \
  };                                                                           \
                                                                               \
  template <class R, class Cls, class... Args>                                 \
  struct member_function_pod_type<R (Cls::*)(Args..., ...)                     \
                                      __VA_ARGS__ a_nopropQual a_propQual> {   \
    using type = R(__VA_ARGS__ Cls *, Args..., ...) a_propQual;                \
  };

#define REL_MAKE_MEMBER_FUNCTION_POD_TYPE_HELPER(a_qualifer, ...)              \
  REL_MAKE_MEMBER_FUNCTION_POD_TYPE_HELPER_IMPL(a_qualifer, , ##__VA_ARGS__)   \
  REL_MAKE_MEMBER_FUNCTION_POD_TYPE_HELPER_IMPL(a_qualifer, noexcept,          \
                                                ##__VA_ARGS__)

#define REL_MAKE_MEMBER_FUNCTION_POD_TYPE(...)                                 \
  REL_MAKE_MEMBER_FUNCTION_POD_TYPE_HELPER(, __VA_ARGS__)                      \
  REL_MAKE_MEMBER_FUNCTION_POD_TYPE_HELPER(&, ##__VA_ARGS__)                   \
  REL_MAKE_MEMBER_FUNCTION_POD_TYPE_HELPER(&&, ##__VA_ARGS__)

#define REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE_HELPER_IMPL(a_nopropQual,        \
                                                          a_propQual, ...)     \
  template <class R, class Cls, class... Args>                                 \
  struct member_function_non_pod_type<R (Cls::*)(                              \
      Args...) __VA_ARGS__ a_nopropQual a_propQual> {                          \
    using type = R &(__VA_ARGS__ Cls *, void *, Args...)a_propQual;            \
  };                                                                           \
                                                                               \
  template <class R, class Cls, class... Args>                                 \
  struct member_function_non_pod_type<R (Cls::*)(                              \
      Args..., ...) __VA_ARGS__ a_nopropQual a_propQual> {                     \
    using type = R &(__VA_ARGS__ Cls *, void *, Args..., ...)a_propQual;       \
  };

#define REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE_HELPER(a_qualifer, ...)          \
  REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE_HELPER_IMPL(a_qualifer, ,              \
                                                    ##__VA_ARGS__)             \
  REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE_HELPER_IMPL(a_qualifer, noexcept,      \
                                                    ##__VA_ARGS__)

#define REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE(...)                             \
  REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE_HELPER(, __VA_ARGS__)                  \
  REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE_HELPER(&, ##__VA_ARGS__)               \
  REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE_HELPER(&&, ##__VA_ARGS__)

namespace REL {

class Offset;

template <class> class Relocation;

template <class Enum>
[[nodiscard]] constexpr auto to_underlying(Enum a_val) noexcept //
  requires(std::is_enum_v<Enum>)
{
  return static_cast<std::underlying_type_t<Enum>>(a_val);
}

template <class To, class From>
[[nodiscard]] To unrestricted_cast(From a_from) {
  if constexpr (std::is_same_v<std::remove_cv_t<From>, std::remove_cv_t<To>>) {
    return To{a_from};

    // From != To
  } else if constexpr (std::is_reference_v<From>) {
    return unrestricted_cast<To>(std::addressof(a_from));

    // From: NOT reference
  } else if constexpr (std::is_reference_v<To>) {
    return *unrestricted_cast<std::add_pointer_t<std::remove_reference_t<To>>>(
        a_from);

    // To: NOT reference
  } else if constexpr (std::is_pointer_v<From> && std::is_pointer_v<To>) {
    return static_cast<To>(
        const_cast<void *>(static_cast<const volatile void *>(a_from)));
  } else if constexpr ((std::is_pointer_v<From> && std::is_integral_v<To>) ||
                       (std::is_integral_v<From> && std::is_pointer_v<To>)) {
    return reinterpret_cast<To>(a_from);
  } else {
    union {
      std::remove_cv_t<std::remove_reference_t<From>> from;
      std::remove_cv_t<std::remove_reference_t<To>> to;
    };

    from = std::forward<From>(a_from);
    return to;
  }
}

namespace detail {
template <class> struct member_function_pod_type;

REL_MAKE_MEMBER_FUNCTION_POD_TYPE();
REL_MAKE_MEMBER_FUNCTION_POD_TYPE(const);
REL_MAKE_MEMBER_FUNCTION_POD_TYPE(volatile);
REL_MAKE_MEMBER_FUNCTION_POD_TYPE(const volatile);

template <class F>
using member_function_pod_type_t = typename member_function_pod_type<F>::type;

template <class> struct member_function_non_pod_type;

REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE();
REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE(const);
REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE(volatile);
REL_MAKE_MEMBER_FUNCTION_NON_POD_TYPE(const volatile);

template <class F>
using member_function_non_pod_type_t =
    typename member_function_non_pod_type<F>::type;

// https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention

template <class T>
struct meets_length_req : std::disjunction<std::bool_constant<sizeof(T) == 1>,
                                           std::bool_constant<sizeof(T) == 2>,
                                           std::bool_constant<sizeof(T) == 4>,
                                           std::bool_constant<sizeof(T) == 8>> {
};

template <class T>
struct meets_function_req
    : std::conjunction<std::is_trivially_constructible<T>,
                       std::is_trivially_destructible<T>,
                       std::is_trivially_copy_assignable<T>,
                       std::negation<std::is_polymorphic<T>>> {};

template <class T> struct meets_member_req : std::is_standard_layout<T> {};

template <class T, class = void> struct is_x64_pod : std::true_type {};

template <class T>
struct is_x64_pod<T, std::enable_if_t<std::is_union_v<T>>> : std::false_type {};

template <class T>
struct is_x64_pod<T, std::enable_if_t<std::is_class_v<T>>>
    : std::conjunction<meets_length_req<T>, meets_function_req<T>,
                       meets_member_req<T>> {};

template <class T> inline constexpr bool is_x64_pod_v = is_x64_pod<T>::value;

template <class F, class First, class... Rest>
decltype(auto) invoke_member_function_non_pod(F &&a_func, First &&a_first,
                                              Rest &&...a_rest) //
    noexcept(std::is_nothrow_invocable_v<F, First, Rest...>) {
  using result_t = std::invoke_result_t<F, First, Rest...>;
  std::aligned_storage_t<sizeof(result_t), alignof(result_t)> result;

  using func_t = member_function_non_pod_type_t<F>;
  auto func = unrestricted_cast<func_t *>(std::forward<F>(a_func));

  return func(std::forward<First>(a_first), std::addressof(result),
              std::forward<Rest>(a_rest)...);
}
} // namespace detail

template <class F, class... Args>
std::invoke_result_t<F, Args...> invoke(F &&a_func, Args &&...a_args) //
    noexcept(std::is_nothrow_invocable_v<F, Args...>)                 //
  requires(std::invocable<F, Args...>)
{
  if constexpr (std::is_member_function_pointer_v<std::decay_t<F>>) {
    if constexpr (detail::is_x64_pod_v<std::invoke_result_t<
                      F, Args...>>) { // member functions == free functions in
                                      // x64
      using func_t = detail::member_function_pod_type_t<std::decay_t<F>>;
      auto func = unrestricted_cast<func_t *>(std::forward<F>(a_func));
      return func(std::forward<Args>(a_args)...);
    } else { // shift args to insert result
      return detail::invoke_member_function_non_pod(
          std::forward<F>(a_func), std::forward<Args>(a_args)...);
    }
  } else {
    return std::forward<F>(a_func)(std::forward<Args>(a_args)...);
  }
}

class Offset {
public:
  constexpr Offset() noexcept = default;

  explicit constexpr Offset(std::size_t a_offset) noexcept
      : _offset(a_offset) {}

  constexpr Offset &operator=(std::size_t a_offset) noexcept {
    _offset = a_offset;
    return *this;
  }

  [[nodiscard]] std::uintptr_t address() const { return base() + offset(); }
  [[nodiscard]] constexpr std::size_t offset() const noexcept {
    return _offset;
  }

private:
  [[nodiscard]] static std::uintptr_t base() {
    return RelocationManager::s_baseAddr;
  }

  std::size_t _offset{0};
};

class Pattern {
  public:
    constexpr Pattern() noexcept = delete;

    constexpr Pattern(std::uintptr_t a_offset, std::string_view a_signature,
                      std::int32_t a_dstOffset = 0,
                      std::int32_t a_dataOffset = 0,
                      std::int32_t a_instructionLength = 0) noexcept
        : _offset(a_offset), _signature(a_signature), _dstOffset(a_dstOffset),
          _dataOffset(a_dataOffset), _instructionLength(a_instructionLength) {}

  [[nodiscard]] std::uintptr_t address() const;
  private:
    Offset _offset; // base address of the pattern
    std::string_view _signature; // pattern string
    std::int32_t _dstOffset{0}; // destination offset
    std::int32_t _dataOffset{0}; // data offset
    std::int32_t _instructionLength{0}; // instruction length
};

template <class T> class Relocation {
public:
  using value_type =
      std::conditional_t<std::is_member_pointer_v<T> ||
                             std::is_function_v<std::remove_pointer_t<T>>,
                         std::decay_t<T>, T>;

  constexpr Relocation() noexcept = default;

  explicit constexpr Relocation(std::uintptr_t a_address) noexcept
      : _impl{a_address} {}

  explicit Relocation(Offset a_offset) : _impl{a_offset.address()} {}

  explicit Relocation(Pattern a_pattern) : _impl{a_pattern.address()} {}

  constexpr Relocation &operator=(std::uintptr_t a_address) noexcept {
    _impl = a_address;
    return *this;
  }

  Relocation &operator=(Offset a_offset) {
    _impl = a_offset.address();
    return *this;
  }

  template <class U = value_type>
  [[nodiscard]] decltype(auto) operator*() const noexcept //
    requires(std::is_pointer_v<U>)
  {
    return *get();
  }

  template <class U = value_type>
  [[nodiscard]] auto operator->() const noexcept //
    requires(std::is_pointer_v<U>)
  {
    return get();
  }

  template <class... Args>
  std::invoke_result_t<const value_type &, Args...>
  operator()(Args &&...a_args) const                                     //
      noexcept(std::is_nothrow_invocable_v<const value_type &, Args...>) //
    requires(std::invocable<const value_type &, Args...>)
  {
    return REL::invoke(get(), std::forward<Args>(a_args)...);
  }

  [[nodiscard]] constexpr std::uintptr_t address() const noexcept {
    return _impl;
  }
  [[nodiscard]] std::size_t offset() const { return _impl - base(); }

  [[nodiscard]] value_type get() const //
      noexcept(std::is_nothrow_copy_constructible_v<value_type>) {
    assert(_impl != 0);
    return unrestricted_cast<value_type>(_impl);
  }

private:
  // clang-format off
	[[nodiscard]] static std::uintptr_t base() { return RelocationManager::s_baseAddr; }
  // clang-format on

  std::uintptr_t _impl{0};
};

} // namespace REL

#define FORCE_INLINE __forceinline

#define DEF_MEMBER_FN(fnName, retnType, addr, ...)                             \
  template <class... Params>                                                   \
  FORCE_INLINE retnType fnName(Params &&...params) {                           \
    struct empty_struct {};                                                    \
    typedef retnType (empty_struct::*_##fnName##_type)(__VA_ARGS__);           \
    const static uintptr_t address = _##fnName##_GetFnPtr();                   \
    _##fnName##_type fn = *(_##fnName##_type *)&address;                       \
    return (reinterpret_cast<empty_struct *>(this)->*fn)(params...);           \
  }                                                                            \
  static uintptr_t &_##fnName##_GetFnPtr() {                                   \
    static uintptr_t relMem = REL::Offset(addr).address();                     \
    return relMem;                                                             \
  }

#define DEF_MEMBER_FN_CONST(fnName, retnType, addr, ...)                       \
  template <class... Params>                                                   \
  FORCE_INLINE retnType fnName(Params &&...params) const {                     \
    struct empty_struct {};                                                    \
    typedef retnType (empty_struct::*_##fnName##_type)(__VA_ARGS__) const;     \
    const static uintptr_t address = _##fnName##_GetFnPtr();                   \
    _##fnName##_type fn = *(_##fnName##_type *)&address;                       \
    return (reinterpret_cast<const empty_struct *>(this)->*fn)(params...);     \
  }                                                                            \
  static uintptr_t &_##fnName##_GetFnPtr() {                                   \
    static uintptr_t relMem = REL::Offset(addr).address();                     \
    return relMem;                                                             \
  }


#define DEF_MEMBER_FN_REL(fnName, retnType, addr, signature, dstOffset, dataOffset, instructionLength, ...)                             \
  template <class... Params>                                                   \
  FORCE_INLINE retnType fnName(Params &&...params) {                           \
    struct empty_struct {};                                                    \
    typedef retnType (empty_struct::*_##fnName##_type)(__VA_ARGS__);           \
    const static uintptr_t address = _##fnName##_GetFnPtr();                   \
    _##fnName##_type fn = *(_##fnName##_type *)&address;                       \
    return (reinterpret_cast<empty_struct *>(this)->*fn)(params...);           \
  }                                                                            \
  inline static REL::Relocation<uintptr_t> _##fnName {REL::Pattern(addr, signature, dstOffset, dataOffset, instructionLength) }; \
  static uintptr_t &_##fnName##_GetFnPtr() {                                   \
    static uintptr_t relMem = _##fnName.address();                             \
    return relMem;                                                             \
  }

#define DEF_MEMBER_FN_REL_CONST(fnName, retnType, addr, signature, dstOffset, dataOffset, instructionLength, ...)                       \
  template <class... Params>                                                   \
  FORCE_INLINE retnType fnName(Params &&...params) const {                     \
    struct empty_struct {};                                                    \
    typedef retnType (empty_struct::*_##fnName##_type)(__VA_ARGS__) const;     \
    const static uintptr_t address = _##fnName##_GetFnPtr();                   \
    _##fnName##_type fn = *(_##fnName##_type *)&address;                       \
    return (reinterpret_cast<const empty_struct *>(this)->*fn)(params...);     \
  }                                                                            \
  inline static REL::Relocation<uintptr_t> _##fnName {REL::Pattern(addr, signature, dstOffset, dataOffset, instructionLength) }; \
  static uintptr_t &_##fnName##_GetFnPtr() {                                   \
    static uintptr_t relMem = _##fnName.address();                             \
    return relMem;                                                             \
  }
