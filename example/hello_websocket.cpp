#include <warpcoil/beast.hpp>
#include "hello_interfaces.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <silicium/error_or.hpp>
#include <iostream>
#include <warpcoil/beast_push_warnings.hpp>
#include <beast/websocket/stream.hpp>
#include <warpcoil/pop_warnings.hpp>

namespace server
{
    struct my_hello_service : async_hello_as_a_service
    {
        void hello(std::string argument,
                   std::function<void(Si::error_or<std::string>)> on_result) override
        {
            on_result("Hello, " + argument + "!");
        }
    };
}

int main()
{
    using namespace boost::asio;
    io_service io;

    // server:
    ip::tcp::acceptor acceptor(io, ip::tcp::endpoint(ip::tcp::v6(), 0), true);
    acceptor.listen();
    ip::tcp::socket accepted_socket(io);
    server::my_hello_service server_impl;
    acceptor.async_accept(
        accepted_socket, [&accepted_socket, &server_impl](boost::system::error_code ec)
        {
            Si::throw_if_error(ec);
            typedef beast::websocket::stream<ip::tcp::socket &> websocket;
            auto session = std::make_shared<warpcoil::beast::async_stream_adaptor<websocket>>(
                websocket(accepted_socket), beast::streambuf());
            session->next_layer().async_accept(
                [session, &server_impl](boost::system::error_code ec)
                {
                    Si::throw_if_error(ec);
                    auto splitter =
                        std::make_shared<warpcoil::cpp::message_splitter<decltype(*session)>>(
                            *session);
                    auto writer =
                        std::make_shared<warpcoil::cpp::buffered_writer<decltype(*session)>>(
                            *session);
                    auto server = std::make_shared<async_hello_as_a_service_server<
                        decltype(server_impl), decltype(*session), decltype(*session)>>(
                        server_impl, *splitter, *writer);
                    server->serve_one_request(
                        [server, writer, session, splitter](boost::system::error_code ec)
                        {
                            Si::throw_if_error(ec);
                        });
                });
        });

    // client:
    ip::tcp::socket connecting_socket(io);
    connecting_socket.async_connect(
        ip::tcp::endpoint(ip::address_v4::loopback(), acceptor.local_endpoint().port()),
        [&io, &connecting_socket](boost::system::error_code ec)
        {
            Si::throw_if_error(ec);
            typedef beast::websocket::stream<ip::tcp::socket &> websocket;
            auto session = std::make_shared<warpcoil::beast::async_stream_adaptor<websocket>>(
                websocket(connecting_socket), beast::streambuf());
            session->next_layer().async_handshake(
                "localhost", "/", [session](boost::system::error_code ec)
                {
                    Si::throw_if_error(ec);
                    auto splitter =
                        std::make_shared<warpcoil::cpp::message_splitter<decltype(*session)>>(
                            *session);
                    auto writer =
                        std::make_shared<warpcoil::cpp::buffered_writer<decltype(*session)>>(
                            *session);
                    auto client = std::make_shared<
                        async_hello_as_a_service_client<decltype(*session), decltype(*session)>>(
                        *writer, *splitter);
                    client->hello("Alice", [client, writer, session,
                                            splitter](Si::error_or<std::string> result)
                                  {
                                      std::cout << result.get() << '\n';
                                  });
                });
        });

    io.run();
}
