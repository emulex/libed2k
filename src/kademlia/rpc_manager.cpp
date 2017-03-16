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

#include "libed2k/pch.hpp"
#include "libed2k/socket.hpp"

// TODO: it would be nice to not have this dependency here
#include "libed2k/session_impl.hpp"

#include <boost/bind.hpp>

#include "libed2k/invariant_check.hpp"
#include <libed2k/io.hpp>
#include <libed2k/kademlia/node_id.hpp>  // for generate_random_id
#include <libed2k/kademlia/rpc_manager.hpp>
#include <libed2k/kademlia/logging.hpp>
#include <libed2k/kademlia/routing_table.hpp>
#include <libed2k/kademlia/find_data.hpp>
#include <libed2k/kademlia/refresh.hpp>
#include <libed2k/kademlia/node.hpp>
#include <libed2k/kademlia/observer.hpp>
#include <libed2k/hasher.hpp>
#include <libed2k/time.hpp>
#include "libed2k/util.hpp"
#include <time.h>  // time()

#ifdef LIBED2K_DHT_VERBOSE_LOGGING
#include <fstream>
#endif

namespace libed2k {
namespace dht {

namespace io = libed2k::detail;

#ifdef LIBED2K_DHT_VERBOSE_LOGGING
LIBED2K_DEFINE_LOG(rpc)
#endif

void intrusive_ptr_add_ref(observer const* o) {
    LIBED2K_ASSERT(o != 0);
    LIBED2K_ASSERT(o->m_refs >= 0);
    ++o->m_refs;
}

void intrusive_ptr_release(observer const* o) {
    LIBED2K_ASSERT(o != 0);
    LIBED2K_ASSERT(o->m_refs > 0);
    if (--o->m_refs == 0) {
        boost::intrusive_ptr<traversal_algorithm> ta = o->m_algorithm;
        (const_cast<observer*>(o))->~observer();
        ta->free_observer(const_cast<observer*>(o));
    }
}

void observer::set_target(udp::endpoint const& ep) {
#ifdef LIBED2K_DHT_VERBOSE_LOGGING
    // use high resolution timers for logging
    m_sent = time_now_hires();
#else
    m_sent = time_now();
#endif

    m_port = ep.port();
#if LIBED2K_USE_IPV6
    if (ep.address().is_v6()) {
        flags |= flag_ipv6_address;
        m_addr.v6 = ep.address().to_v6().to_bytes();
    } else
#endif
    {
        flags &= ~flag_ipv6_address;
        m_addr.v4 = ep.address().to_v4().to_bytes();
    }
}

address observer::target_addr() const {
#if LIBED2K_USE_IPV6
    if (flags & flag_ipv6_address)
        return address_v6(m_addr.v6);
    else
#endif
        return address_v4(m_addr.v4);
}

udp::endpoint observer::target_ep() const { return udp::endpoint(target_addr(), m_port); }

void observer::abort() {
    if (flags & flag_done) return;
    flags |= flag_done;
    m_algorithm->failed(observer_ptr(this), traversal_algorithm::prevent_request);
}

void observer::done() {
    if (flags & flag_done) return;
    flags |= flag_done;
    m_algorithm->finished(observer_ptr(this));
}

void observer::short_timeout() {
    if (flags & flag_short_timeout) return;
    m_algorithm->failed(observer_ptr(this), traversal_algorithm::short_timeout);
}

// this is called when no reply has been received within
// some timeout
void observer::timeout() {
    if (flags & flag_done) return;
    flags |= flag_done;
    m_algorithm->failed(observer_ptr(this));
}

enum { observer_size = max3<sizeof(find_data_observer), sizeof(announce_observer), sizeof(null_observer)>::value };

rpc_manager::rpc_manager(node_id const& our_id, routing_table& table, send_fun const& sf, void* userdata, uint16_t port)
    : m_pool_allocator(observer_size, 10),
      m_send(sf),
      m_userdata(userdata),
      m_our_id(our_id),
      m_table(table),
      m_timer(time_now()),
      m_random_number(generate_random_id()),
      m_allocated_observers(0),
      m_destructing(false),
      m_port(port) {
    std::srand(time(0));

#ifdef LIBED2K_DHT_VERBOSE_LOGGING
    LIBED2K_LOG(rpc) << "Constructing";

#define PRINT_OFFSETOF(x, y) LIBED2K_LOG(rpc) << "  +" << offsetof(x, y) << ": " #y

    LIBED2K_LOG(rpc) << " observer: " << sizeof(observer);
    PRINT_OFFSETOF(observer, m_sent);
    PRINT_OFFSETOF(observer, m_refs);
    PRINT_OFFSETOF(observer, m_algorithm);
    PRINT_OFFSETOF(observer, m_id);
    PRINT_OFFSETOF(observer, m_addr);
    PRINT_OFFSETOF(observer, m_port);
    PRINT_OFFSETOF(observer, m_transaction_id);
    PRINT_OFFSETOF(observer, flags);

    LIBED2K_LOG(rpc) << " announce_observer: " << sizeof(announce_observer);
    LIBED2K_LOG(rpc) << " null_observer: " << sizeof(null_observer);
    LIBED2K_LOG(rpc) << " find_data_observer: " << sizeof(find_data_observer);

#undef PRINT_OFFSETOF
#endif
}

rpc_manager::~rpc_manager() {
    LIBED2K_ASSERT(!m_destructing);
    m_destructing = true;
#ifdef LIBED2K_DHT_VERBOSE_LOGGING
    LIBED2K_LOG(rpc) << "Destructing";
#endif

    for (transactions_t::iterator i = m_transactions.begin(), end(m_transactions.end()); i != end; ++i) {
        (*i)->abort();
    }
}

void* rpc_manager::allocate_observer() {
    m_pool_allocator.set_next_size(10);
    void* ret = m_pool_allocator.malloc();
    if (ret) ++m_allocated_observers;
    return ret;
}

void rpc_manager::free_observer(void* ptr) {
    if (!ptr) return;
    --m_allocated_observers;
    m_pool_allocator.free(ptr);
}

#if defined LIBED2K_DEBUG || LIBED2K_RELEASE_ASSERTS
size_t rpc_manager::allocation_size() const { return observer_size; }
#endif
#ifdef LIBED2K_DEBUG
void rpc_manager::check_invariant() const {
    for (transactions_t::const_iterator i = m_transactions.begin(), end(m_transactions.end()); i != end; ++i) {
        LIBED2K_ASSERT(*i);
    }
}
#endif

void rpc_manager::unreachable(udp::endpoint const& ep) {
#ifdef LIBED2K_DHT_VERBOSE_LOGGING
    LIBED2K_LOG(rpc) << time_now_string() << " PORT_UNREACHABLE [ ip: " << ep << " ]";
#endif

    for (transactions_t::iterator i = m_transactions.begin(); i != m_transactions.end();) {
        LIBED2K_ASSERT(*i);
        observer_ptr const& o = *i;
        if (o->target_ep() != ep) {
            ++i;
            continue;
        }
        observer_ptr ptr = *i;
        m_transactions.erase(i++);
#ifdef LIBED2K_DHT_VERBOSE_LOGGING
        LIBED2K_LOG(rpc) << "  found transaction [ tid: " << ptr->transaction_id() << " ]";
#endif
        ptr->timeout();
        break;
    }
}

template <typename T>
bool rpc_manager::incoming(const T& t, udp::endpoint target, node_id* id) {
    LIBED2K_INVARIANT_CHECK;

    if (m_destructing) return false;

    observer_ptr o;

    for (transactions_t::iterator i = m_transactions.begin(), end(m_transactions.end()); i != end; ++i) {
        LIBED2K_ASSERT(*i);
        if ((*i)->transaction_id() != transaction_identifier<T>::id || (*i)->target_addr() != target.address())
            continue;
        kad_id packet_id = packet_kad_identifier(t);
        if (packet_id != (*i)->packet_id()) continue;
        o = *i;
        m_transactions.erase(i);
        break;
    }

    uint16_t i = transaction_identifier<T>::id;

    if (!o) {
#ifdef LIBED2K_DHT_VERBOSE_LOGGING
        LIBED2K_LOG(rpc) << "Reply with unknown transaction id: " << i << " from " << target;
#endif
        return false;
    }

#ifdef LIBED2K_DHT_VERBOSE_LOGGING
    std::ofstream reply_stats("round_trip_ms.log", std::ios::app);
    reply_stats << target.address() << "\t" << total_milliseconds(time_now_hires() - o->sent()) << std::endl;
#endif

#ifdef LIBED2K_DHT_VERBOSE_LOGGING
    LIBED2K_LOG(rpc) << "[" << o->m_algorithm.get() << "] Reply with transaction id: " << i << " from "
                     << target.address();
#endif

    o->reply(t, target);

    *id = extract_packet_node_id(t);

    // we have no node_id in packet - set it from observer
    if (*id == node_id::invalid) *id = o->id();

    // we found an observer for this reply, hence the node is not spoofing
    // add it to the routing table
    return m_table.node_seen(*id, target);
}

template bool rpc_manager::incoming<kad2_pong>(const kad2_pong& t, udp::endpoint target, node_id* id);
template bool rpc_manager::incoming<kad2_hello_res>(const kad2_hello_res& t, udp::endpoint target, node_id* id);
template bool rpc_manager::incoming<kad2_bootstrap_res>(const kad2_bootstrap_res& t, udp::endpoint target, node_id* id);
template bool rpc_manager::incoming<kademlia2_res>(const kademlia2_res& t, udp::endpoint target, node_id* id);

time_duration rpc_manager::tick() {
    LIBED2K_INVARIANT_CHECK;

    const static int short_timeout = 2;
    const static int timeout = 12;

    //	look for observers that have timed out

    if (m_transactions.empty()) return seconds(short_timeout);

    std::list<observer_ptr> timeouts;

    time_duration ret = seconds(short_timeout);
    ptime now = time_now();

#if defined LIBED2K_DEBUG || LIBED2K_RELEASE_ASSERTS
    ptime last = min_time();
    for (transactions_t::iterator i = m_transactions.begin(); i != m_transactions.end(); ++i) {
        LIBED2K_ASSERT((*i)->sent() >= last);
        last = (*i)->sent();
    }
#endif

    for (transactions_t::iterator i = m_transactions.begin(); i != m_transactions.end();) {
        observer_ptr o = *i;

        // if we reach an observer that hasn't timed out
        // break, because every observer after this one will
        // also not have timed out yet
        time_duration diff = now - o->sent();
        if (diff < seconds(timeout)) {
            ret = seconds(timeout) - diff;
            break;
        }

#ifdef LIBED2K_DHT_VERBOSE_LOGGING
        LIBED2K_LOG(rpc) << "[" << o->m_algorithm.get() << "] Timing out transaction id: " << (*i)->transaction_id()
                         << " from " << o->target_ep();
#endif
        m_transactions.erase(i++);
        timeouts.push_back(o);
    }

    std::for_each(timeouts.begin(), timeouts.end(), boost::bind(&observer::timeout, _1));
    timeouts.clear();

    for (transactions_t::iterator i = m_transactions.begin(); i != m_transactions.end(); ++i) {
        observer_ptr o = *i;

        // if we reach an observer that hasn't timed out
        // break, because every observer after this one will
        // also not have timed out yet
        time_duration diff = now - o->sent();
        if (diff < seconds(short_timeout)) {
            ret = seconds(short_timeout) - diff;
            break;
        }

        if (o->has_short_timeout()) continue;

        // TODO: don't call short_timeout() again if we've
        // already called it once
        timeouts.push_back(o);
    }

    std::for_each(timeouts.begin(), timeouts.end(), boost::bind(&observer::short_timeout, _1));

    return ret;
}

template <typename T>
bool rpc_manager::invoke(T& t, udp::endpoint target, observer_ptr o) {
    LIBED2K_INVARIANT_CHECK;
    if (m_destructing) return false;

    append_data(t);
    if (o) {
        o->set_target(target);
        o->set_transaction_id(transaction_identifier<T>::id);
    }
#ifdef LIBED2K_DHT_VERBOSE_LOGGING
    if (o) {
        LIBED2K_LOG(rpc) << "[" << o->m_algorithm.get() << "] invoking " << request_name(t) << " ==> " << target;
    } else {
        LIBED2K_LOG(rpc) << "["
                         << "] invoking " << request_name(t) << " ==> " << target;
    }
#endif

    udp_message msg = make_udp_message(t);

    if (m_send(m_userdata, msg, target, 1)) {
        if (o) m_transactions.push_back(o);
#if defined LIBED2K_DEBUG || LIBED2K_RELEASE_ASSERTS
        if (o) o->m_was_sent = true;
#endif
        return true;
    }

    return false;
}

template bool rpc_manager::invoke<kad2_ping>(kad2_ping& t, udp::endpoint target, observer_ptr o);
template bool rpc_manager::invoke<kad2_hello_req>(kad2_hello_req& t, udp::endpoint target, observer_ptr o);
template bool rpc_manager::invoke<kad2_bootstrap_req>(kad2_bootstrap_req& t, udp::endpoint target, observer_ptr o);
template bool rpc_manager::invoke<kademlia2_req>(kademlia2_req& t, udp::endpoint target, observer_ptr o);
template bool rpc_manager::invoke<kad2_search_key_req>(kad2_search_key_req&, udp::endpoint target, observer_ptr o);
template bool rpc_manager::invoke<kad2_search_notes_req>(kad2_search_notes_req&, udp::endpoint target, observer_ptr o);
template bool rpc_manager::invoke<kad2_search_sources_req>(kad2_search_sources_req&, udp::endpoint target,
                                                           observer_ptr o);

template <typename T>
void rpc_manager::append_data(T& t) const {
    // do nothing by default
}

template void rpc_manager::append_data<kad2_ping>(kad2_ping& t) const;

#ifdef LIBED2K_DHT_VERBOSE_LOGGING
template <typename T>
std::string rpc_manager::request_name(const T& t) const {
    return std::string();
}

template <>
std::string rpc_manager::request_name<kad2_ping>(const kad2_ping& t) const {
    return std::string("kad2_ping");
}

template <>
std::string rpc_manager::request_name<kad2_hello_req>(const kad2_hello_req& t) const {
    return std::string("kad2_hello_req");
}

template <>
std::string rpc_manager::request_name<kad2_bootstrap_req>(const kad2_bootstrap_req& t) const {
    return std::string("kad2_bootstrap_req");
}

#endif

observer::~observer() {
    // if the message was sent, it must have been
    // reported back to the traversal_algorithm as
    // well. If it wasn't sent, it cannot have been
    // reported back
    LIBED2K_ASSERT(m_was_sent == bool(flags & flag_done) || m_was_abandoned);
    LIBED2K_ASSERT(!m_in_constructor);
}
}
}  // namespace libed2k::dht
