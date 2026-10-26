# Roadmap and known limitations

What boost.taar does not do, by choice or not yet. "Out of scope" lists
deliberate omissions with the recommended alternative; "Known gaps" are
worth closing, grouped by area, each with the likely approach. Items are
tackled one at a time; the order is a suggestion, not a promise.

## Out of scope

Deliberate, not planned.

- HTTP/2 and HTTP/3: Beast is HTTP/1.1 only. Terminate h2/h3 in a reverse
  proxy in front of the server.
- On-the-fly response compression (gzip, br, zstd): needs a compression
  dependency. Serve precompressed files (`htdocs` picks `file.br` /
  `file.gz` by `Accept-Encoding`) or compress in a proxy.
- Directory listings (autoindex): an information disclosure risk. Build
  one as an explicit route if needed.
- Bandwidth throttling and rate limiting: belong in a proxy, or in an
  interceptor with a user-supplied limiter.
- `.htaccess`, rewrite engines, server-side includes, WebDAV: use
  matchers, several routes, interceptors and guards instead.
- Access control as a built-in feature: use interceptors and guards.
- Custom error pages as a built-in feature: use the soft error handler
  and the response filter.

## Known gaps

### Transport and connection handling

- A client that disconnects from a streamed response is noticed only at
  the next write; a stream suspended in a long wait keeps waiting.
  Approach: a concurrent read watcher during streaming that keeps
  pipelined bytes, or a documented keepalive pattern (MCP streams
  already send keepalive comments).
- Responses carry no `Date` header, which RFC 9110 6.6.1 asks an origin
  server with a clock to send. Approach: let the session add it
  (`format_http_date` exists) unless the handler or filter set one.
- Generators (`chunked_response`, `async_generator`) co_spawn the awaited
  operation on every `co_await` inside the body: one frame and spawn per
  awaited operation. Approach: evaluate Asio `experimental::coro` or a
  direct awaiter bridge when it stabilises.
- Endpoint and session registration (`register_request_handler`,
  `add_interceptor`, `tool`, `method`, ...) is not synchronised against
  serving: configure before `run()` (documented; no runtime check).
- `context_store` has no eviction hook: an MCP session that expires idle
  is dropped without calling the endpoint's `end()` (harmless today
  because a locked entry is never swept, but a hook would make cleanup
  explicit).

### Static files

- File I/O is blocking and runs on the connection's strand thread; no
  `sendfile`. HEAD, 304 and the caches limit it, but a slow disk stalls
  the serving thread. Approach: Asio `stream_file` /
  `random_access_file` (io_uring on Linux) behind a feature check.
- Within `revalidate_after` a file rewritten with the same size is still
  described by its previous validators (and, when it sits in the memory
  cache, served with its previous bytes). Approach: file system change
  notification (inotify, ReadDirectoryChangesW) to invalidate entries.
- On Windows, `:` in a segment is refused but reserved device names
  (`CON`, `NUL`, ...), trailing dots and short (8.3) names are not
  filtered. Approach: reject them in the segment check on Windows.
- Without `path_arg`, a route with several placeholders falls back to the
  whole request path (only the `{*}` entry or a single argument is
  picked). Approach: record the greedy placeholder's name in the context
  when the target matcher captures it.
- Range requests are always served from the uncompressed file, so a
  client resuming a compressed download restarts it uncompressed.
  Approach: apply ranges to the chosen variant once clients that need it
  appear.

### MCP

- A POST never answers with an SSE stream; server notifications and
  requests travel only over the GET stream (or stdout on stdio).
- JSON-RPC `request_scope::report_progress` does nothing over plain HTTP
  (no stream to carry it); only MCP and stdio deliver progress.
- Resource template values are matched raw (no percent decoding); list
  values are joined with commas.
- The stdio transport is POSIX-only (`posix::stream_descriptor`); Windows
  needs a `stream_handle` based variant.

### Interceptors and guards

- The response filter cannot see request attributes, so a value minted
  by an interceptor (such as a request id) cannot be echoed by the filter
  without being stored elsewhere. Approach: pass the request's
  `matcher::context` (or its attributes) to the response filter.
- `guard` runs its check on a copy of the request context (one copy of
  `path_args` per guarded request); changes to `path_args` by the check
  are discarded.

### OpenAPI

- Only `boost::describe`d types are hoisted into `components.schemas`;
  parameters and responses are not shared as components.
- Routes protected by interceptors are not documented as secured unless
  the operation declares `security` explicitly.

### REST

- A streaming handler under `with_session` runs its body without the
  entry's lock; state shared with other requests needs its own
  synchronisation (documented).

### Errors

- `server::tcp` still calls the throwing `acceptor.local_endpoint()`;
  practically never fails, but it would surface as an Asio exception
  rather than `taar::exception`.

### Documentation and tooling

- Pre-existing doxygen "unable to resolve reference" warnings and
  duplicate DocBook ids in the generated reference (validity errors)
  remain; the docs build succeeds but the reference has dead links.
- GCC 16 reports -Wdangling-pointer false positives in test coroutine
  lambdas; tests are unaffected but the b2 log is noisy.

## Planned directions

- C++26 static reflection (P2996, with annotations from P3394) as the
  replacement for most of the hand-written introspection, behind a
  feature check so C++23 builds keep the current machinery:
  - callable signature detection (`type_traits/callable.hpp`,
    `has_call_operator`) and the concepts built on it (`string_like`,
    `container_like`, `numeric_type`, `specialization_of`), which today
    also rule out generic lambdas as handlers;
  - context injection and argument binding by parameter type and
    position;
  - parameter names taken from the callable itself, so `rest(&add)`,
    `rpc.method("add", &add)` and `mcp.tool("add", ..., &add)` need no
    providers that repeat `"a"` and `"b"`;
  - JSON Schema, JSON conversion and OpenAPI/MCP schemas for plain
    aggregates and enums without `BOOST_DESCRIBE_STRUCT` /
    `BOOST_DESCRIBE_ENUM`;
  - descriptions, defaults and constraints as annotations on parameters
    and members (for example a description or `minimum` attached to a
    field), replacing `description {...}`, `with_default` and
    `constrained` in the common cases.

## Inherent to the surfaces

These follow from what each protocol can carry. They are documented
rather than planned.

- Streaming results exist only on REST (`chunked_response`,
  `async_generator`), as do streaming request bodies (`body_reader`). A
  JSON-RPC method or MCP tool receives and returns one JSON value.
- A callable returning `http::response`, `response_builder` or raw bytes is
  REST-only; one whose return type has only a `response_from` overload
  cannot be an RPC method or MCP tool, and one with only `value_from`
  support cannot be a REST route unless it is a described type or JSON
  container (those now work on all three).
- REST sessions (cookie or header keyed) and MCP sessions (`Mcp-Session-Id`)
  are separate stores. One logical user session spanning both requires the
  application to map keys itself.

## Recently closed

- Connection timeouts on the session, modelled on nginx: idle (silent
  close), header and body (`408 Request Timeout`, then close) and send
  (close), with setters on `basic_http` and defaults of 60, 30, 60 and
  60 seconds. Handler time and the gaps of a streaming generator do not
  count; the body timeout also bounds shielded body reads during
  shutdown.
- Static files: `handler::htdocs` rework with `htdocs_options` (safe
  resolution of the route's greedy capture, MIME table, validators and
  RFC 9110 conditional requests, `Cache-Control` rules, single and
  multipart ranges with `If-Range`, precompressed variants with `Vary`,
  metadata and memory caches, `on_response`), and core helpers for
  HTTP-dates, Range, entity tags, Accept-Encoding and `segmented_body`.
- Cancellation coverage across shutdown, client disconnect, JSON-RPC
  cancel and MCP `notifications/cancelled`, with the `cancellation_server`
  example; aborted `a && b` waits no longer surface as hard errors.
- Interceptors and route guards: `basic_http::add_interceptor` (before
  routing and before the body is read), `handler::guard`, request
  attributes on `matcher::context` and `attribute_arg<T>()` on every
  surface.
- Executor surface: `server::tcp`/`server::tls` take an explicit
  connection executor (a server spawned on a strand no longer serialises
  its connections), handler stream types are plain Beast types
  (`beast::tcp_stream`, `ssl_stream<tcp_stream>`, `tcp::socket`), and the
  design chapter documents the executor model.
- Unified error mechanism: `taar::failure`, `taar::result<T>` and
  `taar::exception`; every error hook takes `failure const&`, defaults map
  by code (`http_status_for`, JSON-RPC mapper, MCP tool error handler),
  and the `mcp::error` category replaces the `client_error` class.
- RFC 6570 operators in MCP resource templates (`{+path}`, `{#frag}`,
  `{.ext}`, `{/seg}`, `{;p}`, `{?q}`, `{&q}`, comma lists, `*` and `:n`).
- MCP `logging/setLevel` and `log()`, `completion/complete`, tool
  `outputSchema`, `title` and `annotations` via `tool_spec`.
- Multi-threaded shutdown race: connections run on per-connection strands
  and `cancellation_signals::emit` dispatches through them.
- `query_list_arg` for repeated query keys (`?tag=a&tag=b`), with the
  matching OpenAPI `explode` marking.
- `with_session` over streaming handlers (lock held until the stream is
  produced; minted key on the stream's headers).
- `handler::constrained` (declarative validation on all surfaces), OpenAPI
  defaults, security schemes and `components.schemas`.
- MCP server-to-client requests (`request`, `create_message`, `elicit`,
  `list_roots`), client capabilities on `session_info`, and
  `on_client_notification`.
- MCP binary content (image, audio, embedded resources, blob resources)
  and SSE resumption with `Last-Event-ID`.
- MCP prompts, resource subscriptions, list-changed notifications and list
  pagination.
- Request cancellation (`request_scope`, `endpoint::cancel`, MCP
  `notifications/cancelled`) and MCP progress notifications.
- JSON-RPC positional parameters.
- `405 Method Not Allowed` with `Allow`, and `HEAD` served from `GET`.
- `multipart/form-data`, buffered (`multipart_form_arg`) and streamed
  (`multipart_stream_arg`).
- Request body streaming through `body_reader` / `stream_body_arg`.
- TLS: `session::https` over `beast::ssl_stream`, accepted by `server::tls`.
- REST request providers (`header_arg`, `cookie_arg`, `query_arg`) usable
  on JSON-RPC methods and MCP tools.
- Full JSON Schema for `boost::describe`d structs and enums, sequences and
  maps, shared by OpenAPI and MCP `inputSchema`; described types as REST
  return values.
- `mcp::session_info` context so a tool can notify its own client.
- CORS policy with preflight handler and session response filter.
- MCP stdio transport.
