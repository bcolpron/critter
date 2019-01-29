#include <boost/beast/websocket.hpp>
#include <boost/asio/spawn.hpp>
#include <memory>

namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;

class WebSocketSession;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession>
{
public:
    template<class F>
    WebSocketSession(tcp::socket socket, F f)
        : ws_(std::move(socket)), on_message_(std::move(f))
    {
    }

    void send(std::string_view msg)
    {
        boost::beast::multi_buffer buffer;
        boost::beast::ostream(buffer) << msg;
        ws_.text(true);
        ws_.write(buffer.data());
    }

private:

    using MessageHandler = std::function<void(std::string_view, WebSocketSession&)>;
    friend class HttpServer;

    websocket::stream<tcp::socket> ws_;
    MessageHandler on_message_;

    void fail(boost::system::error_code ec, char const* what)
    {
        std::cerr << what << ": " << ec.message() << "\n";
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

            on_message_(boost::beast::buffers_to_string(buffer.data()), *this);
        }
    }
};