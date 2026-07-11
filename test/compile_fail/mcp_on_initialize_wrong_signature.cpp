// Expected failure: "on_initialize for a stateful endpoint takes (Session&, initialize_params const&)"
#include <boost/taar/mcp/endpoint.hpp>

struct calc_session { int last = 0; };

void probe()
{
    boost::taar::mcp::endpoint<calc_session> e {{.name = "s", .version = "1"}};
    e.on_initialize([](int) {});
}
