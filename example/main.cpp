#include "httpserver/httpserver.h"

int main(int, const char**)
{
    HttpServer server(8888);
    server.serve_files("/static/", "./www/");
    server.add_http_handler(http::verb::get, "/test/?", [](auto&& req)
    {
        return make_response(req, "Hello\n");
    });
    detail::WebSocketHandler a;
    server.add_ws_handler("/ws/?", [](auto msg, auto& session) {
        std::cout << msg << std::endl;
    });

    std::cout << "Server started" << std::endl; 
    server.run();
    return 0;
}
