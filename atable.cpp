/* This file is part of Toilet. Toilet is copyright 2007-2008 The Regents
 * of the University of California. It is distributed under the terms of
 * version 2 of the GNU GPL. See the file LICENSE for details. */

#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>

#include "transaction.h"
#include "atable.h"

bool atable::has(iv_int k1)
{
}

bool atable::has(const char * k1)
{
}

bool atable::has(iv_int k1, iv_int k2)
{
}

bool atable::has(iv_int k1, const char * k2)
{
}

bool atable::has(const char * k1, iv_int k2)
{
}

bool atable::has(const char * k1, const char * k2)
{
}

off_t atable::get(iv_int k1, iv_int k2)
{
}

off_t atable::get(iv_int k1, const char * k2)
{
}

off_t atable::get(const char * k1, iv_int k2)
{
}

off_t atable::get(const char * k1, const char * k2)
{
}

int atable::iter(struct it * it)
{
}

int atable::iter(struct it * it, iv_int k1)
{
}

int atable::iter(struct it * it, const char * k1)
{
}

int atable::next(struct it * it, iv_int * k1, iv_int * k2, off_t * off)
{
}

int atable::next(struct it * it, iv_int * k1, const char ** k2, off_t * off)
{
}

int atable::next(struct it * it, const char ** k1, iv_int * k2, off_t * off)
{
}

int atable::next(struct it * it, const char ** k1, const char ** k2, off_t * off)
{
}

int atable::next(struct it * it, iv_int * k1)
{
}

int atable::next(struct it * it, const char ** k1)
{
}

#define ATABLE_VALUE 1
#define ATABLE_STRING 2

struct file_header {
	uint32_t magic;
	uint16_t version;
	uint8_t types[2];
} __attribute__((packed));

struct atable_data {
	uint32_t k1, k2;
	off_t value;
} __attribute__((packed));

int atable::add_string(const char * string, uint32_t * index)
{
	uint8_t type = ATABLE_STRING;
	uint32_t length;
	const char * copy = strings.lookup(string, index);
	if(copy)
		return 0;
	copy = strings.add(string, index);
	if(!copy)
		return -ENOMEM;
	if(tx_write(fd, &type, offset, 1) < 0)
	{
		strings.remove(copy);
		return -1;
	}
	offset++;
	length = strlen(copy);
	if(tx_write(fd, &length, offset, sizeof(length)) < 0)
	{
		/* need to unwrite the type */
		assert(0);
		return -1;
	}
	offset += sizeof(length);
	if(tx_write(fd, copy, offset, length) < 0)
	{
		/* need to unwrite the type and length */
		assert(0);
		return -1;
	}
	offset += length;
	return 0;
}

int atable::append(iv_int k1, iv_int k2, off_t off)
{
	int r;
	struct atable_data data;
	data.k1 = k1;
	data.k2 = k2;
	data.value = off;
	r = tx_write(fd, &data, offset, sizeof(data));
	if(r < 0)
		return r;
	offset += sizeof(data);
	/* XXX */
	return -ENOSYS;
}

int atable::append(iv_int k1, const char * k2, off_t off)
{
	int r;
	uint32_t index;
	if(k2t != STRING)
		return -EINVAL;
	r = add_string(k2, &index);
	if(r < 0)
		return r;
	return append(k1, (iv_int) index, off);
}

int atable::append(const char * k1, iv_int k2, off_t off)
{
	int r;
	uint32_t index;
	if(k1t != STRING)
		return -EINVAL;
	r = add_string(k1, &index);
	if(r < 0)
		return r;
	return append((iv_int) index, k2, off);
}

int atable::append(const char * k1, const char * k2, off_t off)
{
	int r;
	uint32_t index1, index2;
	if(k1t != STRING || k2t != STRING)
		return -EINVAL;
	r = add_string(k1, &index1);
	if(r < 0)
		return r;
	r = add_string(k2, &index2);
	if(r < 0)
		return r;
	return append((iv_int) index1, (iv_int) index2, off);
}

int atable::playback()
{
	/* playback */
	uint8_t type;
	struct file_header header;
	uint32_t string_index = 0;
	int r, ufd = tx_read_fd(fd);
	assert(ufd >= 0);
	r = lseek(ufd, 0, SEEK_SET);
	if(r < 0)
		return r;
	if((r = read(ufd, &header, sizeof(header))) != sizeof(header))
		return (r < 0) ? r : -EIO;
	if(header.magic != ATABLE_MAGIC || header.version != ATABLE_VERSION)
		return -EINVAL;
	if(header.types[0] != 1 && header.types[0] != 2)
		return -EINVAL;
	if(header.types[1] != 1 && header.types[1] != 2)
		return -EINVAL;
	if(header.types[0] == 2 && k1t != STRING)
		return -EINVAL;
	if(header.types[1] == 2 && k2t != STRING)
		return -EINVAL;
	while((r = read(ufd, &type, 1)) == 1)
	{
		if(type == ATABLE_VALUE)
		{
			struct atable_data data;
			const char * k1s = NULL;
			const char * k2s = NULL;
			r = read(ufd, &data, sizeof(data));
			if(r != sizeof(data))
				return (r < 0) ? r : -EIO;
			if(k1t == STRING)
			{
				k1s = strings.lookup(data.k1);
				if(!k1s)
					return -EINVAL;
			}
			if(k2t == STRING)
			{
				k2s = strings.lookup(data.k2);
				if(!k2s)
					return -EINVAL;
			}
			/* actually we could just use the integer indices here too */
			if(k1t == STRING && k2t == STRING)
				r = append(k1s, k2s, data.value);
			else if(k2t == STRING)
				r = append(data.k1, k2s, data.value);
			else if(k1t == STRING)
				r = append(k1s, data.k2, data.value);
			else
				r = append(data.k1, data.k2, data.value);
			if(r < 0)
				return r;
		}
		else if(type == ATABLE_STRING)
		{
			uint32_t length, index;
			char * string;
			if(k1t != STRING && k2t != STRING)
				return -EINVAL;
			r = read(ufd, &length, 4);
			if(r != 4)
				return (r < 0) ? r : -EIO;
			string = (char *) malloc(length + 1);
			if(!string)
				return -ENOMEM;
			r = read(ufd, string, length);
			if(r != length)
				return (r < 0) ? r : -EIO;
			string[length] = 0;
			if(!strings.add(string, &index))
				return -ENOMEM;
			assert(index == string_index);
			string_index++;
			free(string);
		}
		else
			return -EINVAL;
	}
	assert(r <= 0);
	offset = lseek(ufd, 0, SEEK_END);
	return r;
}

int atable::init(int dfd, const char * file, ktype k1, ktype k2)
{
	bool do_playback = false;
	if(fd >= 0)
		deinit();
	k1t = k1;
	k2t = k2;
	if(k1 == STRING || k2 == STRING)
	{
		int r = strings.init(true);
		if(r < 0)
			return r;
	}
	fd = tx_open(dfd, file, O_RDWR);
	if(fd < 0)
	{
		if(fd == -ENOENT || errno == ENOENT)
		{
			int r;
			struct file_header header;
			/* XXX due to O_CREAT not being part of the transaction, we might get
			 * an empty file as a result of this, which later will cause an error
			 * below instead of here... we should handle that case somewhere */
			fd = tx_open(dfd, file, O_RDWR | O_CREAT);
			if(fd < 0)
				return (int) fd;
			header.magic = ATABLE_MAGIC;
			header.version = ATABLE_VERSION;
			header.types[0] = (k1 == STRING) ? 2 : 1;
			header.types[1] = (k2 == STRING) ? 2 : 1;
			r = tx_write(fd, &header, 0, sizeof(header));
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
	/* do other initialization here */
	if(do_playback)
	{
		int r = playback();
		if(r < 0)
		{
			deinit();
			return r;
		}
	}
	return 0;
}

void atable::deinit()
{
	/* do other deinitialization here */
	if(fd >= 0)
	{
		tx_close(fd);
		fd = -1;
	}
	strings.deinit();
}
