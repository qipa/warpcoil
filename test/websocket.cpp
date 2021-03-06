#include "generated.hpp"
#include "impl_test_interface.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/test/unit_test.hpp>
#include <silicium/error_or.hpp>
#include <warpcoil/beast.hpp>
#include <warpcoil/beast_push_warnings.hpp>
#include <beast/websocket/stream.hpp>
#include <warpcoil/pop_warnings.hpp>
#include "checkpoint.hpp"

BOOST_AUTO_TEST_CASE(websocket_error)
{
    // make sure that these strings are stable
    {
        boost::system::error_code const ec =
            warpcoil::beast::make_error_code(warpcoil::beast::websocket_error::unexpected_opcode);
        BOOST_CHECK_EQUAL("websocket_error", std::string(ec.category().name()));
        BOOST_CHECK_EQUAL("unexpected_opcode", ec.message());
    }
    {
        boost::system::error_code const ec =
            warpcoil::beast::make_error_code(warpcoil::beast::websocket_error::close);
        BOOST_CHECK_EQUAL("websocket_error", std::string(ec.category().name()));
        BOOST_CHECK_EQUAL("close", ec.message());
    }
}

#ifndef _MSC_VER
// serve_one_request in the test below takes minutes to compile with Visual C++
// 2015 Update 3 for an unknown reason.
BOOST_AUTO_TEST_CASE(websocket)
{
    using namespace boost::asio;
    io_service io;

    warpcoil::checkpoint tcp_accepted;
    warpcoil::checkpoint websocket_accepted;
    warpcoil::checkpoint served;
    warpcoil::checkpoint tcp_connected;
    warpcoil::checkpoint websocket_connected;
    warpcoil::checkpoint received_response;

    ip::tcp::acceptor acceptor(io, ip::tcp::endpoint(ip::tcp::v6(), 0), true);
    acceptor.listen();
    ip::tcp::socket accepted_socket(io);
    warpcoil::impl_test_interface server_impl;
    acceptor.async_accept(
        accepted_socket, [&](boost::system::error_code ec)
        {
            tcp_accepted.enter();
            websocket_connected.enable();
            Si::throw_if_error(ec);
            typedef beast::websocket::stream<ip::tcp::socket &> websocket;
            auto session = std::make_shared<warpcoil::beast::async_stream_adaptor<websocket>>(
                websocket(accepted_socket), beast::streambuf());
            session->next_layer().async_accept(
                [session, &server_impl, &websocket_accepted, &served,
                 &received_response](boost::system::error_code ec)
                {
                    websocket_accepted.enter();
                    received_response.enable();
                    Si::throw_if_error(ec);
                    auto splitter =
                        std::make_shared<warpcoil::cpp::message_splitter<decltype(*session)>>(
                            *session);
                    auto writer =
                        std::make_shared<warpcoil::cpp::buffered_writer<decltype(*session)>>(
                            *session);
                    auto server = std::make_shared<async_test_interface_server<
                        decltype(server_impl), decltype(*session), decltype(*session)>>(
                        server_impl, *splitter, *writer);
                    server->serve_one_request(
                        [server, session, splitter, writer, &served](boost::system::error_code ec)
                        {
                            served.enter();
                            Si::throw_if_error(ec);
                        });
                });
        });

    ip::tcp::socket connecting_socket(io);
    connecting_socket.async_connect(
        ip::tcp::endpoint(ip::address_v4::loopback(), acceptor.local_endpoint().port()),
        [&](boost::system::error_code ec)
        {
            tcp_connected.enter();
            websocket_accepted.enable();
            Si::throw_if_error(ec);
            typedef beast::websocket::stream<ip::tcp::socket &> websocket;
            auto session = std::make_shared<warpcoil::beast::async_stream_adaptor<websocket>>(
                websocket(connecting_socket), beast::streambuf());
            session->next_layer().async_handshake(
                "localhost", "/", [session, &websocket_connected, &received_response,
                                   &served](boost::system::error_code ec)
                {
                    websocket_connected.enter();
                    served.enable();
                    Si::throw_if_error(ec);
                    auto splitter =
                        std::make_shared<warpcoil::cpp::message_splitter<decltype(*session)>>(
                            *session);
                    auto writer =
                        std::make_shared<warpcoil::cpp::buffered_writer<decltype(*session)>>(
                            *session);
                    auto client = std::make_shared<
                        async_test_interface_client<decltype(*session), decltype(*session)>>(
                        *writer, *splitter);
                    client->utf8("Alice", [client, writer, splitter, session,
                                           &received_response](Si::error_or<std::string> result)
                                 {
                                     received_response.enter();
                                     BOOST_CHECK_EQUAL("Alice123", result.get());
                                 });
                });
        });

    tcp_accepted.enable();
    tcp_connected.enable();
    io.run();
}
#endif
