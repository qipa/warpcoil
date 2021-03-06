#include "test_streams.hpp"
#include "checkpoint.hpp"
#include "generated.hpp"
#include "boost_print_log_value.hpp"
#include "failing_test_interface.hpp"
#include <silicium/exchange.hpp>
#include <silicium/error_or.hpp>

namespace
{
    void test_invalid_server_request(std::vector<std::uint8_t> expected_request)
    {
        warpcoil::failing_test_interface server_impl;
        warpcoil::async_read_dummy_stream server_requests;
        warpcoil::async_write_dummy_stream server_responses;
        warpcoil::cpp::message_splitter<decltype(server_requests)> splitter(server_requests);
        warpcoil::cpp::buffered_writer<decltype(server_responses)> writer(server_responses);
        async_test_interface_server<decltype(server_impl), warpcoil::async_read_dummy_stream,
                                    warpcoil::async_write_dummy_stream> server(server_impl,
                                                                               splitter, writer);
        BOOST_REQUIRE(!server_requests.respond);
        BOOST_REQUIRE(!server_responses.handle_result);
        warpcoil::checkpoint request_served;
        server.serve_one_request([&request_served](boost::system::error_code ec)
                                 {
                                     request_served.enter();
                                     BOOST_CHECK_EQUAL(warpcoil::cpp::make_invalid_input_error(),
                                                       ec);
                                 });
        BOOST_REQUIRE(server_requests.respond);
        BOOST_REQUIRE(!server_responses.handle_result);
        BOOST_REQUIRE(server_responses.written.empty());
        request_served.enable();
        Si::exchange(server_requests.respond, nullptr)(Si::make_memory_range(expected_request));
        request_served.require_crossed();
        BOOST_REQUIRE(!server_requests.respond);
        BOOST_REQUIRE(!server_responses.handle_result);
    }
}

BOOST_AUTO_TEST_CASE(async_server_invalid_utf8_request)
{
    test_invalid_server_request(
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 'u', 't', 'f', '8', 5, 'N', 'a', 'm', 'e', 0xff});
}

BOOST_AUTO_TEST_CASE(async_server_invalid_variant_request_a)
{
    test_invalid_server_request(
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 'v', 'a', 'r', 'i', 'a', 'n', 't', 2, 0, 0, 0, 0});
}

BOOST_AUTO_TEST_CASE(async_server_invalid_variant_request_b)
{
    test_invalid_server_request(
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 'v', 'a', 'r', 'i', 'a', 'n', 't', 255, 0, 0, 0, 0});
}

BOOST_AUTO_TEST_CASE(async_server_invalid_message_type)
{
    test_invalid_server_request(
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 4, 'u', 't', 'f', '8', 4, 'N', 'a', 'm', 'e'});
}

namespace
{
    template <class Result>
    void test_invalid_client_request(
        std::function<void(async_test_interface &, std::function<void(Si::error_or<Result>)>)>
            begin_request)
    {
        warpcoil::async_write_dummy_stream client_requests;
        warpcoil::async_read_dummy_stream client_responses;
        warpcoil::cpp::message_splitter<decltype(client_responses)> splitter(client_responses);
        warpcoil::cpp::buffered_writer<decltype(client_requests)> writer(client_requests);
        async_test_interface_client<warpcoil::async_write_dummy_stream,
                                    warpcoil::async_read_dummy_stream> client(writer, splitter);
        BOOST_REQUIRE(!client_responses.respond);
        BOOST_REQUIRE(!client_requests.handle_result);
        async_type_erased_test_interface<decltype(client)> type_erased_client{client};
        warpcoil::checkpoint request_rejected;
        request_rejected.enable();
        begin_request(type_erased_client, [&request_rejected](Si::error_or<Result> result)
                      {
                          BOOST_REQUIRE_EQUAL(warpcoil::cpp::make_invalid_input_error(),
                                              result.error());
                          request_rejected.enter();
                      });
        request_rejected.require_crossed();
        BOOST_REQUIRE(!client_responses.respond);
        BOOST_REQUIRE(!client_requests.handle_result);
    }
}

BOOST_AUTO_TEST_CASE(async_client_invalid_utf8_request)
{
    test_invalid_client_request<std::string>(
        [](async_test_interface &client, std::function<void(Si::error_or<std::string>)> on_result)
        {
            client.utf8("Name\xff", on_result);
        });
}

BOOST_AUTO_TEST_CASE(async_client_invalid_utf8_length_request)
{
    test_invalid_client_request<std::string>(
        [](async_test_interface &client, std::function<void(Si::error_or<std::string>)> on_result)
        {
            client.utf8(std::string(256, 'a'), on_result);
        });
}

BOOST_AUTO_TEST_CASE(async_client_invalid_vector_length_request)
{
    test_invalid_client_request<std::vector<std::uint64_t>>(
        [](async_test_interface &client,
           std::function<void(Si::error_or<std::vector<std::uint64_t>>)> on_result)
        {
            client.vectors_256(std::vector<std::uint64_t>(256), on_result);
        });
}

BOOST_AUTO_TEST_CASE(async_client_invalid_int_request_too_small)
{
    test_invalid_client_request<std::uint16_t>(
        [](async_test_interface &client, std::function<void(Si::error_or<std::uint16_t>)> on_result)
        {
            client.atypical_int(0, on_result);
        });
}

BOOST_AUTO_TEST_CASE(async_client_invalid_int_request_too_large)
{
    test_invalid_client_request<std::uint16_t>(
        [](async_test_interface &client, std::function<void(Si::error_or<std::uint16_t>)> on_result)
        {
            client.atypical_int(1001, on_result);
        });
}

BOOST_AUTO_TEST_CASE(async_client_invalid_variant_element_request)
{
    test_invalid_client_request<Si::variant<std::uint16_t, std::string>>(
        [](async_test_interface &client,
           std::function<void(Si::error_or<Si::variant<std::uint16_t, std::string>>)> on_result)
        {
            client.variant(std::string("Name\xff"), on_result);
        });
}
