#ifndef __BASE_CONNECTION__
#define __BASE_CONNECTION__

#include <string>
#include <sstream>
#include <map>
#include <deque>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "libed2k/intrusive_ptr_base.hpp"
#include "libed2k/stat.hpp"
#include "libed2k/assert.hpp"
#include "libed2k/config.hpp"
#include "libed2k/size_type.hpp"
#include "libed2k/socket.hpp"
#include "libed2k/chained_buffer.hpp"
#include "libed2k/log.hpp"
#include "libed2k/archive.hpp"
#include "libed2k/packet_struct.hpp"
#include "libed2k/deadline_timer.hpp"
#include "libed2k/bandwidth_limit.hpp"

namespace libed2k {

enum { header_size = sizeof(libed2k_header) };

namespace aux {
class session_impl;
}

class base_connection : public intrusive_ptr_base<base_connection>, public boost::noncopyable {
    friend class aux::session_impl;

   public:
    base_connection(aux::session_impl& ses);
    base_connection(aux::session_impl& ses, boost::shared_ptr<tcp::socket> s, const tcp::endpoint& remote);
    virtual ~base_connection();

    virtual void disconnect(const error_code& ec, int error = 0);
    bool is_disconnecting() const { return m_disconnecting; }

    /** connection closed when his socket is not opened */
    bool is_closed() const { return !m_socket || !m_socket->is_open(); }
    const tcp::endpoint& remote() const { return m_remote; }
    boost::shared_ptr<tcp::socket> socket() { return m_socket; }

    const stat& statistics() const { return m_statistics; }

    typedef boost::iostreams::basic_array_source<char> Device;

    enum channels { upload_channel, download_channel, num_channels };

   protected:
    // constructor method
    void reset();

    virtual void do_read();
    virtual void do_write(int quota = (std::numeric_limits<int>::max)());

    template <typename T>
    void write_struct(const T& t) {
        write_message(make_message(t));
    }

    void write_message(const message& msg);

    void copy_send_buffer(const char* buf, int size);

    template <class Destructor>
    void append_send_buffer(char* buffer, int size, Destructor const& destructor) {
        m_send_buffer.append_buffer(buffer, size, size, destructor);
    }

    int send_buffer_size() const { return m_send_buffer.size(); }
    int send_buffer_capacity() const { return m_send_buffer.capacity(); }

    virtual void on_timeout(const error_code& e);
    virtual void on_sent(const error_code& e, std::size_t bytes_transferred) = 0;

    /**
     * call when socket got packets header
     */
    void on_read_header(const error_code& error, size_t nSize);

    /**
     * call when socket got packets body and call users call back
     */
    void on_read_packet(const error_code& error, size_t nSize);

    /**
     * order write handler - executed while message order not empty
     */
    void on_write(const error_code& error, size_t nSize);

    /**
     * deadline timer handler
     */
    void check_deadline();

    /**
     * will call from external handlers for extract buffer into structure
     * on error return false
     */
    template <typename T>
    bool decode_packet(T& t) {
        try {
            if (!m_in_container.empty()) {
                boost::iostreams::stream_buffer<base_connection::Device> buffer(&m_in_container[0],
                                                                                m_in_header.m_size - 1);
                std::istream in_array_stream(&buffer);
                archive::ed2k_iarchive ia(in_array_stream);
                ia >> t;
            }
        } catch (libed2k_exception& e) {
            DBG("Error on conversion " << e.what());
            return (false);
        }

        return (true);
    }

    template <typename Self>
    boost::intrusive_ptr<Self> self_as() {
        return boost::intrusive_ptr<Self>((Self*)this);
    }

    template <typename Self>
    boost::intrusive_ptr<const Self> self_as() const {
        return boost::intrusive_ptr<const Self>((const Self*)this);
    }

    /**
     * handler on receive data packet
     * @param read data buffer
     * @param error code
     */
    typedef boost::function<void(const error_code&)> packet_handler;

    // handler storage type - first argument opcode + protocol
    typedef std::map<std::pair<proto_type, proto_type>, packet_handler> handler_map;

    void add_handler(std::pair<proto_type, proto_type> ptype, packet_handler handler);

    aux::session_impl& m_ses;
    boost::shared_ptr<tcp::socket> m_socket;
    deadline_timer m_deadline;          //!< deadline timer for reading operations
    libed2k_header m_in_header;         //!< incoming message header
    socket_buffer m_in_container;       //!< buffer for incoming messages
    socket_buffer m_in_gzip_container;  //!< buffer for compressed data
    chained_buffer m_send_buffer;       //!< buffer for outgoing messages
    tcp::endpoint m_remote;

    // upload and download channel state
    char m_channel_state[2];

    // this is true if this connection has been added
    // to the list of connections that will be closed.
    bool m_disconnecting;

    handler_map m_handlers;

    // statistics about upload and download speeds
    // and total amount of uploads and downloads for
    // this connection
    stat m_statistics;

    //
    // Custom memory allocation for asynchronous operations
    //
    template <std::size_t Size>
    class handler_storage {
       public:
        boost::aligned_storage<Size> bytes;
    };

    handler_storage<LIBED2K_READ_HANDLER_MAX_SIZE> m_read_handler_storage;
    handler_storage<LIBED2K_WRITE_HANDLER_MAX_SIZE> m_write_handler_storage;

    template <class Handler, std::size_t Size>
    class allocating_handler {
       public:
        allocating_handler(Handler const& h, handler_storage<Size>& s) : handler(h), storage(s) {}

        template <class A0>
        void operator()(A0 const& a0) const {
            handler(a0);
        }

        template <class A0, class A1>
        void operator()(A0 const& a0, A1 const& a1) const {
            handler(a0, a1);
        }

        template <class A0, class A1, class A2>
        void operator()(A0 const& a0, A1 const& a1, A2 const& a2) const {
            handler(a0, a1, a2);
        }

        friend void* asio_handler_allocate(std::size_t size, allocating_handler<Handler, Size>* ctx) {
            return &ctx->storage.bytes;
        }

        friend void asio_handler_deallocate(void*, std::size_t, allocating_handler<Handler, Size>* ctx) {}

        Handler handler;
        handler_storage<Size>& storage;
    };

    template <class Handler>
    allocating_handler<Handler, LIBED2K_READ_HANDLER_MAX_SIZE> make_read_handler(Handler const& handler) {
        return allocating_handler<Handler, LIBED2K_READ_HANDLER_MAX_SIZE>(handler, m_read_handler_storage);
    }

    template <class Handler>
    allocating_handler<Handler, LIBED2K_WRITE_HANDLER_MAX_SIZE> make_write_handler(Handler const& handler) {
        return allocating_handler<Handler, LIBED2K_WRITE_HANDLER_MAX_SIZE>(handler, m_write_handler_storage);
    }
};
}

#endif
