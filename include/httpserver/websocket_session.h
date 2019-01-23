#include <boost/beast/websocket.hpp>
#include <boost/asio/spawn.hpp>
#include <memory>

namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession>
{
    websocket::stream<tcp::socket> ws_;
public:
    // Take ownership of the socket
    explicit
    WebSocketSession(tcp::socket socket)
        : ws_(std::move(socket))
    {
    }

    template<class Body, class Allocator>
    void
    run(http::request<Body, http::basic_fields<Allocator>> req,
        boost::asio::yield_context yield)
    {
        // Accept the websocket handshake
        boost::system::error_code ec;
        ws_.async_accept(req, yield[ec]);
        if(ec) fail(ec, "accept");

        boost::asio::spawn(
            ws_.get_io_service(),
            std::bind(
                &WebSocketSession::read, shared_from_this(),
                std::placeholders::_1)); 
    }

    void
    read(boost::asio::yield_context yield)
    {
        for(;;)
        {
            boost::system::error_code ec;
            boost::beast::multi_buffer buffer;

            // Read a message into our buffer
            ws_.async_read(buffer, yield[ec]);
            if(ec)
                return fail(ec, "read");
            
            // Echo the message
            ws_.text(ws_.got_text());
            ws_.async_write(buffer.data(), yield[ec]);
            if(ec)
                return fail(ec, "write");
        }
    }

    void fail(boost::system::error_code ec, char const* what)
    {
        std::cerr << what << ": " << ec.message() << "\n";
    }
};