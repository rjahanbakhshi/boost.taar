// Expected failure: "openapi::generator cannot describe this route"
#include <boost/taar/openapi/generator.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>

namespace http = boost::beast::http;
namespace taar = boost::taar;
using taar::matcher::method;
using taar::matcher::target;

void probe()
{
    taar::session::http session;
    taar::openapi::generator gen {{.title = "t", .version = "1"}};
    session.register_request_handler(
        method != http::verb::delete_ && target == "/a",
        taar::handler::rest([] { return 1; }),
        gen.describe("anything but delete"));
}
