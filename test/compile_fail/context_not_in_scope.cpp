// Expected failure: "requests a context type that is not in scope for this handler"
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>

namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;

struct conn_ctx { int n = 0; };

void probe()
{
    taar::session::http session;   // no connection context
    session.register_request_handler(
        method == http::verb::get && target == "/",
        taar::handler::rest([](conn_ctx& c) { return c.n; }));
}
