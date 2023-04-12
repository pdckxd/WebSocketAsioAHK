#include "library.h"
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/regex.hpp>
#include <iomanip>
#include <iostream>

namespace net       = boost::asio;          // from <boost/asio.hpp>
namespace beast     = boost::beast;         // from <boost/beast.hpp>
namespace http      = beast::http;          // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;     // from <boost/beast/websocket.hpp>
using tcp           = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>
using namespace std::chrono_literals;

#define TRACE(stream, msg) do { stream << L"<WsDll-" ARCH_LABEL "> " << msg << std::endl; } while(0)
#define VERBOSE(msg) do { if (s_enable_verbose) { TRACE(std::wcout, msg); } } while(0)
#define COUT(msg) TRACE(std::wcout, msg)
#define CERR(msg) TRACE(std::wcerr, msg)

namespace /*anon*/ {
    static on_connect_t    s_on_connect_cb{nullptr};
    static on_fail_t       s_on_fail_cb{nullptr};
    static on_disconnect_t s_on_disconnect_cb{nullptr};
    static on_data_t       s_on_data_cb{nullptr};

    // Global variables
    static std::atomic_bool s_enable_verbose{false};

    class Session;
    using SessionPtr = std::shared_ptr<Session>;

    struct Manager {
        // TODO maybe allow multiple sessions and use weak pointers instead?
        static inline SessionPtr Install(SessionPtr sess)
        {
            std::shared_ptr<Session> no_session{};
            if (!std::atomic_compare_exchange_strong(&s_instance_unsafe, &no_session, sess)) {
                return nullptr;
            }
            return sess;
        }

        static inline SessionPtr Active() {
            return std::atomic_load(&s_instance_unsafe);
        }

        static inline bool Clear(SessionPtr sess)
        {
            return std::atomic_compare_exchange_strong(&s_instance_unsafe, &sess, {});
        }

      private:
        static SessionPtr s_instance_unsafe; // use atomic_ operations to safely access
    };
    /*static*/ SessionPtr Manager::s_instance_unsafe;

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
#else // TODO review, should work on windows as well
        std::mbstate_t state{};

        auto in          = wstr.c_str();
        int  size_needed = std::wcsrtombs(nullptr, &in, 0, &state);

        std::string strTo(1 + size_needed, 0);
        size_t n = std::wcsrtombs(&strTo[0], &in, strTo.size(), &state);

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
        int          size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
#else
        std::mbstate_t state{};

        char const* in          = str.c_str();
        size_t      size_needed = std::mbsrtowcs(nullptr, &in, 0, &state);

        std::wstring wstrTo(1 + size_needed, 0);
        size_t n = std::mbsrtowcs(&wstrTo[0], &in, wstrTo.size(), &state);

        if (n == -1ul) {
            throw std::domain_error("mbsrtowcs");
        }
        wstrTo.resize(size_needed);
        return wstrTo;
#endif
    }

    class Session : public std::enable_shared_from_this<Session> {
        net::thread_pool                     ioc_{1};
        websocket::stream<beast::tcp_stream> ws_{make_strand(ioc_.get_executor())};
        tcp::resolver                        resolver_{ws_.get_executor()};

        beast::flat_buffer buffer_;
        std::wstring       host_, path_; // path part in url. For example: /v2/ws

        /// Print error related information in stderr
        /// \param ec instance that contains error related information
        /// \param what customize prefix in output
        void fail(beast::error_code ec, wchar_t const* what)
        {
            std::wstring const msg = what //
                ? what + (L": " + utf8_decode(ec.message()))
                : utf8_decode(ec.message());

            if (s_on_fail_cb)
                s_on_fail_cb(msg.c_str());
            CERR(msg);
        }

      public:
        Session() = default;

        /// Send message to remote websocket server
        /// \param data to be sent
        void send_message(std::wstring const& data)
        {
            VERBOSE(L"Sending message: " << data);

            const std::string to_send = utf8_encode(data); // BIG OOPS! lifetime ends at function exit
            ws_.async_write(net::buffer(to_send),
                            beast::bind_front_handler(&Session::on_write, shared_from_this()));
        }

        /// Close the connect between websocket client and server. It call
        /// async_close to call a callback function which also calls user
        /// registered callback function to deal with close event.
        void disconnect()
        {
            post(ws_.get_executor(), std::bind(&Session::do_disconnect, shared_from_this()));
        }

        /// Start the asynchronous operation
        /// \param host host to be connected
        /// \param port tcp port to be connected
        void run(std::wstring host, std::wstring port, std::wstring path)
        {
            // Save these for later
            host_ = std::move(host);
            path_ = std::move(path);

            VERBOSE(L"Run host_: " << host << L", port: " << port << L", path_: " << path_);

            // Look up the domain name
            resolver_.async_resolve(utf8_encode(host), utf8_encode(port),
                                    beast::bind_front_handler(&Session::on_resolve, shared_from_this()));
        }

      private: // all private (do_*/on_*) assumed on strand
        std::deque<std::wstring> _outbox; // NOTE: reference stability of elements

        void do_send_message(std::wstring data)
        {
            VERBOSE(L"Sending message: " << data);
            _outbox.push_back(std::move(data)); // extend lifetime to completion of async write

            if (_outbox.size()==1) // need to start write chain?
                do_write_loop();
        }

        void do_disconnect()
        {
            VERBOSE(L"Disconnecting");
            ws_.async_close(websocket::close_code::normal,
                            beast::bind_front_handler(&Session::on_close, shared_from_this()));
        }

        /// Callback function registered by async_resolve method. It is
        /// called after resolve operation is done. It will call
        /// async_connect to issue async connecting operation with
        /// callback function
        /// \param ec
        /// \param results
        void on_resolve(beast::error_code ec, tcp::resolver::results_type const& results)
        {
            VERBOSE(L"In on_resolve");
            if (ec)
                return fail(ec, L"resolve");

            // Set the timeout for the operation
            beast::get_lowest_layer(ws_).expires_after(30s);

            // Make the connection on the IP address we get from a lookup
            beast::get_lowest_layer(ws_).async_connect(
                results, beast::bind_front_handler(&Session::on_connect, shared_from_this()));
        }

        void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep)
        {
            VERBOSE(L"In on_connect");
            if (ec)
                return fail(ec, L"connect");

            // Turn off the timeout on the tcp_stream, because
            // the websocket stream has its own timeout system.
            beast::get_lowest_layer(ws_).expires_never();

            // Set suggested timeout settings for the websocket
            ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

            // Set a decorator to change the User-Agent of the handshake
            ws_.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                req.set(http::field::user_agent,
                        std::string(BOOST_BEAST_VERSION_STRING) + " WsDll");
            }));

            // Perform the websocket handshake

            // Host HTTP header includes the port. See https://tools.ietf.org/html/rfc7230#section-5.4
            ws_.async_handshake(utf8_encode(host_) + ":" + std::to_string(ep.port()), utf8_encode(path_),
                                beast::bind_front_handler(&Session::on_handshake, shared_from_this()));
        }

        void on_handshake(beast::error_code ec)
        {
            VERBOSE(L"In on_handshake");
            if (ec)
                return fail(ec, L"handshake");

            if (s_on_connect_cb)
                s_on_connect_cb();

            // Send the message
            VERBOSE(L"Issue async_read in on_handshake");
            ws_.async_read(buffer_, beast::bind_front_handler(&Session::on_read, shared_from_this()));
        }

        void do_write_loop()
        {
            if (_outbox.empty())
                return;

            ws_.async_write(net::buffer(_outbox.front()),
                            beast::bind_front_handler(&Session::on_write, shared_from_this()));
        }

        void on_write(beast::error_code ec, std::size_t bytes_transferred)
        {
            VERBOSE(L"In on_write");
            boost::ignore_unused(bytes_transferred);

            if (ec)
                return fail(ec, L"write");

            _outbox.pop_front();
            do_write_loop(); // drain _outbox
        }

        void on_read(beast::error_code ec, std::size_t bytes_transferred)
        {
            VERBOSE(L"In on_read");

            // error occurs
            if (ec)
                return fail(ec, L"read");

            const std::wstring wdata = utf8_decode(beast::buffers_to_string(buffer_.data()));
            VERBOSE(L"Received[" << bytes_transferred << L"] " << std::quoted(wdata));

            if (s_on_data_cb)
                s_on_data_cb(wdata.c_str(), wdata.length());

            buffer_.consume(bytes_transferred); // some forms of async_read can read extra data

            VERBOSE(L"Issue new async_read in on_read");
            ws_.async_read(buffer_, beast::bind_front_handler(&Session::on_read, shared_from_this()));
        }

        /// Only called when client proactively closes connection by calling
        /// websocket_disconnect. 
        /// \param ec instance of error code
        void on_close(beast::error_code ec)
        {
            VERBOSE(L"In on_close");
            if (ec)
                fail(ec, L"close");

            if (s_on_disconnect_cb)
                s_on_disconnect_cb();

            get_lowest_layer(ws_).cancel(); // cause all async operations to abort

            if (!Manager::Clear(shared_from_this())) {
                // CERR(L"Could not remove active session"); // redundant message when Sessions::Install fails
            }
        }
    };
}

EXPORT void enable_verbose(intptr_t enabled)
{
    COUT(L"Verbose output " << (enabled ? "enabled" : "disabled"));
    s_enable_verbose = enabled;
}

EXPORT size_t websocket_connect(wchar_t const* szServer)
{
    auto new_session = Manager::Install(std::make_shared<Session>());
    if (!new_session) {
        COUT(L"A session is already active.");
        return 0;
    }
    assert(new_session == Manager::Active());

    VERBOSE(L"Connecting to the server: " << szServer);

    static boost::wregex const s_pat(LR"(^wss?://([\w\.]+):(\d+)(.*)$)");

    boost::wsmatch matches;
    if (!boost::regex_match(std::wstring(szServer), matches, s_pat)) {
        COUT(L"Failed to parse host & port. Correct example: ws://localhost:8080/");
        return 0;
    }

    std::wstring path(boost::trim_copy(matches[3].str()));
    if (path.empty())
        path = L"/";

    new_session->run(matches[1], matches[2], std::move(path));

    return 1;
}

EXPORT size_t websocket_disconnect()
{
    if (SessionPtr sess = Manager::Active()) {
        sess->disconnect();
        return 1;
    }
    CERR(L"Session not active. Can't disconnect.");
    return 0;
}

EXPORT size_t websocket_send(wchar_t const* szMessage, size_t dwLen, bool isBinary)
{
    if (SessionPtr sess = Manager::Active()) {
        sess->send_message(szMessage);
        return 1;
    }
    CERR(L"Session not active. Can't send data.");
    return 0;
}

EXPORT size_t websocket_isconnected()
{
    return Manager::Active() != nullptr;
}

EXPORT size_t websocket_register_on_connect_cb(size_t dwAddress)
{
    VERBOSE(L"Registering on_connect callback");
    s_on_connect_cb = reinterpret_cast<on_connect_t>(dwAddress);

    return 1;
}

EXPORT size_t websocket_register_on_fail_cb(size_t dwAddress)
{
    VERBOSE(L"Registering on_fail callback");
    s_on_fail_cb = reinterpret_cast<on_fail_t>(dwAddress);

    return 1;
}

EXPORT size_t websocket_register_on_disconnect_cb(size_t dwAddress)
{
    VERBOSE(L"Registering on_disconnect callback");
    s_on_disconnect_cb = reinterpret_cast<on_disconnect_t>(dwAddress);

    return 1;
}

EXPORT size_t websocket_register_on_data_cb(size_t dwAddress)
{
    VERBOSE(L"Registering on_data callback");
    s_on_data_cb = reinterpret_cast<on_data_t>(dwAddress);

    return 1;
}
