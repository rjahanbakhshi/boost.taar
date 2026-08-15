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

- Capabilities not implemented: `completion/complete`, `logging/setLevel`.
- `outputSchema` and tool annotations are not emitted.
- A POST never answers with an SSE stream; notifications go over the GET
  stream (or stdout on stdio).
- Resource templates support `{name}` placeholders only (no RFC 6570
  operators).
- The stdio transport is POSIX-only (`posix::stream_descriptor`).

## OpenAPI

- Default values from `with_default` are not emitted.
- No security schemes, validation constraints (ranges, patterns), examples,
  or `components` reuse; every schema is inlined.
- Query parameters only bind scalars and CSV lists; repeated query keys
  (`?tag=a&tag=b`) are rejected as ambiguous.

## REST

- `with_session` cannot wrap a streaming handler, so a per-user SSE stream
  must read the session cookie by hand.
- No declarative validation on arguments.

## Recently closed

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
