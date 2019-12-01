#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <stdexcept>
#include <vector>
#include <tuple>
#include <regex>
#include <variant>

namespace http = boost::beast::http;

namespace critter::detail
{

inline auto make_response(http::response<http::string_body>&& response) { return std::move(response); }

inline auto make_response(std::string response)
{
    http::response<http::string_body> res;
    res.body() = std::move(response);
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/plain");
    res.prepare_payload();
    return res;
}

inline auto make_response(const char* response) { return make_response(std::string(response)); }
inline auto make_response(std::string_view response) { return make_response(std::string(response)); }

class WebSocketSession;
using HttpHandler = std::function<http::response<http::string_body>(http::request<http::string_body>&&)>;
using WebSocketHandler = std::function<void(std::string_view, WebSocketSession&)>;

class Registry
{
    using Handler = std::variant<HttpHandler, WebSocketHandler>;
    using Entry = std::tuple<http::verb, std::regex, Handler>;
public:

    struct NotFound: std::out_of_range {
        NotFound(): std::out_of_range("not found") {}
    };

    void add(http::verb v, boost::beast::string_view uri, WebSocketHandler h)
    {
        resource_table.emplace_back(std::move(v), std::regex(uri.begin(), uri.end()), Handler(h));
    }

    void add(http::verb v, boost::beast::string_view uri, HttpHandler f)
    {
        resource_table.emplace_back(std::move(v), std::regex(uri.begin(), uri.end()), Handler(f));
    }

    const Handler& get(http::verb verb, boost::beast::string_view uri) const
    {
        auto pred = [&](const auto& entry){
            std::cmatch match;
            return verb == std::get<0>(entry) 
                && std::regex_match(uri.begin(), uri.end(), match, std::get<1>(entry));
        };
        auto found = 
            std::find_if(begin(resource_table), end(resource_table), pred);
        if (found != resource_table.end())
        {
            return std::get<2>(*found);
        }
        throw NotFound();
    }

private:
    std::vector<Entry> resource_table;
};

}
