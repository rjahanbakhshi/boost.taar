// Expected failure: "The number of REST argument providers must equal the number of callable parameters"
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>

void probe()
{
    auto h = boost::taar::handler::rest([](int, int) { return 0; }, boost::taar::handler::path_arg("a"));
    (void)h;
}
