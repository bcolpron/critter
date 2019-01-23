#include "httpserver/httpserver.h"

int main(int, const char**)
{
    HttpServer server(8888);
    server.serve_files("/static", "./static/");
    server.add_http_handler(http::verb::get, "/test/?", [](http::request<http::string_body>&& req) -> http::response<http::string_body>
    {
        return make_response(req, "Hello\n");
    });
    server.add_ws_handler("/ws");

    std::cout << "Server started" << std::endl; 
    server.run();
    return 0;
}
