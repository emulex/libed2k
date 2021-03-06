/*

Copyright (c) 2006, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef RPC_MANAGER_HPP
#define RPC_MANAGER_HPP

#include <vector>
#include <map>
#include <boost/cstdint.hpp>
#include <boost/pool/pool.hpp>
#include <boost/function/function3.hpp>

#include "libed2k/socket.hpp"
#include "libed2k/entry.hpp"
#include "libed2k/kademlia/node_id.hpp"
#include "libed2k/kademlia/logging.hpp"
#include "libed2k/kademlia/observer.hpp"
#include "libed2k/ptime.hpp"
#include "libed2k/packet_struct.hpp"

namespace libed2k {
namespace aux {
class session_impl;
}
}

namespace libed2k {
namespace dht {

#ifdef LIBED2K_DHT_VERBOSE_LOGGING
LIBED2K_DECLARE_LOG(rpc);
#endif

struct null_observer : public observer {
    null_observer(boost::intrusive_ptr<traversal_algorithm> const& a, udp::endpoint const& ep, node_id const& id)
        : observer(a, ep, id) {}

    virtual void reply(const kad2_pong&, udp::endpoint ep) { flags |= flag_done; }
    virtual void reply(const kad2_hello_res&, udp::endpoint ep) { flags |= flag_done; }
    virtual void reply(const kad2_bootstrap_res&, udp::endpoint ep) { flags |= flag_done; }
    virtual void reply(const kademlia2_res&, udp::endpoint ep) { flags |= flag_done; }
};

class routing_table;

class LIBED2K_EXTRA_EXPORT rpc_manager {
   public:
    typedef bool (*send_fun)(void* userdata, const udp_message&, udp::endpoint const&, int);

    rpc_manager(node_id const& our_id, routing_table& table, send_fun const& sf, void* userdata, uint16_t port);
    ~rpc_manager();

    void unreachable(udp::endpoint const& ep);

    // returns true if the node needs a refresh
    // if so, id is assigned the node id to refresh
    template <typename T>
    bool incoming(const T&, udp::endpoint target, node_id* id);

    // template <typename T>
    // node_id extract_packet_node_id(const T&);

    template <typename T>
    node_id extract_packet_node_id(const T&) {
        return node_id::invalid();
    }

    time_duration tick();

    /**
      * standard rpc invocation
    */
    template <typename T>
    bool invoke(T& t, udp::endpoint target, observer_ptr o);

    template <typename T>
    void append_data(T& t) const;

    /**
      * returns packet kad identifier for separate different transaction to the same endpoint and transaction id
      * currently uses only for pair of packets kademlia2_req <-> kademlia2_res
      * kademlia2_req target identifier writes to observer and id extractes from kademlia2_res for additional
     * verification
      * for all other packets return default equal kad id
    */
    template <typename T>
    kad_id packet_kad_identifier(const T& t) const {
        return kad_id();
    }

#ifdef LIBED2K_DHT_VERBOSE_LOGGING
    template <typename T>
    std::string request_name(const T& t) const;
#endif

#if defined LIBED2K_DEBUG || LIBED2K_RELEASE_ASSERTS
    size_t allocation_size() const;
#endif
#ifdef LIBED2K_DEBUG
    void check_invariant() const;
#endif

    void* allocate_observer();
    void free_observer(void* ptr);

    int num_allocated_observers() const { return m_allocated_observers; }

   private:
    mutable boost::pool<> m_pool_allocator;

    typedef std::list<observer_ptr> transactions_t;
    transactions_t m_transactions;

    send_fun m_send;
    void* m_userdata;
    node_id m_our_id;
    routing_table& m_table;
    ptime m_timer;
    node_id m_random_number;
    int m_allocated_observers;
    bool m_destructing;
    uint16_t m_port;
};
template <>
inline node_id rpc_manager::extract_packet_node_id<kad2_hello_res>(const kad2_hello_res& t) {
    return t.client_info.kid;
}

template <>
inline void rpc_manager::append_data<kad2_hello_req>(kad2_hello_req& t) const {
    t.client_info.kid = m_our_id;
    t.client_info.tcp_port = m_port;
    t.client_info.version = KADEMLIA_VERSION;
}

template <>
inline kad_id rpc_manager::packet_kad_identifier<kademlia2_res>(const kademlia2_res& t) const {
    return t.kid_target;
}

//
}
}  // namespace libed2k::dht

#endif
