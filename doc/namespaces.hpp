//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

// Documentation only: read by Doxygen for the reference, never included.
//
// Doxygen resolves an @ref to a namespace member (a function, an enum and
// its values, a type alias or a variable) only when the namespace itself
// is documented; without these comments most cross-references in the
// reference were dead links ("unable to resolve reference").

/// Boost.Taar: HTTP servers on Boost.Asio, Boost.Beast, Boost.URL and Boost.JSON.
namespace boost::taar {}

/// Request handlers: REST adapters and their argument providers, static files, guards and keyed sessions.
namespace boost::taar::handler {}

/// JSON-RPC 2.0 endpoints, their argument providers and error mapping.
namespace boost::taar::jsonrpc {}

/// Composable request matchers on method, target, headers, cookies and version.
namespace boost::taar::matcher {}

/// Model Context Protocol endpoints: tools, resources, prompts and their transports.
namespace boost::taar::mcp {}

/// OpenAPI 3.1 document generation from registered routes.
namespace boost::taar::openapi {}

/// TCP and TLS acceptors that run a session per connection.
namespace boost::taar::server {}

/// HTTP and HTTPS sessions: routing, keep-alive, timeouts and error handling per connection.
namespace boost::taar::session {}

/// Compile-time introspection used by the handler layer.
namespace boost::taar::type_traits {}
