/* This file is part of Toilet. Toilet is copyright 2007-2008 The Regents
 * of the University of California. It is distributed under the terms of
 * version 2 of the GNU GPL. See the file LICENSE for details. */

#define _ATFILE_SOURCE

#include <errno.h>
#include <assert.h>
#include <fcntl.h>

#include "openat.h"
#include "transaction.h"

#include "sys_journal.h"

/* Currently, sys_journal is built on top of transactions, which are built on
 * top of journal.c, which is built on top of Featherstitch patchgroups, which
 * may be implemented using a journal. There are too many journals here, but we
 * can optimize that later... */

/* We can fix this easily! Make the sys_journal file itself store merely the
 * size and sequence number of a secondary file, and use regular append
 * operations (inside a patchgroup) to add data to the secondary files. */

#define SYSJ_MAGIC 0xBAFE9BDA
#define SYSJ_VERSION 0

struct file_header
{
	uint32_t magic, version;
} __attribute__((packed));

struct entry_header
{
	sys_journal::listener_id id;
	size_t length;
} __attribute__((packed));

int sys_journal::append(journal_listener * listener, void * entry, size_t length)
{
	entry_header header;
	header.id = listener->id();
	assert(lookup_listener(header.id) == listener);
	assert(!discarded.count(header.id));
	if(length == (size_t) -1)
		return -EINVAL;
	header.length = length;
	if(tx_write(fd, &header, sizeof(header), offset) < 0)
		return -1;
	offset += sizeof(header);
	if(tx_write(fd, entry, length, offset) < 0)
	{
		/* need to unwrite the header */
		assert(0);
		return -1;
	}
	offset += length;
	return 0;
}

int sys_journal::discard(journal_listener * listener)
{
	entry_header header;
	header.id = listener->id();
	assert(lookup_listener(header.id) == listener);
	header.length = (size_t) -1;
	if(tx_write(fd, &header, sizeof(header), offset) < 0)
		return -1;
	offset += sizeof(header);
	discarded.insert(header.id);
	return 0;
}

int sys_journal::get_entries(journal_listener * listener)
{
	return playback(listener);
}

int sys_journal::digest(int dfd, const char * file)
{
	entry_header entry;
	int r, out, ufd = tx_read_fd(fd);
	assert(ufd >= 0);
	out = openat(dfd, file, O_WRONLY | O_TRUNC | O_CREAT, 0644);
	if(out < 0)
		return out;
	r = lseek(ufd, sizeof(file_header), SEEK_SET);
	if(r < 0)
		goto fail;
	while((r = read(ufd, &entry, sizeof(entry))) == sizeof(entry))
	{
		void * data;
		if(entry.length == (size_t) -1)
			continue;
		if(discarded.count(entry.id))
		{
			/* skip this entry, it's been discarded */
			lseek(ufd, entry.length, SEEK_CUR);
			continue;
		}
		data = malloc(entry.length);
		if(!data)
		{
			r = -ENOMEM;
			goto fail;
		}
		if(read(ufd, data, entry.length) != (ssize_t) entry.length)
		{
			free(data);
			r = -EIO;
			goto fail;
		}
		r = write(out, &entry, sizeof(entry));
		if(r != sizeof(entry))
		{
			free(data);
			goto fail;
		}
		r = write(out, data, entry.length);
		free(data);
		if(r != (int) entry.length)
			goto fail;
	}
	if(r)
		goto fail;
	close(out);
	lseek(ufd, 0, SEEK_END);
	return 0;
	
fail:
	close(out);
	unlinkat(dfd, file, 0);
	lseek(ufd, 0, SEEK_END);
	return (r < 0) ? r : -1;
}

int sys_journal::init(int dfd, const char * file, bool create, bool fail_missing)
{
	bool do_playback = false;
	if(fd >= 0)
		deinit();
	offset = 0;
	fd = tx_open(dfd, file, O_RDWR);
	if(fd < 0)
	{
		if(create && (fd == -ENOENT || errno == ENOENT))
		{
			int r;
			file_header header;
			/* XXX due to O_CREAT not being part of the transaction, we might get
			 * an empty file as a result of this, which later will cause an error
			 * below instead of here... we should handle that case somewhere */
			fd = tx_open(dfd, file, O_RDWR | O_CREAT, 0644);
			if(fd < 0)
				return (int) fd;
			header.magic = SYSJ_MAGIC;
			header.version = SYSJ_VERSION;
			r = tx_write(fd, &header, sizeof(header), 0);
			if(r < 0)
			{
				tx_close(fd);
				tx_unlink(dfd, file);
				fd = -1;
				return r;
			}
			offset = sizeof(header);
		}
		else
			return (int) fd;
	}
	else
		do_playback = true;
	/* any other initialization here */
	if(do_playback)
	{
		int r = playback(NULL, fail_missing);
		if(r < 0)
		{
			deinit();
			return r;
		}
	}
	return 0;
}

int sys_journal::playback(journal_listener * target, bool fail_missing)
{
	/* playback */
	file_header header;
	entry_header entry;
	std::set<listener_id> missing;
	int r, ufd = tx_read_fd(fd);
	assert(ufd >= 0);
	r = lseek(ufd, 0, SEEK_SET);
	if(r < 0)
		return r;
	if((r = read(ufd, &header, sizeof(header))) != sizeof(header))
		return (r < 0) ? r : -EIO;
	if(header.magic != SYSJ_MAGIC || header.version != SYSJ_VERSION)
		return -EINVAL;
	while((r = read(ufd, &entry, sizeof(entry))) == sizeof(entry))
	{
		void * data;
		journal_listener * listener;
		if(entry.length == (size_t) -1)
		{
			/* warn if entry.id == target->id() ? */
			if(!target)
			{
				missing.erase(entry.id);
				discarded.insert(entry.id);
			}
			continue;
		}
		if(target)
		{
			listener = target;
			/* skip other entries */
			if(listener->id() != entry.id)
			{
				lseek(ufd, entry.length, SEEK_CUR);
				continue;
			}
		}
		else
		{
			listener = lookup_listener(entry.id);
			if(!listener)
			{
				missing.insert(entry.id);
				lseek(ufd, entry.length, SEEK_CUR);
				continue;
			}
		}
		data = malloc(entry.length);
		if(!data)
			return -ENOMEM;
		if(read(ufd, data, entry.length) != (ssize_t) entry.length)
		{
			free(data);
			return -EIO;
		}
		/* data is passed by reference */
		r = listener->journal_replay(data, entry.length);
		if(data)
			free(data);
		if(r < 0)
			return r;
	}
	if(r > 0)
		return -EIO;
	if(!offset)
		offset = lseek(ufd, 0, SEEK_END);
	if(fail_missing && !missing.empty())
		/* print warning message? */
		return -ENOENT;
	return r;
}

void sys_journal::deinit()
{
	if(fd >= 0)
	{
		discarded.clear();
		tx_close(fd);
		fd = -1;
	}
}

sys_journal sys_journal::global_journal;
std::map<sys_journal::listener_id, sys_journal::journal_listener *> sys_journal::listener_map;

sys_journal::journal_listener * sys_journal::lookup_listener(listener_id id)
{
	return listener_map.count(id) ? listener_map[id] : NULL;
}

int sys_journal::register_listener(journal_listener * listener)
{
	std::pair<listener_id, journal_listener *> pair(listener->id(), listener);
	if(!listener_map.insert(pair).second)
		return -EEXIST;
	return 0;
}

void sys_journal::unregister_listener(journal_listener * listener)
{
	listener_map.erase(listener->id());
}

sys_journal::unique_id sys_journal::id;

int sys_journal::set_unique_id_file(int dfd, const char * file, bool create)
{
	int r;
	if(id.fd >= 0)
		tx_close(id.fd);
	id.fd = tx_open(dfd, file, O_RDWR);
	if(id.fd < 0)
	{
		if(!create || (id.fd != -ENOENT && errno != ENOENT))
			return (int) id.fd;
		id.next = 0;
		id.fd = tx_open(dfd, file, O_RDWR | O_CREAT, 0644);
		if(id.fd < 0)
			return (int) id.fd;
		r = tx_write(id.fd, &id.next, sizeof(id.next), 0);
		if(r < 0)
		{
			tx_close(id.fd);
			id.fd = -1;
			unlinkat(dfd, file, 0);
			return r;
		}
	}
	else
	{
		r = read(tx_read_fd(id.fd), &id.next, sizeof(id.next));
		if(r != sizeof(id.next))
		{
			tx_close(id.fd);
			id.fd = -1;
			return (r < 0) ? r : -1;
		}
	}
	return 0;
}

sys_journal::listener_id sys_journal::get_unique_id()
{
	int r;
	listener_id next;
	if(id.fd < 0)
		return NO_ID;
	next = id.next++;
	r = tx_write(id.fd, &id.next, sizeof(id.next), 0);
	if(r < 0)
	{
		id.next = next;
		return NO_ID;
	}
	return next;
}
