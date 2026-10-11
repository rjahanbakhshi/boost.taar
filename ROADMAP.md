# Roadmap and known limitations

What boost.taar does not do yet, grouped by how it affects a server that
exposes one set of callables over REST, JSON-RPC and MCP. Items are
tackled one at a time; the order below is a suggestion, not a promise.

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

## Transport

- HTTP/2 and response compression are out of scope for now.

## MCP

- A POST never answers with an SSE stream; notifications go over the GET
  stream (or stdout on stdio).
- Resource templates: RFC 6570 values are matched raw (no percent
  decoding); list values are joined with commas.
- The stdio transport is POSIX-only (`posix::stream_descriptor`).

## OpenAPI

- Only `boost::describe`d types are hoisted into `components.schemas`;
  parameters and responses are not shared as components.

## REST

- A streaming handler under `with_session` runs its body without the
  entry's lock; state it shares with other requests needs its own
  synchronisation.

## Recently closed

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
