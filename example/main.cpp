#include "critter/webserver.h"

int main(int, const char**)
{
    critter::WebServer server;
    server.listen(8888);
    server.listen(critter::SslOptions({"./cert.pem", "./key.pem"}), 8889);
    server.add_http_handler(http::verb::get, "/test/?", [](auto&& req)
    {
        return "Hello\n";
    });
    server.add_http_handler(http::verb::post, "/test/?", [](auto&& req)
    {
        std::cout << req.body() << std::endl;
        return "ok\n";
    });
    server.add_ws_handler("/ws(/.*)?", [&](auto msg, auto& session) {
        std::cout << msg << std::endl;
        // echo the message to all clients
        for(auto& session: server.get_ws_sessions()) session->send(msg);
    });
    server.serve_files("/", "./www/");

    std::cout << "Server started" << std::endl; 
    server.run();
    return 0;
}
