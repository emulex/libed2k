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

#ifndef LIBED2K_FILE_POOL_HPP
#define LIBED2K_FILE_POOL_HPP

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/intrusive_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <map>
#include <libed2k/filesystem.hpp>
#include <libed2k/time.hpp>
#include <libed2k/thread.hpp>
#include <libed2k/file_storage.hpp>

namespace libed2k {
struct LIBED2K_EXPORT file_pool : boost::noncopyable {
    file_pool(int size = 40);
    ~file_pool();

    boost::intrusive_ptr<file> open_file(void* st, std::string const& p, file_storage::iterator fe,
                                         file_storage const& fs, int m, error_code& ec);
    void release(void* st);
    void release(void* st, int file_index);
    void resize(int size);
    int size_limit() const { return m_size; }
    void set_low_prio_io(bool b) { m_low_prio_io = b; }

   private:
    void remove_oldest();

    int m_size;
    bool m_low_prio_io;

    struct lru_file_entry {
        lru_file_entry() : key(0), last_use(libed2k::time_now()), mode(0) {}
        mutable boost::intrusive_ptr<file> file_ptr;
        void* key;
        libed2k::ptime last_use;
        int mode;
    };

    // maps storage pointer, file index pairs to the
    // lru entry for the file
    typedef std::map<std::pair<void*, int>, lru_file_entry> file_set;

    file_set m_files;
    mutex m_mutex;

#if LIBED2K_CLOSE_MAY_BLOCK
    void closer_thread_fun();
    mutex m_closer_mutex;
    std::vector<boost::intrusive_ptr<file> > m_queued_for_close;
    bool m_stop_thread;

    // used to close files
    thread m_closer_thread;
#endif
};
}

#endif
