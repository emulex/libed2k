#ifndef __LIBED2K_ALERT_TYPES__
#define __LIBED2K_ALERT_TYPES__

// for print_endpoint
#include "libed2k/socket.hpp"
#include "libed2k/alert.hpp"
#include "libed2k/error_code.hpp"
#include "libed2k/packet_struct.hpp"
#include "libed2k/kademlia/kad_packet_struct.hpp"
#include "libed2k/transfer_handle.hpp"
#include "libed2k/socket_io.hpp"
#include "libed2k/entry.hpp"
#include "libed2k/add_transfer_params.hpp"
#include <boost/lexical_cast.hpp>

namespace libed2k {
struct server_alert : alert {
    const static int static_category = alert::status_notification | alert::server_notification;
    server_alert(const std::string& nm, const std::string& h, int p) : name(nm), host(h), port(p) {}
    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new server_alert(*this)); }

    virtual std::string message() const { return std::string("server alert"); }
    virtual char const* what() const { return "abstract server notification"; }
    std::string name;
    std::string host;
    int port;
};

struct server_name_resolved_alert : server_alert {
    server_name_resolved_alert(const std::string& name, const std::string& host, int port, const std::string endp)
        : server_alert(name, host, port), endpoint(endp) {}
    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new server_name_resolved_alert(*this)); }
    virtual std::string message() const { return std::string("server name was resolved"); }
    std::string endpoint;
};

/**
  * after server handshake completed
 */
struct server_connection_initialized_alert : server_alert {
    server_connection_initialized_alert(const std::string& name, const std::string& host, int port, boost::uint32_t cid,
                                        boost::uint32_t tcpf, boost::uint32_t auxp)
        : server_alert(name, host, port), client_id(cid), tcp_flags(tcpf), aux_port(auxp) {}

    virtual std::auto_ptr<alert> clone() const {
        return std::auto_ptr<alert>(new server_connection_initialized_alert(*this));
    }
    virtual std::string message() const { return std::string("handshake completed"); }

    boost::uint32_t client_id;
    boost::uint32_t tcp_flags;
    boost::uint32_t aux_port;
};

/**
  * emit on OP_SERVERSTATUS
 */
struct server_status_alert : server_alert {
    server_status_alert(const std::string& name, const std::string& host, int port, boost::uint32_t fcount,
                        boost::uint32_t ucount)
        : server_alert(name, host, port), files_count(fcount), users_count(ucount) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new server_status_alert(*this)); }
    virtual std::string message() const { return std::string("server status information"); }

    boost::uint32_t files_count;
    boost::uint32_t users_count;
};

/**
  * emit on OP_SERVERIDENT
 */

struct server_identity_alert : server_alert {
    server_identity_alert(const std::string& name, const std::string& host, int port, const md4_hash& shash,
                          const net_identifier& saddr, const std::string& sname, const std::string& sdescr)
        : server_alert(name, host, port),
          server_hash(shash),
          server_address(saddr),
          server_name(sname),
          server_descr(sdescr) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new server_identity_alert(*this)); }

    virtual std::string message() const { return std::string("server identity information"); }

    md4_hash server_hash;
    net_identifier server_address;
    std::string server_name;
    std::string server_descr;
};

/**
  * emit for every server message
 */
struct server_message_alert : server_alert {
    server_message_alert(const std::string& name, const std::string host, int port, const std::string& msg)
        : server_alert(name, host, port), server_message(msg) {}
    virtual std::string message() const { return server_message; }
    virtual char const* what() const { return "incoming server message"; }
    virtual std::auto_ptr<alert> clone() const { return (std::auto_ptr<alert>(new server_message_alert(*this))); }
    std::string server_message;
};

struct server_connection_closed : server_alert {
    server_connection_closed(const std::string& name, const std::string& host, int port, const error_code& error)
        : server_alert(name, host, port), m_error(error) {}
    virtual std::string message() const { return m_error.message(); }
    virtual char const* what() const { return "server connection closed"; }
    virtual std::auto_ptr<alert> clone() const { return (std::auto_ptr<alert>(new server_connection_closed(*this))); }
    error_code m_error;
};

struct listen_failed_alert : alert {
    listen_failed_alert(tcp::endpoint const& ep, error_code const& ec) : endpoint(ep), error(ec) {}

    tcp::endpoint endpoint;
    error_code error;

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new listen_failed_alert(*this)); }
    virtual char const* what() const { return "listen failed"; }
    const static int static_category = alert::status_notification | alert::error_notification;
    virtual int category() const { return static_category; }
    virtual std::string message() const {
        char ret[200];
        snprintf(ret, sizeof(ret), "mule listening on %s failed: %s", libed2k::print_endpoint(endpoint).c_str(),
                 error.message().c_str());
        return ret;
    }
};

struct peer_alert : alert {
    const static int static_category = alert::peer_notification;
    peer_alert(const net_identifier& np, const md4_hash& hash) : m_np(np), m_hash(hash) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new peer_alert(*this)); }

    virtual std::string message() const { return std::string("peer alert"); }
    virtual char const* what() const { return "peer alert"; }

    net_identifier m_np;
    md4_hash m_hash;
};

/**
  * this alert throws on server search results and on user shared files
 */
struct shared_files_alert : peer_alert {
    const static int static_category = alert::server_notification | alert::peer_notification;

    shared_files_alert(const net_identifier& np, const md4_hash& hash, const shared_files_list& files, bool more)
        : peer_alert(np, hash), m_files(files), m_more(more) {}
    virtual int category() const { return static_category; }

    virtual std::string message() const { return "search result from string"; }
    virtual char const* what() const { return "search result from server"; }

    virtual std::auto_ptr<alert> clone() const { return (std::auto_ptr<alert>(new shared_files_alert(*this))); }

    shared_files_list m_files;
    bool m_more;
};

struct shared_directories_alert : peer_alert {
    const static int static_category = alert::peer_notification;

    shared_directories_alert(const net_identifier& np, const md4_hash& hash,
                             const client_shared_directories_answer& dirs)
        : peer_alert(np, hash) {
        for (size_t n = 0; n < dirs.m_dirs.m_collection.size(); ++n) {
            m_dirs.push_back(dirs.m_dirs.m_collection[n].m_collection);
        }
    }

    virtual int category() const { return static_category; }

    virtual std::string message() const { return "search result from string"; }
    virtual char const* what() const { return "search result from server"; }

    virtual std::auto_ptr<alert> clone() const { return (std::auto_ptr<alert>(new shared_directories_alert(*this))); }

    std::vector<std::string> m_dirs;
};

/**
  * this alert throws on server search results and on user shared files
 */
struct shared_directory_files_alert : shared_files_alert {
    const static int static_category = alert::peer_notification;

    shared_directory_files_alert(const net_identifier& np, const md4_hash& hash, const std::string& strDirectory,
                                 const shared_files_list& files)
        : shared_files_alert(np, hash, files, false), m_strDirectory(strDirectory) {}

    virtual int category() const { return static_category; }

    virtual std::string message() const { return "search result for directory from peer"; }
    virtual char const* what() const { return "search result for directory from peer"; }

    virtual std::auto_ptr<alert> clone() const {
        return (std::auto_ptr<alert>(new shared_directory_files_alert(*this)));
    }

    std::string m_strDirectory;
};

struct ismod_shared_directory_files_alert : shared_files_alert {
    const static int static_category = alert::peer_notification;

    ismod_shared_directory_files_alert(const net_identifier& np, const md4_hash& hash, const md4_hash& dir_hash,
                                       const shared_files_list& files)
        : shared_files_alert(np, hash, files, false), m_dir_hash(dir_hash) {}

    virtual int category() const { return static_category; }

    virtual std::string message() const { return "search result for directory from peer"; }
    virtual char const* what() const { return "search result for directory from peer"; }

    virtual std::auto_ptr<alert> clone() const {
        return (std::auto_ptr<alert>(new ismod_shared_directory_files_alert(*this)));
    }

    md4_hash m_dir_hash;
};

struct peer_connected_alert : peer_alert {
    virtual int category() const { return static_category | alert::status_notification; }
    peer_connected_alert(const net_identifier& np, const md4_hash& hash, bool bActive)
        : peer_alert(np, hash), m_active(bActive) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new peer_connected_alert(*this)); }

    virtual std::string message() const { return std::string("peer connected alert"); }
    virtual char const* what() const { return "peer connected alert"; }
    bool m_active;
};

struct peer_disconnected_alert : public peer_alert {
    virtual int category() const { return static_category | alert::status_notification; }
    peer_disconnected_alert(const net_identifier& np, const md4_hash& hash, const error_code& ec)
        : peer_alert(np, hash), m_ec(ec) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new peer_disconnected_alert(*this)); }

    virtual std::string message() const { return std::string("peer disconnected alert"); }
    virtual char const* what() const { return "peer disconnected alert"; }
    error_code m_ec;
};

struct peer_message_alert : peer_alert {
    peer_message_alert(const net_identifier& np, const md4_hash& hash, const std::string& strMessage)
        : peer_alert(np, hash), m_strMessage(strMessage) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new peer_message_alert(*this)); }

    virtual std::string message() const { return std::string("peer message"); }
    virtual char const* what() const { return "peer notification"; }

    std::string m_strMessage;
};

struct peer_captcha_request_alert : peer_alert {
    peer_captcha_request_alert(const net_identifier& np, const md4_hash& hash,
                               const std::vector<unsigned char>& captcha)
        : peer_alert(np, hash), m_captcha(captcha) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new peer_captcha_request_alert(*this)); }

    virtual std::string message() const { return std::string("peer captcha request"); }
    virtual char const* what() const { return "peer captcha request"; }

    std::vector<unsigned char> m_captcha;
};

struct peer_captcha_result_alert : peer_alert {
    peer_captcha_result_alert(const net_identifier& np, const md4_hash& hash, boost::uint8_t nResult)
        : peer_alert(np, hash), m_nResult(nResult) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new peer_captcha_result_alert(*this)); }

    virtual std::string message() const { return std::string("peer captcha result"); }
    virtual char const* what() const { return "peer captcha result"; }

    boost::uint8_t m_nResult;
};

struct shared_files_access_denied : peer_alert {
    shared_files_access_denied(const net_identifier& np, const md4_hash& hash) : peer_alert(np, hash) {}
    virtual int category() const { return static_category; }

    virtual std::string message() const { return "shared files access denied"; }
    virtual char const* what() const { return "shared files access denied"; }

    virtual std::auto_ptr<alert> clone() const { return (std::auto_ptr<alert>(new shared_files_access_denied(*this))); }
};

struct added_transfer_alert : alert {
    const static int static_category = alert::status_notification;

    added_transfer_alert(const transfer_handle& h) : m_handle(h) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new added_transfer_alert(*this)); }

    virtual std::string message() const { return std::string("added transfer"); }
    virtual char const* what() const { return "added transfer"; }

    transfer_handle m_handle;
};

struct paused_transfer_alert : alert {
    const static int static_category = alert::status_notification;

    paused_transfer_alert(const transfer_handle& h) : m_handle(h) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new paused_transfer_alert(*this)); }

    virtual std::string message() const { return std::string("paused transfer"); }
    virtual char const* what() const { return "paused transfer"; }

    transfer_handle m_handle;
};

struct resumed_transfer_alert : alert {
    const static int static_category = alert::status_notification;

    resumed_transfer_alert(const transfer_handle& h) : m_handle(h) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new resumed_transfer_alert(*this)); }

    virtual std::string message() const { return std::string("resumed transfer"); }
    virtual char const* what() const { return "resumed transfer"; }

    transfer_handle m_handle;
};

struct deleted_transfer_alert : alert {
    const static int static_category = alert::status_notification;

    deleted_transfer_alert(const md4_hash& hash) : m_hash(hash) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new deleted_transfer_alert(*this)); }

    virtual std::string message() const { return std::string("deleted transfer"); }
    virtual char const* what() const { return "deleted transfer"; }

    md4_hash m_hash;
};

struct finished_transfer_alert : alert {
    const static int static_category = alert::status_notification;

    finished_transfer_alert(const transfer_handle& h, bool has_picker) : m_handle(h), m_had_picker(has_picker) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new finished_transfer_alert(*this)); }

    virtual std::string message() const { return std::string("transfer finished"); }
    virtual char const* what() const { return "transfer finished"; }

    transfer_handle m_handle;
    bool m_had_picker;
};

struct file_renamed_alert : alert {
    const static int static_category = alert::status_notification;

    file_renamed_alert(const transfer_handle& h, const std::string& name) : m_handle(h), m_name(name) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new file_renamed_alert(*this)); }

    virtual std::string message() const { return std::string("renamed file"); }
    virtual char const* what() const { return "renamed file"; }

    transfer_handle m_handle;
    std::string m_name;
};

struct file_rename_failed_alert : alert {
    const static int static_category = alert::status_notification;

    file_rename_failed_alert(const transfer_handle& h, const error_code& error) : m_handle(h), m_error(error) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new file_rename_failed_alert(*this)); }

    virtual std::string message() const { return std::string("rename failed transfer"); }
    virtual char const* what() const { return "rename failed transfer"; }

    transfer_handle m_handle;
    error_code m_error;
};

struct storage_moved_alert : alert {
    const static int static_category = alert::status_notification;

    storage_moved_alert(const transfer_handle& h, const std::string& path) : m_handle(h), m_path(path) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new storage_moved_alert(*this)); }

    virtual std::string message() const { return std::string("moved storage"); }
    virtual char const* what() const { return "moved storage"; }

    transfer_handle m_handle;
    std::string m_path;
};

struct storage_moved_failed_alert : alert {
    const static int static_category = alert::status_notification;

    storage_moved_failed_alert(const transfer_handle& h, const error_code& error) : m_handle(h), m_error(error) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new storage_moved_failed_alert(*this)); }

    virtual std::string message() const { return std::string("move storage failed"); }
    virtual char const* what() const { return "move storage failed"; }

    transfer_handle m_handle;
    error_code m_error;
};

struct deleted_file_alert : alert {
    const static int static_category = alert::status_notification;

    deleted_file_alert(const transfer_handle& h, const md4_hash& hash) : m_handle(h), m_hash(hash) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new deleted_file_alert(*this)); }

    virtual std::string message() const { return std::string("deleted file"); }
    virtual char const* what() const { return "deleted file"; }

    transfer_handle m_handle;
    md4_hash m_hash;
};

struct delete_failed_transfer_alert : alert {
    const static int static_category = alert::status_notification;

    delete_failed_transfer_alert(const transfer_handle& h, const error_code& error) : m_handle(h), m_error(error) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new delete_failed_transfer_alert(*this)); }

    virtual std::string message() const { return std::string("delete failed transfer"); }
    virtual char const* what() const { return "delete failed transfer"; }

    transfer_handle m_handle;
    error_code m_error;
};

struct state_changed_alert : alert {
    const static int static_category = alert::status_notification;

    state_changed_alert(const transfer_handle& h, transfer_status::state_t new_state,
                        transfer_status::state_t old_state)
        : m_handle(h), m_new_state(new_state), m_old_state(old_state) {}

    virtual int category() const { return static_category; }

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new state_changed_alert(*this)); }

    virtual std::string message() const { return std::string("changed transfer state"); }
    virtual char const* what() const { return "changed transfer state"; }

    transfer_handle m_handle;
    transfer_status::state_t m_new_state;
    transfer_status::state_t m_old_state;
};

struct transfer_alert : alert {
    transfer_alert(transfer_handle const& h) : m_handle(h) {}

    virtual std::string message() const { return m_handle.is_valid() ? m_handle.hash().toString() : " - "; }
    virtual char const* what() const { return "transfer alert"; }
    transfer_handle m_handle;
};

struct save_resume_data_alert : transfer_alert {
    save_resume_data_alert(boost::shared_ptr<entry> const& rd, transfer_handle const& h)
        : transfer_alert(h), resume_data(rd) {}

    boost::shared_ptr<entry> resume_data;

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new save_resume_data_alert(*this)); }
    virtual char const* what() const { return "save resume data complete"; }
    const static int static_category = alert::storage_notification;
    virtual int category() const { return static_category; }

    virtual std::string message() const { return transfer_alert::message() + " resume data generated"; }
};

struct save_resume_data_failed_alert : transfer_alert {
    save_resume_data_failed_alert(transfer_handle const& h, error_code const& e) : transfer_alert(h), error(e) {}

    error_code error;

    virtual std::auto_ptr<alert> clone() const {
        return std::auto_ptr<alert>(new save_resume_data_failed_alert(*this));
    }
    virtual char const* what() const { return "save resume data failed"; }
    const static int static_category = alert::storage_notification | alert::error_notification;
    virtual int category() const { return static_category; }
    virtual std::string message() const {
        return transfer_alert::message() + " resume data was not generated: " + error.message();
    }
};

struct LIBED2K_EXPORT fastresume_rejected_alert : transfer_alert {
    fastresume_rejected_alert(transfer_handle const& h, error_code const& e) : transfer_alert(h), error(e) {}

    error_code error;

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new fastresume_rejected_alert(*this)); }
    virtual char const* what() const { return "resume data rejected"; }
    const static int static_category = alert::status_notification | alert::error_notification;
    virtual int category() const { return static_category; }
    virtual std::string message() const {
        return transfer_alert::message() + " fast resume rejected: " + error.message();
    }
};

struct LIBED2K_EXPORT peer_blocked_alert : transfer_alert {
    peer_blocked_alert(transfer_handle const& h, address const& ip_) : transfer_alert(h), ip(ip_) {}

    address ip;

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new peer_blocked_alert(*this)); }
    virtual char const* what() const { return "blocked peer"; }
    const static int static_category = alert::status_notification;
    virtual int category() const { return static_category; }
    virtual std::string message() const {
        error_code ec;
        return transfer_alert::message() + ": blocked peer: " + ip.to_string(ec);
    }
};

struct LIBED2K_EXPORT file_error_alert : transfer_alert {
    file_error_alert(std::string const& f, transfer_handle const& h, error_code const& e)
        : transfer_alert(h), file(f), error(e) {}

    std::string file;
    error_code error;

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new file_error_alert(*this)); }
    virtual char const* what() const { return "file error"; }
    const static int static_category =
        alert::status_notification | alert::error_notification | alert::storage_notification;
    virtual int category() const { return static_category; }
    virtual std::string message() const {
        return transfer_alert::message() + " file (" + file + ") error: " + error.message();
    }
};

struct transfer_checked_alert : transfer_alert {
    transfer_checked_alert(transfer_handle const& h) : transfer_alert(h) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new transfer_checked_alert(*this)); }
    virtual char const* what() const { return "transfer checked"; }
    const static int static_category = alert::status_notification;
    virtual int category() const { return static_category; }
    virtual std::string message() const { return transfer_alert::message() + " checked"; }
};

struct hash_failed_alert : transfer_alert {
    hash_failed_alert(transfer_handle const& h, int failed_index) : transfer_alert(h), index(failed_index) {}

    int index;

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new hash_failed_alert(*this)); }
    virtual char const* what() const { return "piece check failed"; }
    const static int static_category = alert::status_notification;
    virtual int category() const { return static_category; }
    virtual std::string message() const { return transfer_alert::message() + " piece check failed"; }
};

struct transfer_params_alert : alert {
    const static int static_category = alert::status_notification;
    transfer_params_alert(const add_transfer_params& atp, const error_code& ec) : m_atp(atp), m_ec(ec) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new transfer_params_alert(*this)); }

    virtual char const* what() const { return "transfer parameters ready"; }
    virtual int category() const { return static_category; }
    virtual std::string message() const { return m_atp.file_path + " params ready"; }

    add_transfer_params m_atp;
    error_code m_ec;
};

struct portmap_log_alert : alert {
    portmap_log_alert(int t, std::string const& m) : map_type(t), msg(m) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new portmap_log_alert(*this)); }

    virtual char const* what() const { return "portmap log"; }

    const static int static_category = alert::port_mapping_notification;
    virtual int category() const { return static_category; }
    virtual std::string message() const {
        static char const* type_str[] = {"NAT-PMP", "UPnP"};
        char ret[600];
        snprintf(ret, sizeof(ret), "%s: %s", type_str[map_type], msg.c_str());
        return ret;
    }

    int map_type;
    std::string msg;
};

struct portmap_alert : alert {
    portmap_alert(int i, int port, int t) : mapping(i), external_port(port), map_type(t) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new portmap_alert(*this)); }

    virtual char const* what() const { return "portmap"; }

    const static int static_category = alert::port_mapping_notification;
    virtual int category() const { return static_category; }
    virtual std::string message() const {
        static char const* type_str[] = {"NAT-PMP", "UPnP"};
        char ret[200];
        snprintf(ret, sizeof(ret), "successfully mapped port using %s. external port: %u", type_str[map_type],
                 external_port);
        return ret;
    }

    int mapping;
    int external_port;
    int map_type;
};

struct portmap_error_alert : alert {
    portmap_error_alert(int i, int t, error_code const& e) : mapping(i), map_type(t), error(e) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new portmap_error_alert(*this)); }

    virtual char const* what() const { return "portmap error"; }
    virtual int category() const { return static_category; }
    const static int static_category = alert::port_mapping_notification | alert::error_notification;

    virtual std::string message() const {
        static char const* type_str[] = {"NAT-PMP", "UPnP"};
        return std::string("could not map port using ") + type_str[map_type] + ": " +
               convert_from_native(error.message());
    }

    int mapping;
    int map_type;
    error_code error;
};

struct udp_error_alert : alert {
    udp_error_alert(udp::endpoint const& ep, error_code const& ec) : endpoint(ep), error(ec) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new udp_error_alert(*this)); }

    virtual char const* what() const { return "UDP error"; }
    virtual int category() const { return static_category; }

    const static int static_category = alert::error_notification;
    virtual std::string message() const {
        error_code ec;
        return "UDP error: " + convert_from_native(error.message()) + " from: " + endpoint.address().to_string(ec);
    }

    udp::endpoint endpoint;
    error_code error;
};

struct dht_started : alert {
    dht_started() {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new dht_started(*this)); }

    virtual char const* what() const { return "DHT started"; }
    virtual int category() const { return static_category; }
    const static int static_category = alert::dht_notification;
    virtual std::string message() const { return "DHT started"; }
};

struct dht_stopped : alert {
    dht_stopped() {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new dht_stopped(*this)); }

    virtual char const* what() const { return "DHT stopped"; }
    virtual int category() const { return static_category; }
    const static int static_category = alert::dht_notification;
    virtual std::string message() const { return "DHT stopped"; }
};

struct dht_traverse_finished : alert {
    dht_traverse_finished(const md4_hash& h) : hash(h) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new dht_traverse_finished(*this)); }

    virtual char const* what() const { return "DHT traverse finished"; }
    virtual int category() const { return static_category; }
    const static int static_category = alert::dht_notification;
    virtual std::string message() const { return "DHT traverse finished"; }
    md4_hash hash;
};

struct dht_announce_alert : alert {
    dht_announce_alert(address const& ip_, int port_, md4_hash const& info_hash_)
        : ip(ip_), port(port_), info_hash(info_hash_) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new dht_announce_alert(*this)); }

    virtual char const* what() const { return "DHT announce"; }
    virtual int category() const { return static_category; }

    const static int static_category = alert::dht_notification;

    virtual std::string message() const {
        error_code ec;
        return "DHT announce: " + address_to_bytes(ip) + " port: " + boost::lexical_cast<std::string>(port);
    }

    address ip;
    int port;
    md4_hash info_hash;
};

struct dht_get_peers_alert : alert {
    dht_get_peers_alert(md4_hash const& info_hash_) : info_hash(info_hash_) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new dht_get_peers_alert(*this)); }

    virtual char const* what() const { return "DHT get peers"; }
    virtual int category() const { return static_category; }

    const static int static_category = alert::dht_notification;

    virtual std::string message() const {
        error_code ec;
        return "DHT get peers alert";
    }

    md4_hash info_hash;
};

struct external_ip_alert : alert {
    external_ip_alert(address const& ip) : external_address(ip) {}

    virtual std::auto_ptr<alert> clone() const { return std::auto_ptr<alert>(new external_ip_alert(*this)); }

    virtual char const* what() const { return "DHT get peers"; }
    virtual int category() const { return static_category; }

    const static int static_category = alert::status_notification;
    virtual std::string message() const {
        error_code ec;
        return "external IP received: " + external_address.to_string(ec);
    }

    address external_address;
};

struct dht_keyword_search_result_alert : alert {
    dht_keyword_search_result_alert(const md4_hash& h, const std::deque<kad_info_entry>& entries)
        : m_hash(h), m_entries(entries) {}

    virtual std::auto_ptr<alert> clone() const {
        return std::auto_ptr<alert>(new dht_keyword_search_result_alert(*this));
    }

    virtual char const* what() const { return "DHT search ketyword result"; }
    virtual int category() const { return static_category; }

    const static int static_category = alert::dht_notification;
    virtual std::string message() const { return "DHT search keyword result"; }

    md4_hash m_hash;
    std::deque<kad_info_entry> m_entries;
};
}

#endif  //__LIBED2K_ALERT_TYPES__
