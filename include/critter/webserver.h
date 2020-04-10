#pragma once

#define BOOST_COROUTINES_NO_DEPRECATION_WARNING

#include "detail/registry.h"
#include "detail/serve_files_handler.h"
#include "detail/websocket_session.h"
#include <boost/beast/websocket.hpp>
#if BOOST_VERSION < 107000
#   include <boost/beast/experimental/core/ssl_stream.hpp>
#else
#   include <boost/beast/ssl.hpp>
#endif
#include <boost/asio/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/system/system_error.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <stdexcept>

namespace critter
{

namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
namespace ssl = boost::asio::ssl;

struct HttpException: std::runtime_error
{
    HttpException(http::status code, const std::string& message)
    : std::runtime_error(message), code_(code) {}

    http::status code() const { return code_; }
private:
    http::status code_;
    std::string message;
};

using Request=http::request<http::string_body>;
using Response=http::response<http::string_body>;

struct SslOptions
{
    std::string certificate_file_path;
    std::string key_file_path;
};

class WebServer
{
    using tcp = boost::asio::ip::tcp;
public:
    using WebSocketSessions = std::vector<std::shared_ptr<detail::WebSocketSession>>;
    using verb = http::verb;

    WebServer() {}

    explicit WebServer(unsigned short port)
    {
        listen(port);
    }

    WebServer(const SslOptions& sslOptions, unsigned short port)
    {
        listen(sslOptions, port);
    }

    void listen(unsigned short port=80)
    {
        auto const address = boost::asio::ip::address::from_string("::");
        boost::asio::spawn(ioc,
            std::bind(
                &WebServer::do_listen<tcp::socket>, this,
                std::ref(ioc),
                tcp::endpoint{address, port},
                std::placeholders::_1));
    }

    void listen(const SslOptions& options, unsigned short port=443)
    {
        ctx.set_options(
            boost::asio::ssl::context::default_workarounds |
            boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::single_dh_use);
        ctx.use_certificate_file(options.certificate_file_path, boost::asio::ssl::context_base::pem);
        ctx.use_private_key_file(options.key_file_path, boost::asio::ssl::context_base::pem);

        auto const address = boost::asio::ip::address::from_string("::");
        boost::asio::spawn(ioc,
            std::bind(
                &WebServer::do_listen<boost::beast::ssl_stream<tcp::socket>>, this,
                std::ref(ioc),
                tcp::endpoint{address, port},
                std::placeholders::_1));
    }

    ~WebServer()
    {
        stop();
        std::for_each(begin(threads), end(threads), [](auto& t) {t.join();});
    }

    void serve_files(std::string base_uri, boost::beast::string_view local_path)
    {
        if(base_uri.back() == '/') base_uri.resize(base_uri.size() - 1);
        base_uri += "(/.*)";
        std::string path = local_path.to_string();
        registry_.add(http::verb::get, base_uri,
            [=](http::request<http::string_body>&& req) -> http::response<http::string_body> {
                return detail::serve_file_from(path, base_uri, std::move(req));
            });
    }

    template<class F>
    void add_http_handler(http::verb v, boost::beast::string_view uri_regex, F&& f)
    {
        registry_.add(v, uri_regex, [f=std::move(f)] (auto&& r) {return detail::make_response(f(std::move(r)));});
    }

    void add_ws_handler(boost::beast::string_view uri_regex, detail::WebSocketHandler f)
    {
        registry_.add(http::verb::get, uri_regex, std::move(f));
    }

    void start(unsigned nb_threads=1)
    {
        for(auto i = nb_threads; i > 0; --i)
        {
            threads.emplace_back([&]
            {
                ioc.run();
            });
        }
    }

    WebSocketSessions get_ws_sessions() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ws_sessions_;
    }

    void run()
    {
        ioc.run();
    }

    void stop()
    {
        ioc.stop();
    }

private:

    // Returns a not found response
    auto not_found(http::request<http::string_body>& req)
    {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + req.target().to_string() + "' was not found.";
        res.prepare_payload();
        return res;
    };

    // Returns an error response
    auto exception_response(http::request<http::string_body>& req, const HttpException& e)
    {
        http::response<http::string_body> res{e.code(), req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = e.what();
        res.prepare_payload();
        return res;
    };

    void fail(boost::system::error_code ec, char const* what)
    {
        std::cerr << what << ": " << ec.message() << std::endl;
    }

    template<class StreamClass>
    void do_session(
        StreamClass& stream,
        boost::asio::yield_context yield)
    {
        boost::system::error_code ec;
        boost::beast::flat_buffer buffer;

        if constexpr (std::is_same_v<StreamClass, boost::beast::ssl_stream<tcp::socket>>)
        {
            // Perform the SSL handshake
            stream.async_handshake(ssl::stream_base::server, yield[ec]);
            if(ec)
                return fail(ec, "handshake");
        }

        for(;;)
        {
            // Read a request
            http::request<http::string_body> req;
            http::async_read(stream, buffer, req, yield[ec]);
            if(ec == http::error::end_of_stream)
                break;
            if(ec)
                return fail(ec, "read");

            http::response<http::string_body> response;
            try
            {
                auto handler = registry_.get(req.method(), req.target());
                if(websocket::is_upgrade(req))
                {
                    auto session = std::make_shared<detail::WebSocketSessionImpl<StreamClass>>(std::move(stream),
                            std::get<detail::WebSocketHandler>(handler));
                    this->add(session);
                    session->on_close([this](auto session) {
                        this->remove(session);
                    });
                    session->run(std::move(req), yield);
                    return;
                }
                else
                {
                    try {
                        response = std::get<detail::HttpHandler>(handler)(std::move(req));
                    } catch (const HttpException&) {
                        throw;
                    } catch (const std::exception& e) {
                        throw HttpException(http::status::internal_server_error, e.what());
                    } catch (...) {
                        throw HttpException(http::status::internal_server_error, "unhandled exception");
                    }
                }
            } catch(const detail::Registry::NotFound&) {
                response = not_found(req);
            } catch(const HttpException& e) {
                response = exception_response(req, e);
            }

            // Send the response
            http::serializer<false, http::string_body> sr{response};
            http::async_write(stream, sr, yield[ec]);
            if(ec) return fail(ec, "write");
            if(!response.keep_alive())
            {
                // This means we should close the connection, usually because
                // the response indicated the "Connection: close" semantic.
                break;
            }
        }

        if constexpr (std::is_same_v<StreamClass, boost::beast::ssl_stream<tcp::socket>>)
        {
            stream.async_shutdown(yield[ec]);
            if(ec)
                return fail(ec, "shutdown");
        }
        else
        {
            stream.shutdown(tcp::socket::shutdown_send, ec);
        }
    }

    template<class StreamClass>
    void do_listen(
        boost::asio::io_context& ioc,
        tcp::endpoint endpoint,
        boost::asio::yield_context yield)
    {
        boost::system::error_code ec;

        // Open the acceptor
        tcp::acceptor acceptor(ioc);
        acceptor.open(endpoint.protocol(), ec);
        if(ec)
            throw boost::system::system_error(ec);

        acceptor.set_option(tcp::acceptor::reuse_address(true));

        // Bind to the server address
        acceptor.bind(endpoint, ec);
        if(ec)
            throw boost::system::system_error(ec);

        // Start listening for connections
        acceptor.listen(boost::asio::socket_base::max_connections, ec);
        if(ec)
            throw boost::system::system_error(ec);

        for(;;)
        {
            tcp::socket socket(ioc);
            acceptor.async_accept(socket, yield[ec]);
            if(ec)
                fail(ec, "accept");
            else {
                if constexpr (std::is_same_v<StreamClass, boost::beast::ssl_stream<tcp::socket>>) {
                    boost::asio::spawn(
                        acceptor.get_executor(),
                        std::bind(
                            &WebServer::do_session<boost::beast::ssl_stream<tcp::socket>>, this,
                            boost::beast::ssl_stream<tcp::socket>(std::move(socket), ctx),
                            std::placeholders::_1));
                }
                else
                {
                    boost::asio::spawn(
                        acceptor.get_executor(),
                        std::bind(
                            &WebServer::do_session<tcp::socket>, this,
                            tcp::socket(std::move(socket)),
                            std::placeholders::_1));
                }
            }
        }
    }

    void add(std::shared_ptr<detail::WebSocketSession> session)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ws_sessions_.push_back(std::move(session));
    }

    void remove(const std::shared_ptr<detail::WebSocketSession>& session)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ws_sessions_.erase(std::remove(ws_sessions_.begin(), ws_sessions_.end(), session),
                           ws_sessions_.end());
    }

    boost::asio::io_context ioc;
    ssl::context ctx{ssl::context::tlsv12};
    std::vector<std::thread> threads;
    detail::Registry registry_;
    mutable std::mutex mutex_;
    WebSocketSessions ws_sessions_;
};

}
