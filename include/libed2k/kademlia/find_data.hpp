/*

Copyright (c) 2006, Arvid Norberg & Daniel Wallin
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

#ifndef FIND_DATA_050323_HPP
#define FIND_DATA_050323_HPP

#include <vector>
#include <map>

#include <libed2k/kademlia/traversal_algorithm.hpp>
#include <libed2k/kademlia/node_id.hpp>
#include <libed2k/kademlia/routing_table.hpp>
#include <libed2k/kademlia/rpc_manager.hpp>
#include <libed2k/kademlia/observer.hpp>
#include <libed2k/kademlia/msg.hpp>

#include <boost/optional.hpp>
#include <boost/function/function1.hpp>
#include <boost/function/function2.hpp>

namespace libed2k {
namespace dht {

typedef std::vector<char> packet_t;

class rpc_manager;
class node_impl;

// -------- find data -----------

// TODO: rename this to find_peers
class find_data : public traversal_algorithm {
   public:
    typedef boost::function<void(kad_id const&)> data_callback;
    typedef boost::function<void(std::vector<std::pair<node_entry, std::string> > const&, bool)> nodes_callback;

    find_data(node_impl& node, node_id target, data_callback const& dcallback, nodes_callback const& ncallback,
              uint8_t search_type);

    virtual char const* name() const { return "get_peers"; }

    node_id const target() const { return m_target; }

   protected:
    void done();
    observer_ptr new_observer(void* ptr, udp::endpoint const& ep, node_id const& id);
    virtual bool invoke(observer_ptr o);

   private:
    data_callback m_data_callback;
    nodes_callback m_nodes_callback;
    node_id const m_target;
    node_id const m_id;
    bool m_done : 1;
    bool m_got_peers : 1;
    uint8_t m_search_type;
};

class find_data_observer : public observer {
   public:
    find_data_observer(boost::intrusive_ptr<traversal_algorithm> const& algorithm, udp::endpoint const& ep,
                       node_id const& id)
        : observer(algorithm, ep, id) {}

    // this is called when a reply is received
    virtual void reply(const kad2_pong&, udp::endpoint ep);
    virtual void reply(const kad2_hello_res&, udp::endpoint ep);
    virtual void reply(const kad2_bootstrap_res&, udp::endpoint ep);
    virtual void reply(const kademlia2_res&, udp::endpoint ep);
};
}
}  // namespace libed2k::dht

#endif  // FIND_DATA_050323_HPP
