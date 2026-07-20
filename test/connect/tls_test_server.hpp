// XCockpit
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef TEST_CONNECT_TLS_TEST_SERVER_HPP
#define TEST_CONNECT_TLS_TEST_SERVER_HPP

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>

namespace scratcher::test {

// Throwaway self-signed localhost certificate; the client context runs with
// verify_none, so no trust setup is needed on either side.
inline constexpr char tls_test_cert[] = R"(-----BEGIN CERTIFICATE-----
MIIBmzCCAUGgAwIBAgIUa9Y+/rS3R61Fta6xAe91kCw/wCowCgYIKoZIzj0EAwIw
FDESMBAGA1UEAwwJbG9jYWxob3N0MCAXDTI2MDcyMDAxNTk0NloYDzIxMjYwNjI2
MDE1OTQ2WjAUMRIwEAYDVQQDDAlsb2NhbGhvc3QwWTATBgcqhkjOPQIBBggqhkjO
PQMBBwNCAARX7qdGpAISx67pBMhvF0hP2dKm3jW4QdrzgkVIUEmY7vtfuYb7Oqe5
O4I879spjb1N15kqP4YrY8xyX+nZWlgmo28wbTAdBgNVHQ4EFgQUDcomeAw1ZJDp
gR5xu4Vya/XVDtowHwYDVR0jBBgwFoAUDcomeAw1ZJDpgR5xu4Vya/XVDtowDwYD
VR0TAQH/BAUwAwEB/zAaBgNVHREEEzARhwR/AAABgglsb2NhbGhvc3QwCgYIKoZI
zj0EAwIDSAAwRQIgL2CurOasH6ugR0JQJms8QOc4TUl2tQBKECc50OKFDrsCIQDv
325y6Ar4FWbEJ2mfeGxQAb7hhfWf9BbbZoP9mEMSYA==
-----END CERTIFICATE-----
)";

inline constexpr char tls_test_key[] = R"(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg0glIOdMfoiTaVkJT
thVSEbX1owL7mOXA8CQNVDdjCXOhRANCAARX7qdGpAISx67pBMhvF0hP2dKm3jW4
QdrzgkVIUEmY7vtfuYb7Oqe5O4I879spjb1N15kqP4YrY8xyX+nZWlgm
-----END PRIVATE KEY-----
)";

// One-connection-at-a-time synchronous TLS server on an ephemeral loopback
// port. Plain requests go through on_request (absent handler -> 404); a
// websocket upgrade switches the connection to on_message, which maps every
// received text frame to an optional reply. Declare it before the scheduler in
// tests so it is destroyed after all client sockets are gone.
class tls_test_server
{
public:
    using request_type = boost::beast::http::request<boost::beast::http::string_body>;
    using response_type = boost::beast::http::response<boost::beast::http::string_body>;
    using http_handler = std::function<response_type(const request_type&)>;
    using ws_handler = std::function<std::string(const std::string&)>;

private:
    boost::asio::io_context m_io;
    boost::asio::ssl::context m_ssl{boost::asio::ssl::context::tls_server};
    boost::asio::ip::tcp::acceptor m_acceptor;
    http_handler m_on_request;
    ws_handler m_on_message;
    std::atomic<bool> m_stopped{false};
    std::atomic<int> m_session_fd{-1};
    std::thread m_thread;

public:
    explicit tls_test_server(http_handler on_request, ws_handler on_message = {})
        : m_acceptor(m_io, {boost::asio::ip::address_v4::loopback(), 0})
        , m_on_request(std::move(on_request))
        , m_on_message(std::move(on_message))
    {
        m_ssl.use_certificate_chain(boost::asio::buffer(tls_test_cert, sizeof(tls_test_cert) - 1));
        m_ssl.use_private_key(boost::asio::buffer(tls_test_key, sizeof(tls_test_key) - 1), boost::asio::ssl::context::pem);
        m_thread = std::thread([this] { serve(); });
    }

    ~tls_test_server()
    {
        m_stopped = true;
        // The client scheduler keeps its sockets open past the test scope, so a
        // blocked session read must be broken explicitly; a throwaway plain
        // connect then unblocks the blocking accept and serve() observes
        // m_stopped and returns, letting the join complete.
        if (int fd = m_session_fd.exchange(-1); fd != -1)
            ::shutdown(fd, SHUT_RDWR);
        boost::asio::io_context wake_io;
        boost::asio::ip::tcp::socket wake_socket(wake_io);
        boost::system::error_code ec;
        wake_socket.connect(m_acceptor.local_endpoint(), ec);
        m_thread.join();
    }

    uint16_t port() const { return m_acceptor.local_endpoint().port(); }
    std::string http_url(const std::string& path) const { return "https://127.0.0.1:" + std::to_string(port()) + path; }
    std::string ws_url(const std::string& path) const { return "wss://127.0.0.1:" + std::to_string(port()) + path; }

private:
    void serve()
    {
        while (!m_stopped) {
            try {
                boost::asio::ip::tcp::socket socket = m_acceptor.accept();
                if (m_stopped)
                    return;
                m_session_fd = socket.native_handle();
                boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream(std::move(socket), m_ssl);
                stream.handshake(boost::asio::ssl::stream_base::server);
                boost::beast::flat_buffer buffer;
                request_type request;
                boost::beast::http::read(stream, buffer, request);
                if (boost::beast::websocket::is_upgrade(request))
                    serve_websocket(std::move(stream), request);
                else
                    serve_http(stream, request);
                m_session_fd = -1;
            }
            catch (...) { m_session_fd = -1; }
        }
    }

    void serve_http(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream, const request_type& request)
    {
        response_type response = m_on_request ? m_on_request(request)
                                              : response_type{boost::beast::http::status::not_found, request.version()};
        response.prepare_payload();
        boost::beast::http::write(stream, response);
        // No TLS shutdown: waiting for the client's close_notify would block the
        // serve thread, and the client keeps its stream open after reading.
        boost::system::error_code ec;
        stream.next_layer().close(ec);
    }

    void serve_websocket(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>&& stream, const request_type& request)
    {
        boost::beast::websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> websocket(std::move(stream));
        websocket.accept(request);
        while (!m_stopped) {
            boost::beast::flat_buffer buffer;
            websocket.read(buffer);
            std::string reply = m_on_message ? m_on_message(boost::beast::buffers_to_string(buffer.data())) : std::string{};
            if (!reply.empty()) {
                websocket.text(true);
                websocket.write(boost::asio::buffer(reply));
            }
        }
    }
};

} // namespace scratcher::test

#endif // TEST_CONNECT_TLS_TEST_SERVER_HPP
