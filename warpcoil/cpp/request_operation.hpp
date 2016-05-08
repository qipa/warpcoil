#pragma once

#include <warpcoil/cpp/helpers.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <boost/asio/write.hpp>
#include <silicium/sink/iterator_sink.hpp>

namespace warpcoil
{
    namespace cpp
    {
        namespace detail
        {
            template <class ResultHandler, class Result>
            struct response_parse_operation
            {
                explicit response_parse_operation(ResultHandler handler)
                    : m_handler(std::move(handler))
                {
                }

                void operator()(boost::system::error_code ec, std::tuple<request_id, Result> response)
                {
                    // TODO: use request_id
                    m_handler(ec, std::get<1>(std::move(response)));
                }

            private:
                ResultHandler m_handler;

                template <class Function>
                friend void asio_handler_invoke(Function &&f, response_parse_operation *operation)
                {
                    using boost::asio::asio_handler_invoke;
                    asio_handler_invoke(f, &operation->m_handler);
                }
            };

            template <class ResultHandler, class AsyncReadStream, class Buffer, class Parser>
            struct request_send_operation
            {
                explicit request_send_operation(ResultHandler handler, AsyncReadStream &input, Buffer receive_buffer,
                                                std::size_t &receive_buffer_used)
                    : m_handler(std::move(handler))
                    , m_input(input)
                    , m_receive_buffer(receive_buffer)
                    , m_receive_buffer_used(receive_buffer_used)
                {
                }

                void operator()(boost::system::error_code ec, std::size_t)
                {
                    if (!!ec)
                    {
                        m_handler(ec, {});
                        return;
                    }
                    begin_parse_value(
                        m_input, m_receive_buffer, m_receive_buffer_used,
                        tuple_parser<integer_parser<request_id>, Parser>(),
                        response_parse_operation<ResultHandler, typename Parser::result_type>(std::move(m_handler)));
                }

            private:
                ResultHandler m_handler;
                AsyncReadStream &m_input;
                Buffer m_receive_buffer;
                std::size_t &m_receive_buffer_used;

                template <class Function>
                friend void asio_handler_invoke(Function &&f, request_send_operation *operation)
                {
                    using boost::asio::asio_handler_invoke;
                    asio_handler_invoke(f, &operation->m_handler);
                }
            };
        }

        enum class client_pipeline_request_status
        {
            ok,
            failure
        };

        template <class AsyncWriteStream, class AsyncReadStream>
        struct client_pipeline
        {
            explicit client_pipeline(AsyncWriteStream &requests, AsyncReadStream &responses)
                : requests(requests)
                , responses(responses)
                , response_buffer_used(0)
            {
            }

            template <class ResultParser, class RequestBuilder, class ResultHandler>
            void request(RequestBuilder build_request, ResultHandler &handler)
            {
                begin_request();
                {
                    auto sink = Si::Sink<std::uint8_t>::erase(Si::make_container_sink(request_buffer));
                    switch (build_request(sink))
                    {
                    case client_pipeline_request_status::ok:
                        break;

                    case client_pipeline_request_status::failure:
                        return;
                    }
                }
                auto receive_buffer = boost::asio::buffer(response_buffer);
                boost::asio::async_write(
                    requests, boost::asio::buffer(request_buffer),
                    warpcoil::cpp::detail::request_send_operation<typename std::decay<decltype(handler)>::type,
                                                                  AsyncReadStream, decltype(receive_buffer),
                                                                  ResultParser>(std::move(handler), responses,
                                                                                receive_buffer, response_buffer_used));
            }

        private:
            std::vector<std::uint8_t> request_buffer;
            AsyncWriteStream &requests;
            std::array<std::uint8_t, 512> response_buffer;
            AsyncReadStream &responses;
            std::size_t response_buffer_used;
            request_id next_request_id = 0;

            void begin_request()
            {
                request_buffer.clear();
                auto sink = Si::Sink<std::uint8_t>::erase(Si::make_container_sink(request_buffer));
                write_integer(sink, next_request_id);
                ++next_request_id;
            }
        };
    }
}
