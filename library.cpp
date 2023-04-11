#include "library.h"
#include <boost/regex.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <boost/thread/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <map>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

int EnableVerbose = 0;

class session;

on_connect_t on_connect_cb = nullptr;
on_fail_t on_fail_cb = nullptr;
on_disconnect_t on_disconnect_cb = nullptr;
on_data_t on_data_cb = nullptr;

// Global variables
net::io_context Ioc;
// Global instance of session class which contains callback functions to handle websocket async operations.
std::shared_ptr<session> Session_Ioc;
// Make sure the io_context thread exists through whole dll lifecycle until Ioc.run() is unblocked
boost::thread New_Thread;
// This is for making sure we won't get dirty Is_Connected value from main thread or io_context thread
boost::mutex mtx_;
// To indicate if the server is running or not
bool Is_Connected = false;

std::string utf8_encode(std::wstring const& wstr)
{
    if (wstr.empty()) {
        return {};
    }
#if _WIN32 || _WIN64
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
#else
    std::mbstate_t state{};

    auto in         = wstr.c_str();
    int size_needed = std::wcsrtombs(nullptr, &in, wstr.size(), &state);

    std::string strTo(1 + size_needed, 0);
    size_t n = std::wcsrtombs(strTo.data(), &in, wstr.size(), &state);

    if (n == -1ul) {
        throw std::domain_error("wcsrtombs");
    }
    strTo.resize(n);
    return strTo;
#endif
}

// Convert an UTF8 string to a wide Unicode String
std::wstring utf8_decode(std::string const& str)
{
    if (str.empty()) {
        return {};
    }
#if _WIN32 || _WIN64
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
#else
    std::mbstate_t state{};

    char const* in     = str.c_str();
    size_t size_needed = std::mbsrtowcs(nullptr, &in, 0, &state);

    std::wstring wstrTo(1 + size_needed, 0);
    size_needed = std::mbsrtowcs(wstrTo.data(), &in, wstrTo.size(), &state);

    if (size_needed == -1ul) {
        throw std::domain_error("mbsrtowcs");
    }
    wstrTo.resize(size_needed);
    return wstrTo;
#endif
}

/// Print error related information in stderr
/// \param ec instance that contains error related information
/// \param what customize prefix in output
void fail(beast::error_code ec, wchar_t const* what) {
    std::wcerr << what << L": " << utf8_decode(ec.message()) << std::endl;
}

class session : public std::enable_shared_from_this<session> {
    tcp::resolver resolver_;
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::wstring host_;
    // path part in url. For example: /v2/ws
    std::wstring path_;
    std::wstring text_;
    bool is_first_write_;

public:
    /// Resolver and socket require an io_context
    /// \param ioc
    explicit
    session(net::io_context &ioc)
            : resolver_(net::make_strand(ioc)), ws_(net::make_strand(ioc)), is_first_write_(false) {
    }
    /// Send message to remote websocket server
    /// \param data to be sent
    void send_message(const std::wstring &data) {
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL ">> Sending message: " << data << std::endl;

      const std::string to_send = utf8_encode(data);

      ws_.async_write(
          net::buffer(to_send),
                beast::bind_front_handler(
                        &session::on_write,
                        shared_from_this()));
    }

    /// Close the connect between websocket client and server. It call async_close to call a callback function which also calls user registered callback function to deal with close event.
    void disconnect() {
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> Disconnecting" << std::endl;

        ws_.async_close(websocket::close_code::normal,
                        beast::bind_front_handler(
                                &session::on_close,
                                shared_from_this()));
    }

    /// Start the asynchronous operation
    /// \param host host to be connected
    /// \param port tcp port to be connected
    /// \param text <not used>
    void
    run(
            wchar_t const *host,
            wchar_t const *port,
            wchar_t const *path,
            wchar_t const *text) {
        // Save these for later
        host_ = host;
        text_ = text;
        path_ = path;

//        std::wcout << L"host_: " << host << L", port: " << port << L", path_: " << path_ << std::endl;

        const std::string utf_host = utf8_encode(host);
        const std::wstring w_port(port);
        const std::string utf_port = utf8_encode(port);

        // Look up the domain name
        resolver_.async_resolve(
                utf_host,
                utf_port,
                beast::bind_front_handler(
                        &session::on_resolve,
                        shared_from_this()));
    }

    /// Callback function registered by async_resolve method. It is called after resolve operation is done. It will call async_connect to issue async connecting operation with callback function
    /// \param ec
    /// \param results
    void
    on_resolve(
            beast::error_code ec,
            const tcp::resolver::results_type &results) {
        if (ec) {
            if(on_fail_cb)
                on_fail_cb(L"resolve");
            return fail(ec, L"resolve");
        }

        // Set the timeout for the operation
        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a lookup
        beast::get_lowest_layer(ws_).async_connect(
                results,
                beast::bind_front_handler(
                        &session::on_connect,
                        shared_from_this()));
    }

    /// Callback function registered by async_connect method. In callback function, it call async_handshake to actually do websocket handshake operation and register on_handshake callback.
    /// \param ec instance of error code
    /// \param ep endpoint type.
    void
    on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> in on connect" << std::endl;
        if (ec) {
            if(on_fail_cb)
                on_fail_cb(L"connect");
            return fail(ec, L"connect");
        }

        // Turn off the timeout on the tcp_stream, because
        // the websocket stream has its own timeout system.
        beast::get_lowest_layer(ws_).expires_never();

        // Set suggested timeout settings for the websocket
        ws_.set_option(
                websocket::stream_base::timeout::suggested(
                        beast::role_type::client));

        // Set a decorator to change the User-Agent of the handshake
        ws_.set_option(websocket::stream_base::decorator(
                [](websocket::request_type &req) {
                    req.set(http::field::user_agent,
                            std::string(BOOST_BEAST_VERSION_STRING) +
                                    " websocket-client-async");
                }));

        // Update the host_ string. This will provide the value of the
        // Host HTTP header during the WebSocket handshake.
        // See https://tools.ietf.org/html/rfc7230#section-5.4
        host_ += L':' + std::to_wstring(ep.port());

        std::string utf_host = utf8_encode(host_);
        std::string utf_path = utf8_encode(path_);

        // Perform the websocket handshake
        ws_.async_handshake(utf_host, utf_path,
                            beast::bind_front_handler(
                                    &session::on_handshake,
                                    shared_from_this()));
    }

    /// Callback function registered by async_handshake. In callback function, it calls async_read to waiting for data from websocket server.
    /// \param ec instance of error code
    void
    on_handshake(beast::error_code ec) {
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> in on handshake" << std::endl;
        if (ec) {
            if(on_fail_cb)
                on_fail_cb(L"handshake");
            return fail(ec, L"handshake");
        }

        if (on_connect_cb)
            on_connect_cb();

        // Send the message
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> issue new async_read in on_handshake" << std::endl;
        ws_.async_read(
                buffer_,
                beast::bind_front_handler(
                        &session::on_read,
                        shared_from_this()));
//        ws_.async_write(
//                net::buffer(text_),
//                beast::bind_front_handler(
//                        &session::on_write,
//                        shared_from_this()));
    }

    /// Callback registered by async_write. It issue an async_read call to wait for data from websocket server
    /// \param ec instance of error code
    /// \param bytes_transferred count of bytes which is sent to server
    void
    on_write(
            beast::error_code ec,
            std::size_t bytes_transferred) {
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> in on write" << std::endl;
        boost::ignore_unused(bytes_transferred);

        if (ec) {
            if(on_fail_cb)
                on_fail_cb(L"write");
            return fail(ec, L"write");
        }

        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> issue new async_read in on_write" << std::endl;
        ws_.async_read(
                buffer_,
                beast::bind_front_handler(
                        &session::on_read,
                        shared_from_this()));
    }

    /// Callback registered by async_read. It calls user registered callback to actually process the data. And then issue another async_read to wait for data from server again.
    /// \param ec instance of error code
    /// \param bytes_transferred
    void
    on_read(
            beast::error_code ec,
            std::size_t bytes_transferred) {
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> in on read" << std::endl;
        boost::ignore_unused(bytes_transferred);

        {
            boost::lock_guard<boost::mutex> guard(mtx_);
            if(!Is_Connected) {
                return;
            }

        }

        // error occurs
        if (ec) {
            if(on_fail_cb)
                on_fail_cb(L"read");
            return fail(ec, L"read");
        }

        const std::string data = beast::buffers_to_string(buffer_.data());
        const std::wstring wdata(data.begin(), data.end());
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> received[" << bytes_transferred << L"] " << wdata << std::endl;

//        const std::string str(wdata.begin(), wdata.end());

        if (on_data_cb)
            on_data_cb(wdata.c_str(), wdata.length());

        buffer_.consume(buffer_.size());

        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> issue new async_read in on_read" << std::endl;
        ws_.async_read(
                buffer_,
                beast::bind_front_handler(
                        &session::on_read,
                        shared_from_this()));

        // Close the WebSocket connection
        // ws_.async_close(websocket::close_code::normal,
        //     beast::bind_front_handler(
        //         &session::on_close,
        //         shared_from_this()));
    }

    /// It is only called when client proactively closes connection by calling websocket_disconnect.
    /// \param ec instance of error code
    void
    on_close(beast::error_code ec) {
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> in on close" << std::endl;
        if (ec)
            fail(ec, L"close");

//        ws_.next_layer().cancel();
//        ws_.next_layer().close();
        Ioc.stop();

        if (on_disconnect_cb)
            on_disconnect_cb();

        // If we get here then the connection is closed gracefully

        // The make_printable() function helps print a ConstBufferSequence
        // std::wcout << beast::make_printable(buffer_.data()) << std::endl;
    }
};

EXPORT void enable_verbose(intptr_t enabled) {
    if(enabled)
        std::wcout << L"<WsDll-" ARCH_LABEL "> Verbose output enabled" << std::endl;
    else
        std::wcout << L"<WsDll-" ARCH_LABEL "> Verbose output disabled" << std::endl;

    EnableVerbose = enabled;
}

EXPORT size_t websocket_connect(const wchar_t *szServer) {
    {
        boost::lock_guard<boost::mutex> guard(mtx_);
        if(Is_Connected) {
            std::wcerr << L"<WsDll-" ARCH_LABEL "> Server is running. Can't run again." << std::endl;
            return 0;
        }
    }


    boost::regex pat(R"(^wss?://([\w\.]+):(\d+)(.*)$)");

    const std::wstring line(szServer);
    const std::string utf_line = utf8_encode(line);

    boost::smatch matches;
    if (!boost::regex_match(utf_line, matches, pat)) {
        std::wcerr << L"<WsDll-" ARCH_LABEL "> failed to parse host & port. Correct example: ws://localhost:8080/" << std::endl;
        return 0;
    }

    const std::wstring host(matches[1].begin(), matches[1].end());
    const std::wstring port(matches[2].begin(), matches[2].end());
    const std::wstring path(matches[3].begin(), matches[3].end());
    if(EnableVerbose)
        std::wcout << L"<WsDll-" ARCH_LABEL "> host: " << host << L", port: " << port << L", path: " << path << std::endl;

    if(EnableVerbose)
        std::wcout << L"<WsDll-" ARCH_LABEL "> Connecting to the server: " << szServer << std::endl;

    Session_Ioc = std::make_shared<session>(Ioc);
    // must pass value to lambda otherwise it will cause unexpected exit (no any error message)
    New_Thread = boost::thread([path, host, port]() {
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> in thread" << std::endl;
        Ioc.stop();
        Ioc.reset();
        std::wstring tmp_path = path;
        boost::trim(tmp_path);
        if(tmp_path.empty()) {
            tmp_path.append(L"/");
        }
        Session_Ioc->run(host.c_str(), port.c_str(), tmp_path.c_str(), L"");
        {
            boost::lock_guard<boost::mutex> guard(mtx_);
            Is_Connected = true;
        }
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> Calling Ioc.run()" << std::endl;
        Ioc.run();
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> After calling Ioc.run()" << std::endl;
        {
            boost::lock_guard<boost::mutex> guard(mtx_);
            Is_Connected = false;
        }
        Ioc.stop();
    });

    return 1;
}

EXPORT size_t websocket_disconnect() {
    {
        boost::lock_guard<boost::mutex> guard(mtx_);
        if(!Is_Connected) {
            std::wcerr << L"<WsDll-" ARCH_LABEL "> Server is not running. Can't disconnect." << std::endl;
            return 0;
        }
    }
    {
        boost::lock_guard<boost::mutex> guard(mtx_);
        Is_Connected = false;
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> Connection is closed after Ioc.run() is completed." << std::endl;
    }

    Session_Ioc->disconnect();
    return 1;
}

EXPORT size_t websocket_send(const wchar_t *szMessage, size_t dwLen, bool isBinary) {
    {
        boost::lock_guard<boost::mutex> guard(mtx_);
        if(!Is_Connected) {
            std::wcerr << L"<WsDll-" ARCH_LABEL "> Server is not running. Can't send data." << std::endl;
            return 0;
        }
    }

    Session_Ioc->send_message(szMessage);

    return 1;
}

EXPORT size_t websocket_isconnected() {
    {
        boost::lock_guard<boost::mutex> guard(mtx_);
        return Is_Connected ? 1 : 0;
    }
}

EXPORT size_t websocket_register_on_connect_cb(size_t dwAddress) {
    if(EnableVerbose)
        std::wcout << L"<WsDll-" ARCH_LABEL "> registering on_connect callback" << std::endl;
    on_connect_cb = reinterpret_cast<on_connect_t>(dwAddress);

    return 1;
}

EXPORT size_t websocket_register_on_fail_cb(size_t dwAddress) {
    if(EnableVerbose)
        std::wcout << L"<WsDll-" ARCH_LABEL "> registering on_fail callback" << std::endl;
    on_fail_cb = reinterpret_cast<on_fail_t>(dwAddress);

    return 1;
}

EXPORT size_t websocket_register_on_disconnect_cb(size_t dwAddress) {
    if(EnableVerbose)
        std::wcout << L"<WsDll-" ARCH_LABEL "> registering on_disconnect callback" << std::endl;
    on_disconnect_cb = reinterpret_cast<on_disconnect_t>(dwAddress);

    return 1;
}

EXPORT size_t websocket_register_on_data_cb(size_t dwAddress) {
    if(EnableVerbose)
        std::wcout << L"<WsDll-" ARCH_LABEL "> registering on_data callback" << std::endl;
    on_data_cb = reinterpret_cast<on_data_t>(dwAddress);

    return 1;
}
