#pragma once

#include <type_traits>
#include <unordered_map>
#include <functional>
#include <string>
#include <utility>
#include <type_traits>

namespace boost::web::matcher {

struct context
{
  std::unordered_map<std::string, std::string> path_args;
};

template <typename RequestType>
class matcher_base
{
public:
  using request_type = RequestType;

  matcher_base(std::function<bool(const RequestType&, context&)> evaluate_fn)
    : evaluate_fn_ {std::move(evaluate_fn)}
  {}

  bool evaluate(const RequestType& request, context& context) const
  {
    return evaluate_fn_(request, context);
  }

private:
  std::function<bool(const RequestType&, context&)> evaluate_fn_;
};

// A dependent type always evaluating to false_type. This is necessary due to
// a limitation in C++20 which is addressed with P2593 and accepted in C++23
template <typename...> constexpr static std::false_type always_false {};

// Type trait evaluating to the super type between two types
template <typename T, typename U>
struct super_type
{
  using type = std::conditional_t<
    std::is_base_of_v<T, U>,
    U,
    std::conditional_t<
      std::is_base_of_v<U, T>,
      T,
      std::enable_if_t<std::is_same_v<T, U>, T>
    >
  >;
};
template <typename T, typename U>
using super_type_t = typename super_type<T, U>::type;

// Matcher's unary Not operator
template <typename MatcherType>
auto operator!(const MatcherType& matcher)
{
  using request_type = typename MatcherType::request_type;

  return matcher_base<request_type> {
    [matcher](const request_type& request, context& context)
    {
      return !matcher.evaluate(request, context);
    }
  };
}

// Matcher's equality operator
template<typename LHSType, typename RHSType>
auto operator==(const LHSType& lhs, const RHSType& rhs)
{
  if constexpr (requires
    {
      (bool)lhs.equals(
        std::declval<const typename LHSType::request_type&>(),
        std::declval<context&>(),
        std::declval<const RHSType&>());
    })
  {
    using request_type = typename LHSType::request_type;

    return matcher_base<request_type> {
      [lhs, rhs](const request_type& request, context& context)
      {
        return lhs.equals(request, context, rhs);
      }
    };
  }
  else if constexpr (requires
    {
      rhs.equals(
        std::declval<const typename RHSType::request_type&>(),
        std::declval<context&>(),
        std::declval<const LHSType&>());
    })
  {
    using request_type = typename RHSType::request_type;

    return matcher_base<request_type> {
      [lhs, rhs](const request_type& request, context& context)
      {
        return rhs.equals(request, context, lhs);
      }
    };
  }
  else
  {
    static_assert(
      always_false<LHSType, RHSType>,
      "Incompatible operands are used with matcher equality operator.");
  }
}

// Matcher's inequality operator
template<typename LHSType, typename RHSType>
auto operator!=(const LHSType& lhs, const RHSType& rhs)
{
  return !(lhs == rhs);
}

// Matcher's less than operator
template<typename LHSType, typename RHSType>
auto operator<(const LHSType& lhs, const RHSType& rhs)
{
  if constexpr (requires
    {
      (bool)lhs.less(
        std::declval<const typename LHSType::request_type&>(),
        std::declval<context&>(),
        std::declval<const RHSType&>());
    })
  {
    using request_type = typename LHSType::request_type;

    return matcher_base<request_type> {
      [lhs, rhs](const request_type& request, context& context)
      {
        return lhs.less(request, context, rhs);
      }
    };
  }
  else if constexpr (requires
    {
      rhs.greater(
        std::declval<const typename RHSType::request_type&>(),
        std::declval<context&>(),
        std::declval<const LHSType&>());
    })
  {
    using request_type = typename RHSType::request_type;

    return matcher_base<request_type> {
      [lhs, rhs](const request_type& request, context& context)
      {
        return rhs.greater(request, context, lhs);
      }
    };
  }
  else
  {
    static_assert(
      always_false<LHSType, RHSType>,
      "Incompatible operands are used with matcher less than operator.");
  }
}

// Matcher's greater than operator
template<typename LHSType, typename RHSType>
auto operator>(const LHSType& lhs, const RHSType& rhs)
{
  if constexpr (requires
    {
      (bool)lhs.greater(
        std::declval<const typename LHSType::request_type&>(),
        std::declval<context&>(),
        std::declval<const RHSType&>());
    })
  {
    using request_type = typename LHSType::request_type;

    return matcher_base<request_type> {
      [lhs, rhs](const request_type& request, context& context)
      {
        return lhs.greater(request, context, rhs);
      }
    };
  }
  else if constexpr (requires
    {
      rhs.less(
        std::declval<const typename RHSType::request_type&>(),
        std::declval<context&>(),
        std::declval<const LHSType&>());
    })
  {
    using request_type = typename RHSType::request_type;

    return matcher_base<request_type> {
      [lhs, rhs](const request_type& request, context& context)
      {
        return rhs.less(request, context, lhs);
      }
    };
  }
  else
  {
    static_assert(
      always_false<LHSType, RHSType>,
      "Incompatible operands are used with matcher greater than operator.");
  }
}

// Matcher's less than or equal operator
template<typename LHSType, typename RHSType>
auto operator<=(const LHSType& lhs, const RHSType& rhs)
{
  return (lhs < rhs) || (lhs == rhs);
}

// Matcher's greater than or equal operator
template<typename LHSType, typename RHSType>
auto operator>=(const LHSType& lhs, const RHSType& rhs)
{
  return (lhs > rhs) || (lhs == rhs);
}

// Matcher's binary And operator
template<typename LHSType, typename RHSType>
auto operator&&(const LHSType& lhs, const RHSType& rhs)
{
  if constexpr (
    requires {
      (bool)lhs.evaluate(
        std::declval<const typename LHSType::request_type&>(),
        std::declval<context&>());
    } &&
    requires {
      (bool)rhs.evaluate(
        std::declval<const typename RHSType::request_type&>(),
        std::declval<context&>());
    })
  {
    // Super request type
    using request_type = super_type_t<
      typename LHSType::request_type,
      typename RHSType::request_type>;

    return matcher_base<request_type> {
      [lhs, rhs](const request_type& request, context& context)
      {
        return lhs.evaluate(request, context) && rhs.evaluate(request, context);
      }
    };
  }
  else
  {
    static_assert(
      always_false<LHSType, RHSType>,
      "Incompatible operands are used with matcher binary And operator.");
  }
}

// Matcher's binary Or operator
template<typename LHSType, typename RHSType>
auto operator||(const LHSType& lhs, const RHSType& rhs)
{
  if constexpr (
    requires {
      (bool)lhs.evaluate(
        std::declval<const typename LHSType::request_type&>(),
        std::declval<context&>());
    } &&
    requires {
      (bool)rhs.evaluate(
        std::declval<const typename RHSType::request_type&>(),
        std::declval<context&>());
    })
  {
    // Super request type
    using request_type = super_type_t<
      typename LHSType::request_type,
      typename RHSType::request_type>;

    return matcher_base<request_type> {
      [lhs, rhs](const request_type& request, context& context)
      {
        return lhs.evaluate(request, context) || rhs.evaluate(request, context);
      }
    };
  }
  else
  {
    static_assert(
      always_false<LHSType, RHSType>,
      "Incompatible operands are used with matcher binary And operator.");
  }
}

} // namespace boost::web::matcher
