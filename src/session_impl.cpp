
#ifndef WIN32
#include <sys/resource.h>
#endif

#include <algorithm>
#include <boost/format.hpp>
#include <boost/foreach.hpp>

#include "libed2k/session_impl.hpp"
#include "libed2k/session.hpp"
#include "libed2k/peer_connection.hpp"
#include "libed2k/socket.hpp"
#include "libed2k/transfer_handle.hpp"
#include "libed2k/transfer.hpp"
#include "libed2k/server_connection.hpp"
#include "libed2k/upnp.hpp"
#include "libed2k/natpmp.hpp"
#include "libed2k/constants.hpp"
#include "libed2k/log.hpp"
#include "libed2k/alert_types.hpp"
#include "libed2k/file.hpp"
#include "libed2k/util.hpp"
#include "libed2k/random.hpp"

namespace libed2k {
namespace aux {

session_impl_base::session_impl_base(const session_settings& settings)
    : m_io_service(),
      m_abort(false),
      m_settings(settings),
      m_transfers(),
      m_active_transfers(),
      m_alerts(m_io_service),
      m_tpm(m_alerts, settings.m_known_file) {}

session_impl_base::~session_impl_base() { abort(); }

void session_impl_base::abort() {
    if (m_abort) return;
    m_abort = true;
    m_tpm.stop();
}

void session_impl_base::post_transfer(add_transfer_params const& params) {
    DBG("session_impl_base::post_transfer");
    error_code ec;
    m_io_service.post(boost::bind(&session_impl_base::add_transfer, this, params, ec));
}

alert const* session_impl_base::wait_for_alert(time_duration max_wait) { return m_alerts.wait_for_alert(max_wait); }

md4_hash session_impl_base::callbacked_lowid(client_id_type id) {
    md4_hash res(md4_hash::invalid);
    lowid_callbacks_map::iterator itr = lowid_conn_dict.find(id);

    if (itr != lowid_conn_dict.end()) {
        res = itr->second;
        lowid_conn_dict.erase(itr);
    }

    return res;
}

bool session_impl_base::register_callback(client_id_type id, md4_hash filehash) {
    LIBED2K_ASSERT(filehash != md4_hash::invalid);
    std::pair<lowid_callbacks_map::iterator, bool> ret = lowid_conn_dict.insert(std::make_pair(id, filehash));
    return ret.second;
}

void session_impl_base::cleanup_callbacks() { lowid_conn_dict.clear(); }

void session_impl_base::set_alert_mask(boost::uint32_t m) { m_alerts.set_alert_mask(m); }

size_t session_impl_base::set_alert_queue_size_limit(size_t queue_size_limit_) {
    return m_alerts.set_alert_queue_size_limit(queue_size_limit_);
}

std::auto_ptr<alert> session_impl_base::pop_alert() {
    if (m_alerts.pending()) {
        return m_alerts.get();
    }

    return std::auto_ptr<alert>(0);
}

void session_impl_base::set_alert_dispatch(boost::function<void(alert const&)> const& fun) {
    m_alerts.set_dispatch_function(fun);
}

session_impl::session_impl(const fingerprint& id, const char* listen_interface, const session_settings& settings)
    : session_impl_base(settings),
      m_host_resolver(m_io_service),
      m_peer_pool(500),
      m_send_buffers(send_buffer_size),
      m_z_buffers(BLOCK_SIZE),
      m_skip_buffer(4096),
      m_filepool(40),
      m_disk_thread(m_io_service, boost::bind(&session_impl::on_disk_queue, this), m_filepool, BLOCK_SIZE),
      m_half_open(m_io_service),
      m_download_rate(peer_connection::download_channel),
      m_upload_rate(peer_connection::upload_channel),
      m_server_connection(new server_connection(*this)),
      m_next_connect_transfer(m_active_transfers),
      m_paused(false),
      m_created(time_now_hires()),
      m_second_timer(seconds(1)),
      m_timer(m_io_service),
      m_last_tick(m_created),
      m_total_failed_bytes(0),
      m_total_redundant_bytes(0),
      m_queue_pos(0),
      m_udp_socket(m_io_service, boost::bind(&session_impl::on_receive_udp, this, _1, _2, _3, _4),
                   boost::bind(&session_impl::on_receive_udp_hostname, this, _1, _2, _3, _4), m_half_open)
#ifndef LIBED2K_DISABLE_DHT
      ,
      m_dht_announce_timer(m_io_service)
#endif
{
    DBG("*** create ed2k session ***");

    if (!listen_interface) listen_interface = "0.0.0.0";
    error_code ec;
    m_listen_interface = tcp::endpoint(ip::address::from_string(listen_interface, ec), settings.listen_port);

    if (ec) {
        ERR("session_impl::session_impl{" << ec.message() << "} on iface {" << listen_interface << "}");
    }

    LIBED2K_ASSERT_VAL(!ec, ec.message());

#ifdef WIN32
    // windows XP has a limit on the number of
    // simultaneous half-open TCP connections
    // here's a table:

    // windows version       half-open connections limit
    // --------------------- ---------------------------
    // XP sp1 and earlier    infinite
    // earlier than vista    8
    // vista sp1 and earlier 5
    // vista sp2 and later   infinite

    // windows release                     version number
    // ----------------------------------- --------------
    // Windows 7                           6.1
    // Windows Server 2008 R2              6.1
    // Windows Server 2008                 6.0
    // Windows Vista                       6.0
    // Windows Server 2003 R2              5.2
    // Windows Home Server                 5.2
    // Windows Server 2003                 5.2
    // Windows XP Professional x64 Edition 5.2
    // Windows XP                          5.1
    // Windows 2000                        5.0

    OSVERSIONINFOEX osv;
    memset(&osv, 0, sizeof(osv));
    osv.dwOSVersionInfoSize = sizeof(osv);
    GetVersionEx((OSVERSIONINFO*)&osv);

    // the low two bytes of windows_version is the actual
    // version.
    boost::uint32_t windows_version =
        ((osv.dwMajorVersion & 0xff) << 16) | ((osv.dwMinorVersion & 0xff) << 8) | (osv.wServicePackMajor & 0xff);

    // this is the format of windows_version
    // xx xx xx
    // |  |  |
    // |  |  + service pack version
    // |  + minor version
    // + major version

    // the least significant byte is the major version
    // and the most significant one is the minor version
    if (windows_version >= 0x060100) {
        // windows 7 and up doesn't have a half-open limit
        m_half_open.limit(0);
    } else if (windows_version >= 0x060002) {
        // on vista SP 2 and up, there's no limit
        m_half_open.limit(0);
    } else if (windows_version >= 0x060000) {
        // on vista the limit is 5 (in home edition)
        m_half_open.limit(4);
    } else if (windows_version >= 0x050102) {
        // on XP SP2 the limit is 10
        m_half_open.limit(9);
    } else {
        // before XP SP2, there was no limit
        m_half_open.limit(0);
    }
#endif

#if defined LIBED2K_BSD || defined LIBED2K_LINUX
    // ---- auto-cap open files ----

    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        DBG("max number of open files: " << rl.rlim_cur);

        // deduct some margin for epoll/kqueue, log files,
        // futexes, shared objects etc.
        rl.rlim_cur -= 20;

        // 80% of the available file descriptors should go
        m_settings.connections_limit = (std::min)(m_settings.connections_limit, int(rl.rlim_cur * 8 / 10));
        // 20% goes towards regular files
        m_filepool.resize((std::min)(m_filepool.size_limit(), int(rl.rlim_cur * 2 / 10)));

        DBG("max connections: " << m_settings.connections_limit);
        DBG("max files: " << m_filepool.size_limit());
    }
#endif  // LIBED2K_BSD || LIBED2K_LINUX

    m_bandwidth_channel[peer_connection::download_channel] = &m_download_channel;
    m_bandwidth_channel[peer_connection::upload_channel] = &m_upload_channel;

    update_rate_settings();
    update_connections_limit();

    m_io_service.post(boost::bind(&session_impl::on_tick, this, ec));

    m_tcp_mapping[0] = -1;
    m_tcp_mapping[1] = -1;
    m_udp_mapping[0] = -1;
    m_udp_mapping[1] = -1;
#ifdef LIBED2K_USE_OPENSSL
    m_ssl_mapping[0] = -1;
    m_ssl_mapping[1] = -1;
#endif

#ifdef LIBED2K_UPNP_LOGGING
    m_upnp_log.open("upnp.log", std::ios::in | std::ios::out | std::ios::trunc);
#endif
    m_thread.reset(new boost::thread(boost::ref(*this)));
}

session_impl::~session_impl() {
    DBG("*** shutting down session ***");
    m_io_service.post(boost::bind(&session_impl::abort, this));

    // we need to wait for the disk-io thread to
    // die first, to make sure it won't post any
    // more messages to the io_service containing references
    // to disk_io_pool inside the disk_io_thread. Once
    // the main thread has handled all the outstanding requests
    // we know it's safe to destruct the disk thread.
    DBG("waiting for disk io thread");
    m_disk_thread.join();

    DBG("waiting for main thread");
    m_thread->join();

    DBG("shutdown complete!");
}

const session_settings& session_impl::settings() const { return m_settings; }

void session_impl::set_settings(const session_settings& s) {
    LIBED2K_ASSERT_VAL(s.file_pool_size > 0, s.file_pool_size);

    // if disk io thread settings were changed
    // post a notification to that thread
    bool update_disk_io_thread = false;
    if (m_settings.cache_size != s.cache_size || m_settings.cache_expiry != s.cache_expiry ||
        m_settings.optimize_hashing_for_speed != s.optimize_hashing_for_speed ||
        m_settings.file_checks_delay_per_block != s.file_checks_delay_per_block ||
        m_settings.disk_cache_algorithm != s.disk_cache_algorithm ||
        m_settings.read_cache_line_size != s.read_cache_line_size ||
        m_settings.write_cache_line_size != s.write_cache_line_size ||
        m_settings.coalesce_writes != s.coalesce_writes || m_settings.coalesce_reads != s.coalesce_reads ||
        m_settings.max_queued_disk_bytes != s.max_queued_disk_bytes ||
        m_settings.max_queued_disk_bytes_low_watermark != s.max_queued_disk_bytes_low_watermark ||
        m_settings.disable_hash_checks != s.disable_hash_checks ||
        m_settings.explicit_read_cache != s.explicit_read_cache
#ifndef LIBED2K_DISABLE_MLOCK
        || m_settings.lock_disk_cache != s.lock_disk_cache
#endif
        || m_settings.use_read_cache != s.use_read_cache || m_settings.disk_io_write_mode != s.disk_io_write_mode ||
        m_settings.disk_io_read_mode != s.disk_io_read_mode ||
        m_settings.allow_reordered_disk_operations != s.allow_reordered_disk_operations ||
        m_settings.file_pool_size != s.file_pool_size || m_settings.volatile_read_cache != s.volatile_read_cache ||
        m_settings.no_atime_storage != s.no_atime_storage ||
        m_settings.ignore_resume_timestamps != s.ignore_resume_timestamps ||
        m_settings.no_recheck_incomplete_resume != s.no_recheck_incomplete_resume ||
        m_settings.low_prio_disk != s.low_prio_disk || m_settings.lock_files != s.lock_files)
        update_disk_io_thread = true;

    bool connections_limit_changed = m_settings.connections_limit != s.connections_limit;

    if (m_settings.alert_queue_size != s.alert_queue_size) m_alerts.set_alert_queue_size_limit(s.alert_queue_size);

    m_settings = s;

    if (m_settings.cache_buffer_chunk_size <= 0) m_settings.cache_buffer_chunk_size = 1;

    update_rate_settings();

    if (connections_limit_changed) update_connections_limit();

    if (m_settings.connection_speed < 0) m_settings.connection_speed = 200;

    if (update_disk_io_thread) update_disk_thread_settings();
}

void session_impl::operator()() {
    // main session thread

    // eh_initializer();

    if (m_listen_interface.port() != 0) {
        boost::mutex::scoped_lock l(m_mutex);
        open_listen_port();
        // m_server_connection->start();
    }

    m_tpm.start();

    bool stop_loop = false;
    while (!stop_loop) {
        error_code ec;
        m_io_service.run(ec);
        if (ec) {
            ERR("session_impl::operator()" << ec.message());
            std::string err = ec.message();
        }
        m_io_service.reset();

        boost::mutex::scoped_lock l(m_mutex);
        stop_loop = m_abort;
    }

    boost::mutex::scoped_lock l(m_mutex);
    m_transfers.clear();
    m_active_transfers.clear();
}

void session_impl::open_listen_port() {
    // close the open listen sockets
    DBG("session_impl::open_listen_port()");
    m_listen_sockets.clear();

    // we should only open a single listen socket, that
    // binds to the given interface
    listen_socket_t s = setup_listener(m_listen_interface);

    if (s.sock) {
        m_listen_sockets.push_back(s);
        async_accept(s.sock);
    }

    error_code ec;
    m_udp_socket.bind(udp::endpoint(m_listen_interface.address(), m_listen_interface.port()), ec);
    if (ec) {
        ERR("Cannot bind to UDP interface " << print_endpoint(m_listen_interface) << ": " << ec.message());
        m_alerts.post_alert_should(listen_failed_alert(m_listen_interface, ec));
    } else {
        maybe_update_udp_mapping(0, m_listen_interface.port(), m_listen_interface.port());
        maybe_update_udp_mapping(1, m_listen_interface.port(), m_listen_interface.port());
    }

    m_udp_socket.set_option(type_of_service(m_settings.peer_tos), ec);
    DBG("SET_TOS[ udp_socket tos: " << m_settings.peer_tos << " e: " << ec.message() << " ]");
    ec.clear();
}

void session_impl::set_ip_filter(const ip_filter& f) {
    m_ip_filter = f;

    // Close connections whose endpoint is filtered
    // by the new ip-filter
    for (transfer_map::iterator i = m_transfers.begin(), end(m_transfers.end()); i != end; ++i)
        i->second->ip_filter_updated();
}

const ip_filter& session_impl::get_ip_filter() const { return m_ip_filter; }

bool session_impl::listen_on(int port, const char* net_interface) {
    DBG("listen_on(" << ((net_interface) ? net_interface : "null") << ":" << port);
    tcp::endpoint new_interface;

    if (net_interface && std::strlen(net_interface) > 0) {
        error_code ec;
        new_interface = tcp::endpoint(ip::address::from_string(net_interface, ec), port);

        if (ec) {
            ERR("session_impl::listen_on: " << net_interface << " failed with: " << ec.message());
            return false;
        }
    } else
        new_interface = tcp::endpoint(ip::address_v4::any(), port);

    // if the interface is the same and the socket is open
    // don't do anything
    if (new_interface == m_listen_interface && !m_listen_sockets.empty()) return true;

    m_listen_interface = new_interface;
    m_settings.listen_port = port;
    open_listen_port();
    return !m_listen_sockets.empty();
}

bool session_impl::is_listening() const { return !m_listen_sockets.empty(); }

boost::uint16_t session_impl::listen_port() const {
    if (m_listen_sockets.empty()) return 0;
    return m_listen_sockets.front().external_port;
}

boost::uint16_t session_impl::ssl_listen_port() const {
#ifdef LIBED2K_USE_OPENSSL
// if peer connections are set up to be received over a socks
// proxy, and it's the same one as we're using for the tracker
// just tell the tracker the socks5 port we're listening on
/*
 // TODO - check it needs
if (m_socks_listen_socket && m_socks_listen_socket->is_open()
    && m_proxy.hostname == m_proxy.hostname)
    return m_socks_listen_port;

// if not, don't tell the tracker anything if we're in anonymous
// mode. We don't want to leak our listen port since it can
// potentially identify us if it is leaked elsewere
if (m_settings.anonymous_mode) return 0;
if (m_listen_sockets.empty()) return 0;
for (std::list<listen_socket_t>::const_iterator i = m_listen_sockets.begin()
         , end(m_listen_sockets.end()); i != end; ++i)
{
    if (i->ssl) return i->external_port;
}
*/
#endif
    return 0;
}

void session_impl::update_disk_thread_settings() {
    disk_io_job j;
    j.buffer = (char*)new session_settings(m_settings);
    j.action = disk_io_job::update_settings;
    m_disk_thread.add_job(j);
}

void session_impl::async_accept(boost::shared_ptr<ip::tcp::acceptor> const& listener) {
    boost::shared_ptr<tcp::socket> c(new tcp::socket(m_io_service));
    listener->async_accept(
        *c, bind(&session_impl::on_accept_connection, this, c, boost::weak_ptr<tcp::acceptor>(listener), _1));
}

void session_impl::on_accept_connection(boost::shared_ptr<tcp::socket> const& s,
                                        boost::weak_ptr<ip::tcp::acceptor> listen_socket, error_code const& e) {
    boost::shared_ptr<tcp::acceptor> listener = listen_socket.lock();
    if (!listener) return;

    if (e == boost::asio::error::operation_aborted) {
        s->close();
        DBG("session_impl::on_accept_connection: abort operation");
        return;
    }

    if (m_abort) {
        DBG("session_impl::on_accept_connection: abort set");
        return;
    }

    error_code ec;
    if (e) {
        tcp::endpoint ep = listener->local_endpoint(ec);

        std::string msg = "error accepting connection on '" + libed2k::print_endpoint(ep) + "' " + e.message();
        DBG(msg);

#ifdef LIBED2K_WINDOWS
        // Windows sometimes generates this error. It seems to be
        // non-fatal and we have to do another async_accept.
        if (e.value() == ERROR_SEM_TIMEOUT) {
            async_accept(listener);
            return;
        }
#endif
#ifdef LIBED2K_BSD
        // Leopard sometimes generates an "invalid argument" error. It seems to be
        // non-fatal and we have to do another async_accept.
        if (e.value() == EINVAL) {
            async_accept(listener);
            return;
        }
#endif
        m_alerts.post_alert_should(listen_failed_alert(ep, e));
        return;
    }

    async_accept(listener);
    incoming_connection(s);
}

void session_impl::incoming_connection(boost::shared_ptr<tcp::socket> const& s) {
    if (m_paused) {
        DBG("INCOMING CONNECTION [ ignored, paused ]");
        return;
    }

    error_code ec;
    // we got a connection request!
    tcp::endpoint endp = s->remote_endpoint(ec);

    if (ec) {
        ERR(endp << " <== INCOMING CONNECTION FAILED, could not retrieve remote endpoint " << ec.message());
        return;
    }

    DBG("<== INCOMING CONNECTION " << endp);

    if (m_ip_filter.access(endp.address()) & ip_filter::blocked) {
        DBG("filtered blocked ip " << endp);
        m_alerts.post_alert_should(peer_blocked_alert(transfer_handle(), endp.address()));
        return;
    }

    // don't allow more connections than the max setting
    if (num_connections() >= max_connections()) {
        // TODO: fire alert here

        DBG("number of connections limit exceeded (conns: " << num_connections() << ", limit: " << max_connections()
                                                            << "), connection rejected");

        return;
    }

    setup_socket_buffers(*s);

    boost::intrusive_ptr<peer_connection> c(new peer_connection(*this, s, endp, NULL));

    if (!c->is_disconnecting()) {
        // store connection in map only for real peers
        if (m_server_connection->m_target.address() != endp.address()) {
            m_connections.insert(c);
        }

        c->start();
    }
}

void session_impl::on_port_map_log(char const* msg, int map_transport) {
    LIBED2K_ASSERT(map_transport >= 0 && map_transport <= 1);
// log message
#ifdef LIBED2K_UPNP_LOGGING
    char const* transport_names[] = {"NAT-PMP", "UPnP"};
    m_upnp_log << time_now_string() << " " << transport_names[map_transport] << ": " << msg;
#endif
    m_alerts.post_alert_should(portmap_log_alert(map_transport, msg));
}

void session_impl::on_port_mapping(int mapping, address const& ip, int port, error_code const& ec, int map_transport) {
    LIBED2K_ASSERT(map_transport >= 0 && map_transport <= 1);

    if (mapping == m_udp_mapping[map_transport] && port != 0) {
        m_alerts.post_alert_should(portmap_alert(mapping, port, map_transport));
        return;
    }

    if (mapping == m_tcp_mapping[map_transport] && port != 0) {
        // TODO: report the proper address of the router
        // if (ip != address()) set_external_address(ip, source_router, address());

        if (!m_listen_sockets.empty()) {
            m_listen_sockets.front().external_address = ip;
            m_listen_sockets.front().external_port = port;
        }
        m_alerts.post_alert_should(portmap_alert(mapping, port, map_transport));
        return;
    }

    if (ec) {
        m_alerts.post_alert_should(portmap_error_alert(mapping, map_transport, ec));
    } else {
        m_alerts.post_alert_should(portmap_alert(mapping, port, map_transport));
    }
}

void session_impl::on_receive_udp(error_code const& e, udp::endpoint const& ep, char const* buf, int len) {
    if (e) {
        if (e == asio::error::connection_refused || e == asio::error::connection_reset ||
            e == asio::error::connection_aborted
#ifdef WIN32
            || e == error_code(ERROR_HOST_UNREACHABLE, get_system_category()) ||
            e == error_code(ERROR_PORT_UNREACHABLE, get_system_category()) ||
            e == error_code(ERROR_CONNECTION_REFUSED, get_system_category()) ||
            e == error_code(ERROR_CONNECTION_ABORTED, get_system_category())
#endif
                ) {
        } else {
            ERR("UDP socket error: (" << e.value() << ") " << e.message());
        }

        // don't bubble up operation aborted errors to the user
        if (e != asio::error::operation_aborted) m_alerts.post_alert_should(udp_error_alert(ep, e));

        return;
    }

// now process only dht packets
#ifndef LIBED2K_DISABLE_DHT
    // this is probably a dht message
    if (m_dht) m_dht->on_receive(ep, buf, len);
#endif
}

void session_impl::on_receive_udp_hostname(error_code const& e, char const* hostname, char const* buf, int len) {}

void session_impl::maybe_update_udp_mapping(int nat, int local_port, int external_port) {
    int local, external, protocol;
    if (nat == 0 && m_natpmp.get()) {
        if (m_udp_mapping[nat] != -1) {
            if (m_natpmp->get_mapping(m_udp_mapping[nat], local, external, protocol)) {
                // we already have a mapping. If it's the same, don't do anything
                if (local == local_port && external == external_port && protocol == natpmp::udp) return;
            }
            m_natpmp->delete_mapping(m_udp_mapping[nat]);
        }
        m_udp_mapping[nat] = m_natpmp->add_mapping(natpmp::udp, local_port, external_port);
        return;
    } else if (nat == 1 && m_upnp.get()) {
        if (m_udp_mapping[nat] != -1) {
            if (m_upnp->get_mapping(m_udp_mapping[nat], local, external, protocol)) {
                // we already have a mapping. If it's the same, don't do anything
                if (local == local_port && external == external_port && protocol == natpmp::udp) return;
            }
            m_upnp->delete_mapping(m_udp_mapping[nat]);
        }
        m_udp_mapping[nat] = m_upnp->add_mapping(upnp::udp, local_port, external_port);
        return;
    }
}

boost::weak_ptr<transfer> session_impl::find_transfer(const md4_hash& hash) {
    transfer_map::iterator i = m_transfers.find(hash);

    if (i != m_transfers.end()) return i->second;

    return boost::weak_ptr<transfer>();
}

boost::weak_ptr<transfer> session_impl::find_transfer(const std::string& filename) {
    transfer_map::iterator itr = m_transfers.begin();

    while (itr != m_transfers.end()) {
        if (combine_path(itr->second->save_path(), itr->second->name()) == filename) {
            return itr->second;
        }

        ++itr;
    }

    return (boost::weak_ptr<transfer>());
}

boost::intrusive_ptr<peer_connection> session_impl::find_peer_connection(const net_identifier& np) const {
    connection_map::const_iterator itr = std::find_if(m_connections.begin(), m_connections.end(),
                                                      boost::bind(&peer_connection::has_network_point, _1, np));
    if (itr != m_connections.end()) {
        return *itr;
    }
    return boost::intrusive_ptr<peer_connection>();
}

boost::intrusive_ptr<peer_connection> session_impl::find_peer_connection(const md4_hash& hash) const {
    connection_map::const_iterator itr =
        std::find_if(m_connections.begin(), m_connections.end(), boost::bind(&peer_connection::has_hash, _1, hash));
    if (itr != m_connections.end()) {
        return *itr;
    }
    return boost::intrusive_ptr<peer_connection>();
}

transfer_handle session_impl::find_transfer_handle(const md4_hash& hash) {
    return transfer_handle(find_transfer(hash));
}

peer_connection_handle session_impl::find_peer_connection_handle(const net_identifier& np) {
    return peer_connection_handle(find_peer_connection(np), this);
}

peer_connection_handle session_impl::find_peer_connection_handle(const md4_hash& hash) {
    return peer_connection_handle(find_peer_connection(hash), this);
}

std::vector<transfer_handle> session_impl::get_transfers() {
    std::vector<transfer_handle> ret;

    for (session_impl::transfer_map::iterator i = m_transfers.begin(), end(m_transfers.end()); i != end; ++i) {
        transfer& t = *i->second;
        if (t.is_aborted()) continue;
        ret.push_back(t.handle());
    }

    return ret;
}

std::vector<transfer_handle> session_impl::get_active_transfers() {
    std::vector<transfer_handle> ret;

    for (session_impl::transfer_map::iterator i = m_active_transfers.begin(), end(m_active_transfers.end()); i != end;
         ++i) {
        transfer& t = *i->second;
        if (t.is_aborted()) continue;
        ret.push_back(t.handle());
    }

    return ret;
}

void session_impl::queue_check_transfer(boost::shared_ptr<transfer> const& t) {
    if (m_abort) return;
    LIBED2K_ASSERT(t->should_check_file());
    LIBED2K_ASSERT(t->state() != transfer_status::checking_files);
    if (m_queued_for_checking.empty())
        t->start_checking();
    else
        t->set_state(transfer_status::queued_for_checking);

    LIBED2K_ASSERT(std::find(m_queued_for_checking.begin(), m_queued_for_checking.end(), t) ==
                   m_queued_for_checking.end());
    m_queued_for_checking.push_back(t);
}

void session_impl::dequeue_check_transfer(boost::shared_ptr<transfer> const& t) {
    LIBED2K_ASSERT(t->state() == transfer_status::checking_files || t->state() == transfer_status::queued_for_checking);

    if (m_queued_for_checking.empty()) return;

    boost::shared_ptr<transfer> next_check = *m_queued_for_checking.begin();
    check_queue_t::iterator done = m_queued_for_checking.end();
    for (check_queue_t::iterator i = m_queued_for_checking.begin(), end(m_queued_for_checking.end()); i != end; ++i) {
        LIBED2K_ASSERT(*i == t || (*i)->should_check_file());
        if (*i == t) done = i;
        if (next_check == t || next_check->queue_position() > (*i)->queue_position()) next_check = *i;
    }
    // only start a new one if we removed the one that is checking
    LIBED2K_ASSERT(done != m_queued_for_checking.end());
    if (done == m_queued_for_checking.end()) return;

    if (next_check != t && t->state() == transfer_status::checking_files) next_check->start_checking();

    m_queued_for_checking.erase(done);
}

void session_impl::close_connection(const peer_connection* p, const error_code& ec) {
    assert(p->is_disconnecting());

    connection_map::iterator i = std::find_if(m_connections.begin(), m_connections.end(),
                                              boost::bind(&boost::intrusive_ptr<peer_connection>::get, _1) == p);
    if (i != m_connections.end()) m_connections.erase(i);
}

transfer_handle session_impl::add_transfer(add_transfer_params const& params, error_code& ec) {
    APP("add transfer: {hash: " << params.file_hash << ", path: " << convert_to_native(params.file_path)
                                << ", size: " << params.file_size << "}");

    if (is_aborted()) {
        ec = errors::session_closing;
        return transfer_handle();
    }

    // is the transfer already active?
    boost::shared_ptr<transfer> transfer_ptr = find_transfer(params.file_hash).lock();

    if (transfer_ptr) {
        if (!params.duplicate_is_error) {
            return transfer_handle(transfer_ptr);
        }

        ec = errors::duplicate_transfer;
        return transfer_handle();
    }

    transfer_ptr.reset(new transfer(*this, m_listen_interface, ++m_queue_pos, params));
    transfer_ptr->start();

    m_transfers.insert(std::make_pair(params.file_hash, transfer_ptr));

    transfer_handle handle(transfer_ptr);
    m_alerts.post_alert_should(added_transfer_alert(handle));

    return handle;
}

void session_impl::remove_transfer(const transfer_handle& h, int options) {
    boost::shared_ptr<transfer> tptr = h.m_transfer.lock();
    if (!tptr) return;

    remove_active_transfer(tptr);
    transfer_map::iterator i = m_transfers.find(tptr->hash());

    if (i != m_transfers.end()) {
        transfer& t = *i->second;
        md4_hash hash = t.hash();

        if (options & session::delete_files) t.delete_files();
        t.abort();

        // t.set_queue_position(-1);
        m_transfers.erase(i);

        m_alerts.post_alert_should(deleted_transfer_alert(hash));
    }
}

bool session_impl::add_active_transfer(const boost::shared_ptr<transfer>& t) {
    DBG("add active transfer:" << t->hash().toString());
    return m_active_transfers.insert(std::make_pair(t->hash(), t)).second;
}

bool session_impl::remove_active_transfer(const boost::shared_ptr<transfer>& t) {
    bool removed = false;
    transfer_map::iterator i = m_active_transfers.find(t->hash());
    if (i != m_active_transfers.end()) {
        remove_active_transfer(i);
        removed = true;
    }

    return removed;
}

void session_impl::remove_active_transfer(transfer_map::iterator i) {
    DBG("remove active transfer: " << i->second->hash().toString());
    if (i == m_next_connect_transfer) m_next_connect_transfer.inc();
    m_active_transfers.erase(i);
    m_next_connect_transfer.validate();
}

peer_connection_handle session_impl::add_peer_connection(net_identifier np, error_code& ec) {
    DBG("session_impl::add_peer_connection");

    if (is_aborted()) {
        ec = errors::session_closing;
        return peer_connection_handle();
    }

    boost::intrusive_ptr<peer_connection> ptr = find_peer_connection(np);

    // peer already connected
    if (ptr) {
        DBG("connection exists");
        // already exists
        return peer_connection_handle(ptr, this);
    }

    tcp::endpoint endp(boost::asio::ip::address::from_string(int2ipstr(np.m_nIP)), np.m_nPort);
    boost::shared_ptr<tcp::socket> sock(new tcp::socket(m_io_service));
    setup_socket_buffers(*sock);

    boost::intrusive_ptr<peer_connection> c(new peer_connection(*this, boost::weak_ptr<transfer>(), sock, endp, NULL));

    m_connections.insert(c);

    m_half_open.enqueue(boost::bind(&peer_connection::connect, c, _1), boost::bind(&peer_connection::on_timeout, c),
                        libed2k::seconds(m_settings.peer_connect_timeout));

    return (peer_connection_handle(c, this));
}

std::pair<char*, int> session_impl::allocate_send_buffer(int size) {
    int num_buffers = (size + send_buffer_size - 1) / send_buffer_size;

    boost::mutex::scoped_lock l(m_send_buffer_mutex);

    return std::make_pair((char*)m_send_buffers.ordered_malloc(num_buffers), num_buffers * send_buffer_size);
}

void session_impl::free_send_buffer(char* buf, int size) {
    int num_buffers = size / send_buffer_size;

    boost::mutex::scoped_lock l(m_send_buffer_mutex);
    m_send_buffers.ordered_free(buf, num_buffers);
}

char* session_impl::allocate_disk_buffer(char const* category) { return m_disk_thread.allocate_buffer(category); }

void session_impl::free_disk_buffer(char* buf) { m_disk_thread.free_buffer(buf); }

char* session_impl::allocate_z_buffer() { return (char*)m_z_buffers.ordered_malloc(); }

void session_impl::free_z_buffer(char* buf) { m_z_buffers.ordered_free(buf); }

std::string session_impl::send_buffer_usage() {
    int send_buffer_capacity = 0;
    int used_send_buffer = 0;
    for (connection_map::const_iterator i = m_connections.begin(), end(m_connections.end()); i != end; ++i) {
        send_buffer_capacity += (*i)->send_buffer_capacity();
        used_send_buffer += (*i)->send_buffer_size();
    }

    return (boost::format("{disk_queued: %1%, send_buf_size: %2%,"
                          " used_send_buf: %3%, send_buf_utilization: %4%}") %
            m_disk_thread.queue_buffer_size() % send_buffer_capacity % used_send_buffer %
            (used_send_buffer * 100.f / send_buffer_capacity))
        .str();
}

session_status session_impl::status() const {
    session_status s;

    s.num_peers = (int)m_connections.size();

    // s.total_redundant_bytes = m_total_redundant_bytes;
    // s.total_failed_bytes = m_total_failed_bytes;

    s.up_bandwidth_queue = m_upload_rate.queue_size();
    s.down_bandwidth_queue = m_download_rate.queue_size();

    s.up_bandwidth_bytes_queue = m_upload_rate.queued_bytes();
    s.down_bandwidth_bytes_queue = m_download_rate.queued_bytes();

    s.has_incoming_connections = false;

    // total
    s.download_rate = m_stat.download_rate();
    s.total_upload = m_stat.total_upload();
    s.upload_rate = m_stat.upload_rate();
    s.total_download = m_stat.total_download();

    // payload
    s.payload_download_rate = m_stat.transfer_rate(stat::download_payload);
    s.total_payload_download = m_stat.total_transfer(stat::download_payload);
    s.payload_upload_rate = m_stat.transfer_rate(stat::upload_payload);
    s.total_payload_upload = m_stat.total_transfer(stat::upload_payload);

    // IP-overhead
    s.ip_overhead_download_rate = m_stat.transfer_rate(stat::download_ip_protocol);
    s.total_ip_overhead_download = m_stat.total_transfer(stat::download_ip_protocol);
    s.ip_overhead_upload_rate = m_stat.transfer_rate(stat::upload_ip_protocol);
    s.total_ip_overhead_upload = m_stat.total_transfer(stat::upload_ip_protocol);

    // tracker
    s.tracker_download_rate = m_stat.transfer_rate(stat::download_tracker_protocol);
    s.total_tracker_download = m_stat.total_transfer(stat::download_tracker_protocol);
    s.tracker_upload_rate = m_stat.transfer_rate(stat::upload_tracker_protocol);
    s.total_tracker_upload = m_stat.total_transfer(stat::upload_tracker_protocol);

    return s;
}

const tcp::endpoint& session_impl::server() const { return m_server_connection->m_target; }

void session_impl::abort() {
    if (m_abort) return;
    DBG("*** ABORT CALLED ***");

    // abort the main thread
    session_impl_base::abort();
    error_code ec;
    m_timer.cancel(ec);

    // close the listen sockets
    for (std::list<listen_socket_t>::iterator i = m_listen_sockets.begin(), end(m_listen_sockets.end()); i != end;
         ++i) {
        DBG("session_impl::abort: close listen socket");
        i->sock->close(ec);
    }

    stop_upnp();
    stop_natpmp();
#ifndef LIBED2K_DISABLE_DHT
    stop_dht();
#endif

    DBG("aborting all transfers (" << m_transfers.size() << ")");
    // abort all transfers
    for (transfer_map::iterator i = m_transfers.begin(), end(m_transfers.end()); i != end; ++i) {
        transfer& t = *i->second;
        t.abort();
    }

    DBG("aborting all server requests");
    // m_server_connection.abort_all_requests();
    m_server_connection->stop(errors::session_closing);

    DBG("aborting all connections (" << m_connections.size() << ")");

    // closing all the connections needs to be done from a callback,
    // when the session mutex is not held
    m_io_service.post(boost::bind(&libed2k::connection_queue::close, &m_half_open));

    DBG("connection queue: " << m_half_open.size());
    DBG("without transfers connections size: " << m_connections.size());

    // abort all connections
    while (!m_connections.empty()) {
        (*m_connections.begin())->disconnect(errors::stopping_transfer);
    }

    DBG("connection queue: " << m_half_open.size());

    m_download_rate.close();
    m_upload_rate.close();

    // #error closing the udp socket here means that
    // the uTP connections cannot be closed gracefully
    m_udp_socket.close();

    m_disk_thread.abort();
}

void session_impl::pause() {
    if (m_paused) return;
    m_paused = true;
    for (transfer_map::iterator i = m_transfers.begin(), end(m_transfers.end()); i != end; ++i) {
        transfer& t = *i->second;
        t.do_pause();
    }
}

void session_impl::resume() {
    if (!m_paused) return;
    m_paused = false;
    for (transfer_map::iterator i = m_transfers.begin(), end(m_transfers.end()); i != end; ++i) {
        transfer& t = *i->second;
        t.do_resume();
    }
}

// this function is called from the disk-io thread
// when the disk queue is low enough to post new
// write jobs to it. It will go through all peer
// connections that are blocked on the disk and
// wake them up
void session_impl::on_disk_queue() {}

// used to cache the current time
// every 100 ms. This is cheaper
// than a system call and can be
// used where more accurate time
// is not necessary
extern ptime g_current_time;

initialize_timer::initialize_timer() { g_current_time = time_now_hires(); }

void session_impl::on_tick(error_code const& e) {
    boost::mutex::scoped_lock l(m_mutex);

    if (m_abort) return;

    if (e == boost::asio::error::operation_aborted) return;

    if (e) {
        ERR("*** TICK TIMER FAILED " << e.message());
        ::abort();
        return;
    }

    ptime now = time_now_hires();
    aux::g_current_time = now;

    error_code ec;
    m_timer.expires_from_now(milliseconds(m_settings.tick_interval), ec);
    m_timer.async_wait(bind(&session_impl::on_tick, this, _1));

    m_download_rate.update_quotas(now - m_last_tick);
    m_upload_rate.update_quotas(now - m_last_tick);

    m_last_tick = now;

    // only tick the following once per second
    if (!m_second_timer.expired(now)) return;

    int tick_interval_ms = total_milliseconds(m_second_timer.tick_interval());

    // --------------------------------------------------------------
    // check for incoming connections that might have timed out
    // --------------------------------------------------------------
    // TODO: should it be implemented?

    m_server_connection->second_tick(tick_interval_ms);
    update_active_transfers();

    // --------------------------------------------------------------
    // second_tick every active transfer
    // --------------------------------------------------------------

    int num_checking = 0;
    int num_queued = 0;
    for (transfer_map::iterator i = m_active_transfers.begin(), end(m_active_transfers.end()); i != end; ++i) {
        transfer& t = *i->second;
        LIBED2K_ASSERT(!t.is_aborted());
        if (t.state() == transfer_status::checking_files)
            ++num_checking;
        else if (t.state() == transfer_status::queued_for_checking && !t.is_paused())
            ++num_queued;
        t.second_tick(m_stat, tick_interval_ms, now);
    }

    // some people claim that there sometimes can be cases where
    // there is no transfers being checked, but there are transfers
    // waiting to be checked. I have never seen this, and I can't
    // see a way for it to happen. But, if it does, start one of
    // the queued transfers
    if (num_checking == 0 && num_queued > 0) {
        LIBED2K_ASSERT(false);
        check_queue_t::iterator i =
            std::min_element(m_queued_for_checking.begin(), m_queued_for_checking.end(),
                             boost::bind(&transfer::queue_position, _1) < boost::bind(&transfer::queue_position, _2));

        if (i != m_queued_for_checking.end()) {
            (*i)->start_checking();
        }
    }

    m_stat.second_tick(tick_interval_ms);

    connect_new_peers();

    // --------------------------------------------------------------
    // disconnect peers when we have too many
    // --------------------------------------------------------------
    // TODO: should it be implemented?
}

void session_impl::connect_new_peers() {
    // TODO:
    // this loop will "hand out" max(connection_speed, half_open.free_slots())
    // to the transfers, in a round robin fashion, so that every transfer is
    // equally likely to connect to a peer

    int free_slots = m_half_open.free_slots();
    if (!m_active_transfers.empty() && free_slots > -m_half_open.limit() &&
        num_connections() < m_settings.connections_limit && !m_abort) {
        // this is the maximum number of connections we will
        // attempt this tick
        int max_connections_per_second = 10;
        int steps_since_last_connect = 0;
        int num_active_transfers = int(m_active_transfers.size());
        m_next_connect_transfer.validate();

        for (;;) {
            transfer& t = *m_next_connect_transfer->second;
            if (t.want_more_peers()) {
                try {
                    if (t.try_connect_peer()) {
                        --max_connections_per_second;
                        --free_slots;
                        steps_since_last_connect = 0;
                    }
                } catch (std::bad_alloc&) {
                    // we ran out of memory trying to connect to a peer
                    // lower the global limit to the number of peers
                    // we already have
                    m_settings.connections_limit = num_connections();
                    if (m_settings.connections_limit < 2) m_settings.connections_limit = 2;
                }
            }

            ++m_next_connect_transfer;
            ++steps_since_last_connect;

            // if we have gone two whole loops without
            // handing out a single connection, break
            if (steps_since_last_connect > num_active_transfers * 2) break;
            // if there are no more free connection slots, abort
            if (free_slots <= -m_half_open.limit()) break;
            // if we should not make any more connections
            // attempts this tick, abort
            if (max_connections_per_second == 0) break;
            // maintain the global limit on number of connections
            if (num_connections() >= m_settings.connections_limit) break;
        }
    }
}

void session_impl::setup_socket_buffers(ip::tcp::socket& s) {
    error_code ec;
    if (m_settings.send_socket_buffer_size) {
        tcp::socket::send_buffer_size option(m_settings.send_socket_buffer_size);
        s.set_option(option, ec);
    }
    if (m_settings.recv_socket_buffer_size) {
        tcp::socket::receive_buffer_size option(m_settings.recv_socket_buffer_size);
        s.set_option(option, ec);
    }
}

session_impl::listen_socket_t session_impl::setup_listener(ip::tcp::endpoint ep, bool v6_only) {
    DBG("session_impl::setup_listener");
    error_code ec;
    listen_socket_t s;
    s.sock.reset(new tcp::acceptor(m_io_service));
    s.sock->open(ep.protocol(), ec);

    if (ec) {
        ERR("failed to open socket: " << libed2k::print_endpoint(ep) << ": " << ec.message().c_str());
    }

    s.sock->bind(ep, ec);

    if (ec) {
        // post alert
        ERR("cannot bind to interface " << libed2k::print_endpoint(ep).c_str() << " : " << ec.message().c_str());
        return listen_socket_t();
    }

    s.external_port = s.sock->local_endpoint(ec).port();
    s.sock->listen(5, ec);

    if (ec) {
        // post alert

        char msg[200];
        snprintf(msg, 200, "cannot listen on interface \"%s\": %s", libed2k::print_endpoint(ep).c_str(),
                 ec.message().c_str());
        ERR(msg);

        return listen_socket_t();
    }

    // post alert succeeded

    DBG("listening on: " << ep << " external port: " << s.external_port);

    return s;
}

void session_impl::post_search_request(search_request& ro) {
    m_server_connection->post_search_request(ro);
    BOOST_FOREACH (const slave_sc_vale& val, m_slave_sc) { val.second->post_search_request(ro); }
}

void session_impl::post_search_more_result_request() {
    m_server_connection->post_search_more_result_request();
    BOOST_FOREACH (const slave_sc_vale& val, m_slave_sc) { val.second->post_search_more_result_request(); }
}

void session_impl::post_cancel_search() {
    shared_files_list sl;
    m_server_connection->post_announce(sl);
    BOOST_FOREACH (const slave_sc_vale& val, m_slave_sc) { val.second->post_announce(sl); }
}

void session_impl::post_announce(shared_files_list& sl) {
    m_server_connection->post_announce(sl);
    BOOST_FOREACH (const slave_sc_vale& val, m_slave_sc) { val.second->post_announce(sl); }
}

void session_impl::post_sources_request(const md4_hash& hFile, boost::uint64_t nSize) {
    m_server_connection->post_sources_request(hFile, nSize);
    BOOST_FOREACH (const slave_sc_vale& val, m_slave_sc) { val.second->post_sources_request(hFile, nSize); }
}

void session_impl::update_connections_limit() {
    if (m_settings.connections_limit <= 0) {
        m_settings.connections_limit = (std::numeric_limits<int>::max)();
#if LIBED2K_USE_RLIMIT
        rlimit l;
        if (getrlimit(RLIMIT_NOFILE, &l) == 0 && l.rlim_cur != RLIM_INFINITY) {
            m_settings.connections_limit = l.rlim_cur - m_settings.file_pool_size;
            if (m_settings.connections_limit < 5) m_settings.connections_limit = 5;
        }
#endif
    }

    if (num_connections() > m_settings.connections_limit && !m_transfers.empty()) {
        // if we have more connections that we're allowed, disconnect
        // peers from the transfers so that they are all as even as possible

        int to_disconnect = num_connections() - m_settings.connections_limit;

        int last_average = 0;
        int average = m_settings.connections_limit / m_transfers.size();

        // the number of slots that are unused by transfers
        int extra = m_settings.connections_limit % m_transfers.size();

        // run 3 iterations of this, then we're probably close enough
        for (int iter = 0; iter < 4; ++iter) {
            // the number of transfers that are above average
            int num_above = 0;
            for (transfer_map::iterator i = m_transfers.begin(), end(m_transfers.end()); i != end; ++i) {
                int num = i->second->num_peers();
                if (num <= last_average) continue;
                if (num > average) ++num_above;
                if (num < average) extra += average - num;
            }

            // distribute extra among the transfers that are above average
            if (num_above == 0) num_above = 1;
            last_average = average;
            average += extra / num_above;
            if (extra == 0) break;
            // save the remainder for the next iteration
            extra = extra % num_above;
        }

        for (transfer_map::iterator i = m_transfers.begin(), end(m_transfers.end()); i != end; ++i) {
            int num = i->second->num_peers();
            if (num <= average) continue;

            // distribute the remainder
            int my_average = average;
            if (extra > 0) {
                ++my_average;
                --extra;
            }

            int disconnect = (std::min)(to_disconnect, num - my_average);
            to_disconnect -= disconnect;
            i->second->disconnect_peers(disconnect, error_code(errors::too_many_connections, get_libed2k_category()));
        }
    }
}

void session_impl::update_rate_settings() {
    if (m_settings.half_open_limit <= 0) m_settings.half_open_limit = (std::numeric_limits<int>::max)();
    m_half_open.limit(m_settings.half_open_limit);

    if (m_settings.download_rate_limit < 0) m_settings.download_rate_limit = 0;
    m_download_channel.throttle(m_settings.download_rate_limit);

    if (m_settings.upload_rate_limit < 0) m_settings.upload_rate_limit = 0;
    m_upload_channel.throttle(m_settings.upload_rate_limit);
}

void session_impl::update_active_transfers() {
    for (transfer_map::iterator i = m_active_transfers.begin(), end(m_active_transfers.end()); i != end;) {
        transfer& t = *i->second;
        if (!t.active() && t.last_active() > 20)
            remove_active_transfer(i++);
        else
            ++i;
    }
}

void session_impl::start_natpmp() {
    if (m_natpmp) return;

    // the natpmp constructor may fail and call the callbacks
    // into the session_impl.
    natpmp* n = new (std::nothrow) natpmp(m_io_service, m_listen_interface.address(),
                                          boost::bind(&session_impl::on_port_mapping, this, _1, _2, _3, _4, 0),
                                          boost::bind(&session_impl::on_port_map_log, this, _1, 0));
    if (n == 0) return;

    m_natpmp = n;

    if (m_listen_interface.port() > 0) {
        remap_tcp_ports(1, m_listen_interface.port(), ssl_listen_port());
    }
    if (m_udp_socket.is_open()) {
        m_udp_mapping[0] = m_natpmp->add_mapping(natpmp::udp, m_listen_interface.port(), m_listen_interface.port());
    }
}

void session_impl::start_upnp() {
    if (m_upnp) return;

    // the upnp constructor may fail and call the callbacks
    upnp* u = new (std::nothrow)
        upnp(m_io_service, m_half_open, m_listen_interface.address(), m_settings.user_agent_str,
             boost::bind(&session_impl::on_port_mapping, this, _1, _2, _3, _4, 1),
             boost::bind(&session_impl::on_port_map_log, this, _1, 1), m_settings.upnp_ignore_nonrouters);

    if (u == 0) return;

    m_upnp = u;

    m_upnp->discover_device();
    if (m_listen_interface.port() > 0 || ssl_listen_port() > 0) {
        remap_tcp_ports(2, m_listen_interface.port(), ssl_listen_port());
    }
    if (m_udp_socket.is_open()) {
        m_udp_mapping[1] = m_upnp->add_mapping(upnp::udp, m_listen_interface.port(), m_listen_interface.port());
    }
}

void session_impl::stop_natpmp() {
    if (m_natpmp) {
        m_natpmp->close();
        m_udp_mapping[0] = -1;
        m_tcp_mapping[0] = -1;
#ifdef LIBED2K_USE_OPENSSL
        m_ssl_tcp_mapping[0] = -1;
        m_ssl_udp_mapping[0] = -1;
#endif
    }
    m_natpmp.reset();
}

void session_impl::stop_upnp() {
    if (m_upnp) {
        m_upnp->close();
        m_udp_mapping[1] = -1;
        m_tcp_mapping[1] = -1;
#ifdef LIBED2K_USE_OPENSSL
        m_ssl_tcp_mapping[1] = -1;
        m_ssl_udp_mapping[1] = -1;
#endif
    }
    m_upnp.reset();
}

int session_impl::add_port_mapping(int t, int external_port, int local_port) {
    int ret = 0;
    if (m_upnp) ret = m_upnp->add_mapping((upnp::protocol_type)t, external_port, local_port);
    if (m_natpmp) ret = m_natpmp->add_mapping((natpmp::protocol_type)t, external_port, local_port);
    return ret;
}

void session_impl::delete_port_mapping(int handle) {
    if (m_upnp) m_upnp->delete_mapping(handle);
    if (m_natpmp) m_natpmp->delete_mapping(handle);
}

void session_impl::remap_tcp_ports(boost::uint32_t mask, int tcp_port, int ssl_port) {
    if ((mask & 1) && m_natpmp) {
        if (m_tcp_mapping[0] != -1) m_natpmp->delete_mapping(m_tcp_mapping[0]);
        m_tcp_mapping[0] = m_natpmp->add_mapping(natpmp::tcp, tcp_port, tcp_port);
#ifdef LIBED2K_USE_OPENSSL
        if (m_ssl_mapping[0] != -1) m_natpmp->delete_mapping(m_ssl_mapping[0]);
        m_ssl_mapping[0] = m_natpmp->add_mapping(natpmp::tcp, ssl_port, ssl_port);
#endif
    }
    if ((mask & 2) && m_upnp) {
        if (m_tcp_mapping[1] != -1) m_upnp->delete_mapping(m_tcp_mapping[1]);
        m_tcp_mapping[1] = m_upnp->add_mapping(upnp::tcp, tcp_port, tcp_port);
#ifdef LIBED2K_USE_OPENSSL
        if (m_ssl_mapping[1] != -1) m_upnp->delete_mapping(m_ssl_mapping[1]);
        m_ssl_mapping[1] = m_upnp->add_mapping(upnp::tcp, ssl_port, ssl_port);
#endif
    }
}

bool session_impl::external_ip_t::add_vote(md4_hash const& k, int type) {
    sources |= type;
    if (voters.find(k)) return false;
    voters.set(k);
    ++num_votes;
    return true;
}

void session_impl::set_external_address(address const& ip, int source_type, address const& source) {
    if (is_any(ip)) return;
    if (is_local(ip)) return;
    if (is_loopback(ip)) return;

#if defined LIBED2K_VERBOSE_LOGGING
    (*m_logger) << time_now_string() << ": set_external_address(" << print_address(ip) << ", " << source_type << ", "
                << print_address(source) << ")\n";
#endif
    // this is the key to use for the bloom filters
    // it represents the identity of the voter
    md4_hash k;
    hash_address(source, k);

    // do we already have an entry for this external IP?
    std::vector<external_ip_t>::iterator i = std::find_if(m_external_addresses.begin(), m_external_addresses.end(),
                                                          boost::bind(&external_ip_t::addr, _1) == ip);

    if (i == m_external_addresses.end()) {
        // each IP only gets to add a new IP once
        if (m_external_address_voters.find(k)) return;

        if (m_external_addresses.size() > 20) {
            if (random() < UINT_MAX / 2) {
#if defined LIBED2K_VERBOSE_LOGGING
                (*m_logger) << time_now_string() << ": More than 20 slots, dopped\n";
#endif
                return;
            }
            // use stable sort here to maintain the fifo-order
            // of the entries with the same number of votes
            // this will sort in ascending order, i.e. the lowest
            // votes first. Also, the oldest are first, so this
            // is a sort of weighted LRU.
            std::stable_sort(m_external_addresses.begin(), m_external_addresses.end());
// erase the first element, since this is the
// oldest entry and the one with lowst number
// of votes. This makes sense because the oldest
// entry has had the longest time to receive more
// votes to be bumped up
#if defined LIBED2K_VERBOSE_LOGGING
            (*m_logger) << "  More than 20 slots, dopping " << print_address(m_external_addresses.front().addr) << " ("
                        << m_external_addresses.front().num_votes << ")\n";
#endif
            m_external_addresses.erase(m_external_addresses.begin());
        }
        m_external_addresses.push_back(external_ip_t());
        i = m_external_addresses.end() - 1;
        i->addr = ip;
    }
    // add one more vote to this external IP
    if (!i->add_vote(k, source_type)) return;

    i = std::max_element(m_external_addresses.begin(), m_external_addresses.end());
    LIBED2K_ASSERT(i != m_external_addresses.end());

#if defined LIBED2K_VERBOSE_LOGGING
    for (std::vector<external_ip_t>::iterator j = m_external_addresses.begin(), end(m_external_addresses.end());
         j != end; ++j) {
        (*m_logger) << ((j == i) ? "-->" : "   ") << print_address(j->addr) << " votes: " << j->num_votes << "\n";
    }
#endif
    if (i->addr == m_external_address) return;

#if defined LIBED2K_VERBOSE_LOGGING
    (*m_logger) << "  external IP updated\n";
#endif
    m_external_address = i->addr;
    m_external_address_voters.clear();

    m_alerts.post_alert_should(external_ip_alert(ip));

// since we have a new external IP now, we need to
// restart the DHT with a new node ID
#ifndef LIBED2K_DISABLE_DHT
    if (m_dht) {
        entry s = m_dht->state();
        int cur_state = 0;
        int prev_state = 0;
        entry* nodes1 = s.find_key("nodes");
        if (nodes1 && nodes1->type() == entry::list_t) cur_state = nodes1->list().size();
        entry* nodes2 = m_dht_state.find_key("nodes");
        if (nodes2 && nodes2->type() == entry::list_t) prev_state = nodes2->list().size();
        if (cur_state > prev_state) m_dht_state = s;
        start_dht(m_dht_state);
    }
#endif
}

#ifndef LIBED2K_DISABLE_DHT

void session_impl::start_dht() { start_dht(m_dht_state); }

void session_impl::start_dht(entry const& startup_state) {
    if (m_dht) {
        m_dht->stop();
        m_dht = 0;
    }
    m_dht = new dht::dht_tracker(*this, m_udp_socket, m_dht_settings, &startup_state);

    for (std::list<udp::endpoint>::iterator i = m_dht_router_nodes.begin(), end(m_dht_router_nodes.end()); i != end;
         ++i) {
        m_dht->add_router_node(*i);
    }

    m_dht->start(startup_state);
    m_alerts.post_alert_should(dht_started());

    // announce all torrents we have to the DHT
    // TODO - should be implemented
    /*for (torrent_map::const_iterator i = m_torrents.begin()
        , end(m_torrents.end()); i != end; ++i)
    {
        i->second->dht_announce();
    }
    */
}

void session_impl::stop_dht() {
    if (!m_dht) return;
    m_dht->stop();
    m_dht = 0;
    m_alerts.post_alert_should(dht_stopped());
}

void session_impl::set_dht_settings(dht_settings const& settings) { m_dht_settings = settings; }

entry session_impl::dht_state() const {
    if (!m_dht) return entry();
    return m_dht->state();
}

kad_state session_impl::dht_estate() const {
    if (!m_dht) return kad_state();
    return m_dht->estate();
}

void session_impl::add_dht_node_name(std::pair<std::string, int> const& node) {
    if (m_dht) m_dht->add_node(node);
}

void session_impl::add_dht_node(std::pair<std::string, int> const& node, const std::string& id) {
    if (m_dht) {
        kad_id h = md4_hash::fromString(id);
        error_code ec;
        ip::address addr = ip::address::from_string(node.first, ec);
        if (!ec) {
            DBG("add node " << node.first << ":" << node.second << " with " << id);
            m_dht->add_node(udp::endpoint(addr, node.second), h);
        }
    }
}

void session_impl::add_dht_router(std::pair<std::string, int> const& node) {
#if defined LIBED2K_ASIO_DEBUGGING
    add_outstanding_async("session_impl::on_dht_router_name_lookup");
#endif
    char port[7];
    snprintf(port, sizeof(port), "%d", node.second);
    tcp::resolver::query q(node.first, port);
    m_host_resolver.async_resolve(q, boost::bind(&session_impl::on_dht_router_name_lookup, this, _1, _2));
}

void session_impl::on_dht_router_name_lookup(error_code const& e, tcp::resolver::iterator host) {
#if defined LIBED2K_ASIO_DEBUGGING
    complete_async("session_impl::on_dht_router_name_lookup");
#endif
    // TODO: report errors as alerts
    if (e) return;
    while (host != tcp::resolver::iterator()) {
        // router nodes should be added before the DHT is started (and bootstrapped)
        udp::endpoint ep(host->endpoint().address(), host->endpoint().port());
        if (m_dht) m_dht->add_router_node(ep);
        m_dht_router_nodes.push_back(ep);
        ++host;
    }
}

void session_impl::find_keyword(const std::string& keyword) {
    md4_hash target = hasher::from_string(keyword);
    if (m_active_dht_requests.find(target) == m_active_dht_requests.end()) {
        m_active_dht_requests.insert(target);
        if (m_dht)
            m_dht->search_keywords(target, listen_port(), boost::bind(&session_impl::on_traverse_completed, this, _1));
    } else {
        DBG("dht search keyword request before previous finished " << keyword << " hash " << target);
    }
}

void session_impl::find_sources(const md4_hash& hash, size_type size) {
    if (m_active_dht_requests.find(hash) == m_active_dht_requests.end()) {
        m_active_dht_requests.insert(hash);
        if (m_dht)
            m_dht->search_sources(hash, listen_port(), size,
                                  boost::bind(&session_impl::on_traverse_completed, this, _1));
    } else {
        DBG("dht search sources request before previous finished hash " << hash);
    }
}

void session_impl::on_traverse_completed(const kad_id& id) {
    DBG("traverse for " << id << " completed");
    size_t n = m_active_dht_requests.erase(id);
    LIBED2K_ASSERT(n == 1u);
    m_alerts.post_alert_should(dht_traverse_finished(id));
}

void session_impl::on_find_dht_source(const md4_hash& hash, uint8_t type, client_id_type ip, uint16_t port,
                                      client_id_type low_id) {
    DBG("dht found peer " << hash << " type " << type << " ip " << int2ipstr(ip) << " port " << port << " low id "
                          << low_id);

    if (ip != 0) {
        boost::shared_ptr<transfer> transfer_ptr = find_transfer(hash).lock();
        if (transfer_ptr) {
            tcp::endpoint peer(ip::address::from_string(int2ipstr(ip)), port);
            transfer_ptr->add_peer(peer, peer_info::dht);
            DBG("peer added to transfer");
        }
    }
}

void session_impl::on_find_dht_keyword(const md4_hash& h, const std::deque<kad_info_entry>& kk) {
    m_alerts.post_alert_should(dht_keyword_search_result_alert(h, kk));
}

#endif
}
}
