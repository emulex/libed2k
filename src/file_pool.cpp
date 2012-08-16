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

#include <boost/version.hpp>
#include <boost/bind.hpp>

#include <libtorrent/pch.hpp>
#include <libed2k/assert.hpp>
#include <libed2k/file_pool.hpp>
#include <libed2k/error_code.hpp>
#include <libed2k/file_storage.hpp> // for file_entry

namespace libed2k
{

	file_pool::file_pool(int size)
		: m_size(size)
		, m_low_prio_io(true)
#if LIBED2K_CLOSE_MAY_BLOCK
		, m_stop_thread(false)
		, m_closer_thread(boost::bind(&file_pool::closer_thread_fun, this))
#endif
	{}

	file_pool::~file_pool()
	{
#if LIBED2K_CLOSE_MAY_BLOCK
		mutex::scoped_lock l(m_closer_mutex);
		m_stop_thread = true;
		l.unlock();
		// wait for hte closer thread to finish closing all files
		m_closer_thread.join();
#endif
	}

#if LIBED2K_CLOSE_MAY_BLOCK
	void file_pool::closer_thread_fun()
	{
		for (;;)
		{
			mutex::scoped_lock l(m_closer_mutex);
			if (m_stop_thread)
			{
				l.unlock();
				m_queued_for_close.clear();
				return;
			}

			// find a file that doesn't have any other threads referencing
			// it. Since only those files can be closed in this thead
			std::vector<boost::intrusive_ptr<file> >::iterator i = std::find_if(
				m_queued_for_close.begin(), m_queued_for_close.end()
				, boost::bind(&file::refcount, boost::bind(&boost::intrusive_ptr<file>::get, _1)) == 1);

			if (i == m_queued_for_close.end())
			{
				l.unlock();
				// none of the files are ready to be closet yet
				// because they're still in use by other threads
				// hold off for a while
				sleep(1000);
			}
			else
			{
				// ok, first pull the file out of the queue, release the mutex
				// (since closing the file may block) and then close it.
				boost::intrusive_ptr<file> f = *i;
				m_queued_for_close.erase(i);
				l.unlock();
				f->close();
			}
		}
	}
#endif

	boost::intrusive_ptr<file> file_pool::open_file(void* st, std::string const& p
		, file_storage::iterator fe, file_storage const& fs, int m, error_code& ec)
	{
		LIBED2K_ASSERT(st != 0);
		LIBED2K_ASSERT(is_complete(p));
		LIBED2K_ASSERT((m & file::rw_mask) == file::read_only
			|| (m & file::rw_mask) == file::read_write);
		mutex::scoped_lock l(m_mutex);
		file_set::iterator i = m_files.find(std::make_pair(st, fs.file_index(*fe)));
		if (i != m_files.end())
		{
			lru_file_entry& e = i->second;
			e.last_use = libtorrent::time_now();

			if (e.key != st && ((e.mode & file::rw_mask) != file::read_only
				|| (m & file::rw_mask) != file::read_only))
			{
				// this means that another instance of the storage
				// is using the exact same file.
#if BOOST_VERSION >= 103500
				ec = errors::file_collision;
#endif
				return boost::intrusive_ptr<file>();
			}

			e.key = st;
			// if we asked for a file in write mode,
			// and the cached file is is not opened in
			// write mode, re-open it
			if ((((e.mode & file::rw_mask) != file::read_write)
				&& ((m & file::rw_mask) == file::read_write))
				|| (e.mode & file::no_buffer) != (m & file::no_buffer)
				|| (e.mode & file::random_access) != (m & file::random_access))
			{
				// close the file before we open it with
				// the new read/write privilages
				LIBED2K_ASSERT(e.file_ptr->refcount() == 1);

#if LIBED2K_CLOSE_MAY_BLOCK
				mutex::scoped_lock l(m_closer_mutex);
				m_queued_for_close.push_back(e.file_ptr);
				l.unlock();
				e.file_ptr = new file;
#else
				e.file_ptr->close();
#endif
				std::string full_path = combine_path(p, fs.file_path(*fe));
				if (!e.file_ptr->open(full_path, m, ec))
				{
					m_files.erase(i);
					return boost::intrusive_ptr<file>();
				}
#ifdef LIBED2K_WINDOWS
// file prio is supported on vista and up
#if _WIN32_WINNT >= 0x0600
				if (m_low_prio_io)
				{
					// TODO: load this function dynamically from Kernel32.dll
					FILE_IO_PRIORITY_HINT_INFO priorityHint;
					priorityHint.PriorityHint = IoPriorityHintLow;
					SetFileInformationByHandle(e.file_ptr->native_handle(),
						FileIoPriorityHintInfo, &priorityHint, sizeof(priorityHint));
				}
#endif
#endif
				LIBED2K_ASSERT(e.file_ptr->is_open());
				e.mode = m;
			}
			LIBED2K_ASSERT((e.mode & file::no_buffer) == (m & file::no_buffer));
			return e.file_ptr;
		}
		// the file is not in our cache
		if ((int)m_files.size() >= m_size)
		{
			// the file cache is at its maximum size, close
			// the least recently used (lru) file from it
			remove_oldest();
		}
		lru_file_entry e;
		e.file_ptr.reset(new (std::nothrow)file);
		if (!e.file_ptr)
		{
			ec = error_code(ENOMEM, get_posix_category());
			return e.file_ptr;
		}
		std::string full_path = combine_path(p, fs.file_path(*fe));
		if (!e.file_ptr->open(full_path, m, ec))
			return boost::intrusive_ptr<file>();
		e.mode = m;
		e.key = st;
		m_files.insert(std::make_pair(std::make_pair(st, fs.file_index(*fe)), e));
		LIBED2K_ASSERT(e.file_ptr->is_open());
		return e.file_ptr;
	}

	void file_pool::remove_oldest()
	{
		file_set::iterator i = std::min_element(m_files.begin(), m_files.end()
			, boost::bind(&lru_file_entry::last_use, boost::bind(&file_set::value_type::second, _1))
				< boost::bind(&lru_file_entry::last_use, boost::bind(&file_set::value_type::second, _2)));
		if (i == m_files.end()) return;

#if LIBED2K_CLOSE_MAY_BLOCK
		mutex::scoped_lock l(m_closer_mutex);
		m_queued_for_close.push_back(i->second.file_ptr);
		l.unlock();
#endif
		m_files.erase(i);
	}

	void file_pool::release(void* st, int file_index)
	{
		mutex::scoped_lock l(m_mutex);
		file_set::iterator i = m_files.find(std::make_pair(st, file_index));
		if (i == m_files.end()) return;

#if LIBED2K_CLOSE_MAY_BLOCK
		mutex::scoped_lock l2(m_closer_mutex);
		m_queued_for_close.push_back(i->second.file_ptr);
		l2.unlock();
#endif
		m_files.erase(i);
	}

	// closes files belonging to the specified
	// storage. If 0 is passed, all files are closed
	void file_pool::release(void* st)
	{
		mutex::scoped_lock l(m_mutex);
		if (st == 0)
		{
			m_files.clear();
			return;
		}

		for (file_set::iterator i = m_files.begin();
			i != m_files.end();)
		{
			if (i->second.key == st)
				m_files.erase(i++);
			else
				++i;
		}
	}

	void file_pool::resize(int size)
	{
		LIBED2K_ASSERT(size > 0);
		if (size == m_size) return;
		mutex::scoped_lock l(m_mutex);
		m_size = size;
		if (int(m_files.size()) <= m_size) return;

		// close the least recently used files
		while (int(m_files.size()) > m_size)
			remove_oldest();
	}

}
