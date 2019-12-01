#include "critter/webserver.h"

int main(int, const char**)
{
    critter::WebServer server(8888);
    server.serve_files("/static/", "./www/");
    server.add_http_handler(http::verb::get, "/test/?", [](auto&& req)
    {
        return critter::make_response(req, "Hello\n");
    });
    server.add_http_handler(http::verb::post, "/test/?", [](auto&& req)
    {
        std::cout << req.body() << std::endl;
        return critter::make_response(req, "ok\n");
    });
    server.add_ws_handler("/ws(/.*)?", [&](auto msg, auto& session) {
        std::cout << msg << std::endl;
        // echo the message to all clients
        for(auto& session: server.get_ws_sessions()) session->send(msg);
    });

    std::cout << "Server started" << std::endl; 
    server.run();
    return 0;
}
