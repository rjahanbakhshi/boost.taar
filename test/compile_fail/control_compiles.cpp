// Must compile: validates that the probe harness builds a well-formed TU.
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>

namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;

struct conn_ctx { int n = 0; };

void probe()
{
    taar::session::basic_http<conn_ctx> session;
    session.register_request_handler(
        method == http::verb::get && target == "/users/{id}",
        taar::handler::rest([](conn_ctx& c, int id) { return c.n + id; }, taar::handler::path_arg("id")));
}
