#ifndef __LIBED2K_SESSION_SETTINGS__
#define __LIBED2K_SESSION_SETTINGS__

#include <limits>
#include <libed2k/hasher.hpp>
#include <libed2k/constants.hpp>

namespace libed2k {
struct proxy_settings {
    proxy_settings() : port(0), type(none), proxy_hostnames(true), proxy_peer_connections(true) {}

    std::string hostname;
    int port;

    std::string username;
    std::string password;

    enum proxy_type {
        // a plain tcp socket is used, and
        // the other settings are ignored.
        none,
        // socks4 server, requires username.
        socks4,
        // the hostname and port settings are
        // used to connect to the proxy. No
        // username or password is sent.
        socks5,
        // the hostname and port are used to
        // connect to the proxy. the username
        // and password are used to authenticate
        // with the proxy server.
        socks5_pw,
        // the http proxy is only available for
        // tracker and web seed traffic
        // assumes anonymous access to proxy
        http,
        // http proxy with basic authentication
        // uses username and password
        http_pw,
        // route through a i2p SAM proxy
        i2p_proxy
    };

    proxy_type type;

    // when set to true, hostname are resolved
    // through the proxy (if supported)
    bool proxy_hostnames;

    // if true, use this proxy for peers too
    bool proxy_peer_connections;
};

class session_settings {
   public:
    typedef std::vector<std::pair<std::string, bool> > fd_list;

    session_settings()
        : peer_timeout(120),
          peer_connect_timeout(7),
          block_request_timeout(10),
          max_failcount(3),
          min_reconnect_time(60),
          connection_speed(6),
          allow_multiple_connections_per_ip(false),
          recv_socket_buffer_size(0),
          send_socket_buffer_size(0),
          send_buffer_watermark(3 * BLOCK_SIZE),
          listen_port(4662),
          client_name("libed2k"),
          mod_name("libed2k"),
          max_peerlist_size(4000),
          max_paused_peerlist_size(4000),
          tick_interval(100),
          download_rate_limit(-1),
          upload_rate_limit(-1),
          unchoke_slots_limit(8),
          half_open_limit(0),
          connections_limit(200),
          enable_outgoing_utp(true),
          enable_incoming_utp(true),
          utp_target_delay(100)  // milliseconds
          ,
          utp_gain_factor(1500)  // bytes per rtt
          ,
          utp_min_timeout(500)  // milliseconds
          ,
          utp_syn_resends(2),
          utp_fin_resends(2),
          utp_num_resends(6),
          utp_connect_timeout(3000)  // milliseconds
          ,
          utp_delayed_ack(0)  // milliseconds
          ,
          utp_dynamic_sock_buf(false)  // this doesn't seem quite reliable yet
          ,
          utp_loss_multiplier(50)  // specified in percent
          ,
          mixed_mode_algorithm(peer_proportional),
          rate_limit_utp(true),
          m_version(0x3c),
          mod_major(0),
          mod_minor(0),
          mod_build(0),
          m_max_announces_per_call(198),
          m_show_shared_catalogs(true),
          m_show_shared_files(true),
          user_agent(md4_hash::emulex),
          user_agent_str(md4_hash::emulex.toString()),
          ignore_resume_timestamps(false),
          no_recheck_incomplete_resume(false),
          seeding_outgoing_connections(false),
          alert_queue_size(1000)
          // Disk IO settings
          ,
          file_pool_size(40),
          max_queued_disk_bytes(16 * 1024 * 1024),
          max_queued_disk_bytes_low_watermark(0),
          cache_size((16 * 1024 * 1024) / BLOCK_SIZE),
          cache_buffer_chunk_size((16 * 16 * 1024) / BLOCK_SIZE),
          cache_expiry(5 * 60),
          use_read_cache(true),
          explicit_read_cache(false),
          disk_io_write_mode(0),
          disk_io_read_mode(0),
          coalesce_reads(false),
          coalesce_writes(false),
          optimize_hashing_for_speed(true),
          file_checks_delay_per_block(0),
          disk_cache_algorithm(avoid_readback),
          read_cache_line_size((32 * 16 * 1024) / BLOCK_SIZE),
          write_cache_line_size((32 * 16 * 1024) / BLOCK_SIZE),
          optimistic_disk_retry(10 * 60),
          disable_hash_checks(false),
          allow_reordered_disk_operations(true)
#ifndef LIBED2K_DISABLE_MLOCK
          ,
          lock_disk_cache(false)
#endif
          ,
          volatile_read_cache(false),
          default_cache_min_age(1),
          no_atime_storage(true),
          read_job_every(10),
          use_disk_read_ahead(true),
          lock_files(false),
          low_prio_disk(true),
          peer_tos(0),
          upnp_ignore_nonrouters(false) {
    }

    // the number of seconds to wait for any activity on
    // the peer wire before closing the connection due
    // to time out.
    int peer_timeout;

    // this is the timeout for a connection attempt. If
    // the connect does not succeed within this time, the
    // connection is dropped. The time is specified in seconds.
    int peer_connect_timeout;

    // the number of seconds to wait for block request.
    int block_request_timeout;

    // the number of times we can fail to connect to a peer
    // before we stop retrying it.
    int max_failcount;

    // the number of seconds to wait to reconnect to a peer.
    // this time is multiplied with the failcount.
    int min_reconnect_time;

    // the number of connection attempts that
    // are made per second.
    int connection_speed;

    // false to not allow multiple connections from the same
    // IP address. true will allow it.
    bool allow_multiple_connections_per_ip;

    // sets the socket send and receive buffer sizes
    // 0 means OS default
    int recv_socket_buffer_size;
    int send_socket_buffer_size;

    // if the send buffer has fewer bytes than this, we'll
    // read another 16kB block onto it. If set too small,
    // upload rate capacity will suffer. If set too high,
    // memory will be wasted.
    // The actual watermark may be lower than this in case
    // the upload rate is low, this is the upper limit.
    int send_buffer_watermark;

    // ed2k peer port for incoming peer connections
    int listen_port;
    // ed2k client name
    std::string client_name;
    // ed2k mod program name
    std::string mod_name;

    // the max number of peers in the peer list
    // per transfer. This is the peers we know
    // about, not necessarily connected to.
    int max_peerlist_size;

    // when a torrent is paused, this is the max peer
    // list size that's used
    int max_paused_peerlist_size;

    // the number of milliseconds between internal ticks. Should be no
    // more than one second (i.e. 1000).
    int tick_interval;

    /**
      * session rate limits
      * -1 unlimits
     */
    int download_rate_limit;
    int upload_rate_limit;

    // the max number of unchoke slots in the session (might be
    // overridden by unchoke algorithm)
    int unchoke_slots_limit;

    // the max number of half-open TCP connections
    int half_open_limit;

    // the max number of connections in the session
    int connections_limit;

    // when set to true, libtorrent will try to make outgoing utp connections
    bool enable_outgoing_utp;

    // if set to false, libtorrent will reject incoming utp connections
    bool enable_incoming_utp;

    // target delay, milliseconds
    int utp_target_delay;

    // max number of bytes to increase cwnd per rtt in uTP
    // congestion controller
    int utp_gain_factor;

    // the shortest allowed uTP connection timeout in milliseconds
    // defaults to 500 milliseconds. The shorter timeout, the
    // faster the connection recovers from a loss of an entire window
    int utp_min_timeout;

    // the number of SYN packets that are sent before giving up
    int utp_syn_resends;

    // the number of resent packets sent on a closed socket before giving up
    int utp_fin_resends;

    // the number of times to send a packet before giving up
    int utp_num_resends;

    // initial timeout for uTP SYN packets
    int utp_connect_timeout;

    // number of milliseconds of delaying ACKing packets the most
    int utp_delayed_ack;

    // set to true if the uTP socket buffer size is allowed to increase
    // dynamically based on the NIC MTU setting. This is true by default
    // and improves uTP performance for networks with larger frame sizes
    // including loopback
    bool utp_dynamic_sock_buf;

    // what to multiply the congestion window by on packet loss.
    // it's specified as a percent. The default is 50, i.e. cut
    // in half
    int utp_loss_multiplier;

    enum bandwidth_mixed_algo_t {
        // disables the mixed mode bandwidth balancing
        prefer_tcp = 0,

        // does not throttle uTP, throttles TCP to the same proportion
        // of throughput as there are TCP connections
        peer_proportional = 1

    };
    // the algorithm to use to balance bandwidth between tcp
    // connections and uTP connections
    int mixed_mode_algorithm;

    // set to true if uTP connections should be rate limited
    // defaults to false
    bool rate_limit_utp;

    unsigned short m_version;
    unsigned short mod_major;
    unsigned short mod_minor;
    unsigned short mod_build;
    unsigned short m_max_announces_per_call;

    bool m_show_shared_catalogs;  //!< show shared catalogs to client
    bool m_show_shared_files;     //!< show shared files to client
    md4_hash user_agent;          //!< ed2k client hash - user agent information
    std::string user_agent_str;

    //!< known.met file
    std::string m_known_file;

    //!< users files and directories
    //!< second parameter true for recursive search and false otherwise
    fd_list m_fd_list;

    /**
      * root directory for auto-creating collections
      * collection will create when we share some folder
     */
    std::string m_collections_directory;

    // when set to true, the file modification time is ignored when loading
    // resume data. The resume data includes the expected timestamp of each
    // file and is typically compared to make sure the files haven't changed
    // since the last session
    bool ignore_resume_timestamps;

    // normally, if a resume file is incomplete (typically there's no
    // "file sizes" field) the transfer is queued for a full check. If
    // this settings is set to true, instead libed2k will assume
    // we have none of the files and go straight to download
    bool no_recheck_incomplete_resume;

    // this controls whether or not seeding (and complete) transfers
    // attempt to make outgoing connections or not.
    bool seeding_outgoing_connections;

    // the max alert queue size
    int alert_queue_size;

    /********************
     * Disk IO settings *
     ********************/

    // sets the upper limit on the total number of files this
    // session will keep open. The reason why files are
    // left open at all is that some anti virus software
    // hooks on every file close, and scans the file for
    // viruses. deferring the closing of the files will
    // be the difference between a usable system and
    // a completely hogged down system. Most operating
    // systems also has a limit on the total number of
    // file descriptors a process may have open. It is
    // usually a good idea to find this limit and set the
    // number of connections and the number of files
    // limits so their sum is slightly below it.
    int file_pool_size;

    // the maximum number of bytes a connection may have
    // pending in the disk write queue before its download
    // rate is being throttled. This prevents fast downloads
    // to slow medias to allocate more and more memory
    // indefinitely. This should be set to at least 16 kB
    // to not completely disrupt normal downloads. If it's
    // set to 0, you will be starving the disk thread and
    // nothing will be written to disk.
    // this is a per session setting.
    int max_queued_disk_bytes;

    // this is the low watermark for the disk buffer queue.
    // whenever the number of queued bytes exceed the
    // max_queued_disk_bytes, libed2k will wait for
    // it to drop below this value before issuing more
    // reads from the sockets. If set to 0, the
    // low watermark will be half of the max queued disk bytes
    int max_queued_disk_bytes_low_watermark;

    // the disk write cache, specified in BLOCK_SIZE blocks.
    // default is 16 MiB / BLOCK_SIZE. -1 means automatic, which
    // adjusts the cache size depending on the amount
    // of physical RAM in the machine.
    int cache_size;

    // this is the number of disk buffer blocks (BLOCK_SIZE)
    // that should be allocated at a time. It must be
    // at least 1. Lower number saves memory at the expense
    // of more heap allocations
    int cache_buffer_chunk_size;

    // the number of seconds a write cache entry sits
    // idle in the cache before it's forcefully flushed
    // to disk. Default is 5 minutes.
    int cache_expiry;

    // when true, the disk I/O thread uses the disk
    // cache for caching blocks read from disk too
    bool use_read_cache;

    // don't implicitly cache pieces in the read cache,
    // only cache pieces that are explicitly asked to be
    // cached.
    bool explicit_read_cache;

    enum io_buffer_mode_t { enable_os_cache = 0, disable_os_cache_for_aligned_files = 1, disable_os_cache = 2 };
    int disk_io_write_mode;
    int disk_io_read_mode;

    bool coalesce_reads;
    bool coalesce_writes;

    // if this is set to false, the hashing will be
    // optimized for memory usage instead of the
    // number of read operations
    bool optimize_hashing_for_speed;

    // if > 0, file checks will have a short
    // delay between disk operations, to make it
    // less intrusive on the system as a whole
    // blocking the disk. This delay is specified
    // in milliseconds and the delay will be this
    // long per BLOCK_SIZE block
    // the default of 10 ms/16kiB will limit
    // the checking rate to 1.6 MiB per second
    int file_checks_delay_per_block;

    enum disk_cache_algo_t { lru, largest_contiguous, avoid_readback };

    disk_cache_algo_t disk_cache_algorithm;

    // the number of blocks that will be read ahead
    // when reading a block into the read cache
    int read_cache_line_size;

    // whenever a contiguous range of this many
    // blocks is found in the write cache, it
    // is flushed immediately
    int write_cache_line_size;

    // this is the number of seconds a disk failure
    // occurs until libtorrent will re-try.
    int optimistic_disk_retry;

    // when set to true, all data downloaded from
    // peers will be assumed to be correct, and not
    // tested to match the hashes in the torrent
    // this is only useful for simulation and
    // testing purposes (typically combined with
    // disabled_storage)
    bool disable_hash_checks;

    // if this is true, disk read operations may
    // be re-ordered based on their physical disk
    // read offset. This greatly improves throughput
    // when uploading to many peers. This assumes
    // a traditional hard drive with a read head
    // and spinning platters. If your storage medium
    // is a solid state drive, this optimization
    // doesn't give you an benefits
    bool allow_reordered_disk_operations;

#ifndef LIBED2K_DISABLE_MLOCK
    // if this is set to true, the memory allocated for the
    // disk cache will be locked in physical RAM, never to
    // be swapped out
    bool lock_disk_cache;
#endif

    // if this is set to true, any block read from the
    // disk cache will be dropped from the cache immediately
    // following. This may be useful if the block is not
    // expected to be hit again. It would save some memory
    bool volatile_read_cache;

    // this is the default minimum time any read cache line
    // is kept in the cache.
    int default_cache_min_age;

    // if set to true, files won't have their atime updated
    // on disk reads. This works on linux
    bool no_atime_storage;

    // to avoid write jobs starving read jobs, if this many
    // write jobs have been taking priority in a row, service
    // one read job
    int read_job_every;

    // issue posix_fadvise() or fcntl(F_RDADVISE) for disk reads
    // ahead of time
    bool use_disk_read_ahead;

    // if set to true, files will be locked when opened.
    // preventing any other process from modifying them
    bool lock_files;

    // if this is set to true, the disk I/O will be
    // run at lower-than-normal priority. This is
    // intended to make the machine more responsive
    // to foreground tasks, while bittorrent runs
    // in the background
    bool low_prio_disk;

    // the TOS byte of all peer traffic (including
    // web seeds) is set to this value. The default
    // is the QBSS scavenger service
    // http://qbone.internet2.edu/qbss/
    // For unmarked packets, set to 0
    char peer_tos;

    // when this is true, the upnp port mapper will ignore
    // any upnp devices that don't have an address that matches
    // our currently configured router.
    bool upnp_ignore_nonrouters;
};

#ifndef LIBED2K_DISABLE_DHT
struct dht_settings {
    dht_settings()
        : max_peers_reply(100),
          search_branching(5)
#ifndef LIBED2K_NO_DEPRECATE
          ,
          service_port(0)
#endif
          ,
          max_fail_count(20),
          max_torrents(2000),
          max_dht_items(700),
          max_torrent_search_reply(20),
          restrict_routing_ips(true),
          restrict_search_ips(true) {
    }

    // the maximum number of peers to send in a
    // reply to get_peers
    int max_peers_reply;

    // the number of simultanous "connections" when
    // searching the DHT.
    int search_branching;

#ifndef LIBED2K_NO_DEPRECATE
    // the listen port for the dht. This is a UDP port.
    // zero means use the same as the tcp interface
    int service_port;
#endif

    // the maximum number of times a node can fail
    // in a row before it is removed from the table.
    int max_fail_count;

    // this is the max number of torrents the DHT will track
    int max_torrents;

    // max number of items the DHT will store
    int max_dht_items;

    // the max number of torrents to return in a
    // torrent search query to the DHT
    int max_torrent_search_reply;

    // when set, nodes whose IP address that's in
    // the same /24 (or /64 for IPv6) range in the
    // same routing table bucket. This is an attempt
    // to mitigate node ID spoofing attacks
    // also restrict any IP to only have a single
    // entry in the whole routing table
    bool restrict_routing_ips;

    // applies the same IP restrictions on nodes
    // received during a DHT search (traversal algorithm)
    bool restrict_search_ips;
};
#endif

#ifndef LIBED2K_DISABLE_ENCRYPTION

struct pe_settings {
    pe_settings() : out_enc_policy(enabled), in_enc_policy(enabled), allowed_enc_level(both), prefer_rc4(false) {}

    enum enc_policy {
        forced,   // disallow non encrypted connections
        enabled,  // allow encrypted and non encrypted connections
        disabled  // disallow encrypted connections
    };

    enum enc_level {
        plaintext = 1,  // use only plaintext encryption
        rc4 = 2,        // use only rc4 encryption
        both = 3        // allow both
    };

    enc_policy out_enc_policy;
    enc_policy in_enc_policy;

    enc_level allowed_enc_level;
    // if the allowed encryption level is both, setting this to
    // true will prefer rc4 if both methods are offered, plaintext
    // otherwise
    bool prefer_rc4;
};
#endif
}

#endif
