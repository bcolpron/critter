#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/config.hpp>
#include <boost/system/system_error.hpp>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <regex>

namespace http = boost::beast::http;

namespace detail
{

class Registry
{
    using Handler = std::function<http::response<http::string_body>(http::request<http::string_body>)>;
    using Entry = std::tuple<http::verb, std::regex, Handler>;
public:

    struct NotFound: std::out_of_range {
        NotFound(): std::out_of_range("not found") {}
    };

    template<class F>
    void add(http::verb v, boost::beast::string_view uri, F&& f)
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

http::response<http::string_body> make_response(
    http::request<http::string_body> request,
    std::string message)
{
    http::response<http::string_body> res;
    res.body = std::move(message);
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/plain");
    res.prepare_payload();
    res.keep_alive(request.keep_alive());
    return res;
}

class HttpServer
{
    using tcp = boost::asio::ip::tcp;
public:
    using verb = http::verb;

    explicit HttpServer(unsigned short port=80)
    {
        auto const address = boost::asio::ip::address::from_string("0.0.0.0");

        // Spawn a listening port
        boost::asio::spawn(ios,
            std::bind(
                &HttpServer::do_listen, this,
                std::ref(ios),
                tcp::endpoint{address, port},
                std::placeholders::_1));
    }

    ~HttpServer()
    {
        stop();
        std::for_each(begin(threads), end(threads), [](auto& t) {t.join();});
    }

    template<class F>
    void add_handler(http::verb v, boost::beast::string_view uri_regex, F&& f)
    {
        registry.add(v, uri_regex, std::move(f));
    }

    void start(unsigned nb_threads=1)
    {
        for(auto i = nb_threads; i > 0; --i)
        {
            threads.emplace_back([&]
            {
                ios.run();
            });
        }
    }

    void run()
    {
        ios.run();
    }

    void stop()
    {
        ios.stop();
    }

private:

    http::response<http::string_body>
    handle_request(
        http::request<http::string_body>&& req)
    {
        // Returns a not found response
        auto const not_found =
        [&req](boost::beast::string_view target)
        {
            http::response<http::string_body> res{http::status::not_found, req.version};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body = "The resource '" + target.to_string() + "' was not found.";
            res.prepare_payload();
            return res;
        };

        // Returns a server error response
        auto const server_error =
        [&req](boost::beast::string_view what)
        {
            http::response<http::string_body> res{http::status::internal_server_error, req.version};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body = "An error occurred: '" + what.to_string() + "'";
            res.prepare_payload();
            return res;
        };

        try {
            return registry.get(req.method(), req.target())(req);
        } catch(const detail::Registry::NotFound&) {
            return not_found(req.target());
        } catch(const std::exception& e) {
            return server_error(e.what());
        }

    }

    void fail(boost::system::error_code ec, char const* what)
    {
        std::cerr << what << ": " << ec.message() << std::endl;
    }

    void do_session(
        tcp::socket& socket,
        boost::asio::yield_context yield)
    {
        boost::system::error_code ec;

        // This buffer is required to persist across reads
        boost::beast::flat_buffer buffer;

        // This lambda is used to send messages
        auto lambda = [&socket, &ec, yield](http::response<http::string_body>&& msg)
        {
            http::serializer<false, http::string_body> sr{msg};
            http::async_write(socket, sr, yield[ec]);
        };
        
        for(;;)
        {
            // Read a request
            http::request<http::string_body> req;
            http::async_read(socket, buffer, req, yield[ec]);
            if(ec == http::error::end_of_stream)
                break;
            if(ec)
                return fail(ec, "read");

            // Send the response
            auto response = handle_request(std::move(req));

            lambda(std::move(response));

            if(ec == http::error::end_of_stream)
            {
                // This means we should close the connection, usually because
                // the response indicated the "Connection: close" semantic.
                break;
            }
            if(ec)
                return fail(ec, "write");
        }
        socket.shutdown(tcp::socket::shutdown_send, ec);
    }

    void do_listen(
        boost::asio::io_service& ios,
        tcp::endpoint endpoint,
        boost::asio::yield_context yield)
    {
        boost::system::error_code ec;

        // Open the acceptor
        tcp::acceptor acceptor(ios);
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
            tcp::socket socket(ios);
            acceptor.async_accept(socket, yield[ec]);
            if(ec)
                fail(ec, "accept");
            else
                boost::asio::spawn(
                    acceptor.get_io_service(),
                    std::bind(
                        &HttpServer::do_session, this,
                        std::move(socket),
                        std::placeholders::_1));
        }
    }

    boost::asio::io_service ios;
    std::vector<std::thread> threads;
    detail::Registry registry;
};
