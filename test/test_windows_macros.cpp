//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//
// <windows.h> without NOMINMAX defines function-like min and max macros.
// Taar's own headers must compile after them, which means writing
// (std::min)(a, b) and (std::numeric_limits<T>::max)() in the library.
//
// The standard and Boost headers taar depends on are included first: they
// are include-guarded, so taar's own includes of them become no-ops and
// only taar's code meets the macros. (Boost.Asio defines NOMINMAX itself
// when it includes <windows.h>; libstdc++ undefines min and max in its
// configuration header, which is why the macros come after it.)

#if defined(_WIN32) && !defined(NOMINMAX)
#  define NOMINMAX
#endif

#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <list>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <ranges>
#include <regex>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/multiple_exceptions.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/assert/source_location.hpp>
#include <boost/beast/core/file.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/buffer_body.hpp>
#include <boost/beast/http/chunk_encode.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/rfc7230.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/core/type_name.hpp>
#include <boost/describe/enumerators.hpp>
#include <boost/describe/members.hpp>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_from.hpp>
#include <boost/json/value_to.hpp>
#include <boost/mp11/algorithm.hpp>
#include <boost/optional/optional.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_category.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>
#include <boost/system/system_error.hpp>
#include <boost/url/decode_view.hpp>
#include <boost/url/grammar/all_chars.hpp>
#include <boost/url/grammar/ci_string.hpp>
#include <boost/url/grammar/lut_chars.hpp>
#include <boost/url/ignore_case.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/parse_query.hpp>
#include <boost/url/url_view.hpp>
#include <boost/test/unit_test.hpp>

#undef min
#undef max
#define min(a, b) ((b) < (a) ? (b) : (a))
#define max(a, b) ((a) < (b) ? (b) : (a))

#include <boost/taar/core/accept_encoding.hpp>
#include <boost/taar/core/async_channel.hpp>
#include <boost/taar/core/async_generator.hpp>
#include <boost/taar/core/awaitable.hpp>
#include <boost/taar/core/base64.hpp>
#include <boost/taar/core/body_reader.hpp>
#include <boost/taar/core/byte_range.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/chunk_body_from.hpp>
#include <boost/taar/core/chunk_body_from_tag.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/constexpr_string.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/core/cookies.hpp>
#include <boost/taar/core/cors.hpp>
#include <boost/taar/core/entity_tag.hpp>
#include <boost/taar/core/error.hpp>
#include <boost/taar/core/form_kvp.hpp>
#include <boost/taar/core/http_date.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/core/interceptor.hpp>
#include <boost/taar/core/is_async_generator.hpp>
#include <boost/taar/core/is_awaitable.hpp>
#include <boost/taar/core/is_chunked_response.hpp>
#include <boost/taar/core/is_http_response.hpp>
#include <boost/taar/core/json_schema.hpp>
#include <boost/taar/core/member_function_of.hpp>
#include <boost/taar/core/mime.hpp>
#include <boost/taar/core/multipart.hpp>
#include <boost/taar/core/rebind_executor.hpp>
#include <boost/taar/core/request_attributes.hpp>
#include <boost/taar/core/response_builder.hpp>
#include <boost/taar/core/response_from.hpp>
#include <boost/taar/core/response_from_tag.hpp>
#include <boost/taar/core/segmented_body.hpp>
#include <boost/taar/handler/constrained.hpp>
#include <boost/taar/handler/context_inject.hpp>
#include <boost/taar/handler/guard.hpp>
#include <boost/taar/handler/htdocs.hpp>
#include <boost/taar/handler/path_arg_check.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/rest_arg_cast.hpp>
#include <boost/taar/handler/rest_arg_cast_tag.hpp>
#include <boost/taar/handler/rest_param.hpp>
#include <boost/taar/handler/session_key.hpp>
#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/jsonrpc/arg.hpp>
#include <boost/taar/jsonrpc/bind.hpp>
#include <boost/taar/jsonrpc/endpoint.hpp>
#include <boost/taar/jsonrpc/envelope.hpp>
#include <boost/taar/jsonrpc/error.hpp>
#include <boost/taar/jsonrpc/request_scope.hpp>
#include <boost/taar/jsonrpc/response.hpp>
#include <boost/taar/matcher/context.hpp>
#include <boost/taar/matcher/cookie.hpp>
#include <boost/taar/matcher/header.hpp>
#include <boost/taar/matcher/metadata.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/operand.hpp>
#include <boost/taar/matcher/route_shape.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/matcher/template_parser.hpp>
#include <boost/taar/matcher/version.hpp>
#include <boost/taar/mcp/arg.hpp>
#include <boost/taar/mcp/content.hpp>
#include <boost/taar/mcp/endpoint.hpp>
#include <boost/taar/mcp/error.hpp>
#include <boost/taar/openapi/generator.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/server/tls.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/taar/session/https.hpp>
#include <boost/taar/type_traits/always_false.hpp>
#include <boost/taar/type_traits/callable.hpp>
#include <boost/taar/type_traits/container_like.hpp>
#include <boost/taar/type_traits/has_call_operator.hpp>
#include <boost/taar/type_traits/numeric_type.hpp>
#include <boost/taar/type_traits/specialization_of.hpp>
#include <boost/taar/type_traits/string_like.hpp>
#include <boost/taar/type_traits/super_type.hpp>

namespace {

BOOST_AUTO_TEST_CASE(test_public_headers_compile_with_windows_min_max_macros)
{
    BOOST_TEST(min(1, 2) == 1);
    BOOST_TEST(max(1, 2) == 2);
}

} // namespace
