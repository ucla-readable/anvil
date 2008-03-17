/* This file is part of Toilet. Toilet is copyright 2007-2008 The Regents
 * of the University of California. It is distributed under the terms of
 * version 2 of the GNU GPL. See the file LICENSE for details. */

#define _ATFILE_SOURCE

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include "openat.h"
#include "stable.h"
#include "hash_map.h"
#include "transaction.h"
#include "itable.h"

/* itable file format:
 * byte 0: k1 type (0 -> invalid, 1 -> int, 2 -> string)
 * byte 1: k2 type (same as k1 type)
 * byte 2-n: if k1 or k2 types are string, a string table
 * byte 2 or n+1: main data tables
 * 
 * main data tables:
 * bytes 0-3: k1 count
 * bytes 4-7: off_t base (can be extended to 8 bytes later)
 * byte 8: k1 size (1-4 bytes)
 * byte 9: k2 size (1-4 bytes)
 * byte 10: count size (1-4 bytes)
 * byte 11: offset (internal) size
 * byte 12: off_t (data) size
 * k1 array:
 * [] = byte 0-m: k2 count
 * [] = byte m+1-n: k1 value
 *      byte n+1-o: k2 array offset (relative to k1_offset)
 * each k2 array:
 * [] = byte 0-m: k2 value
 *      byte m+1-n: off_t */

/* TODO: use some sort of buffering here to avoid lots of small read()/write() calls */

struct itable_header {
	uint32_t k1_count;
	uint32_t off_base;
	uint8_t key_sizes[2];
	uint8_t count_size;
	uint8_t off_sizes[2];
} __attribute__((packed));

int itable_disk::init(int dfd, const char * file)
{
	int r = -1;
	uint8_t types[2];
	struct itable_header header;
	if(fd > -1)
		deinit();
	fd = openat(dfd, file, O_RDONLY);
	if(fd < 0)
		return fd;
	if(read(fd, types, 2) != 2)
		goto fail;
	if(!types[0] || !types[1])
		goto fail;
	k1t = (types[0] == 2) ? STRING : INT;
	k2t = (types[1] == 2) ? STRING : INT;
	k1_offset = 2;
	if(types[0] == 2 || types[1] == 2)
	{
		r = st_init(&st, fd, 2);
		if(r < 0)
			goto fail;
		k1_offset += st.size;
		r = lseek(fd, k1_offset, SEEK_SET);
		if(r < 0)
			goto fail_st;
	}
	r = read(fd, &header, sizeof(header));
	if(r != sizeof(header))
		goto fail_st;
	k1_offset += sizeof(header);
	k1_count = header.k1_count;
	off_base = header.off_base;
	key_sizes[0] = header.key_sizes[0];
	key_sizes[1] = header.key_sizes[1];
	count_size = header.count_size;
	off_sizes[0] = header.off_sizes[0];
	off_sizes[1] = header.off_sizes[1];
	entry_sizes[0] = count_size + key_sizes[0] + off_sizes[0];
	entry_sizes[1] = key_sizes[1] + off_sizes[1];
	
	/* sanity checks? */
	return 0;
	
fail_st:
	if(k1t == STRING || k2t == STRING)
		st_kill(&st);
fail:
	close(fd);
	fd = -1;
	return (r < 0) ? r : -1;
}

void itable_disk::deinit()
{
	if(fd < 0)
		return;
	if(k1t == STRING || k2t == STRING)
		st_kill(&st);
	close(fd);
	fd = -1;
}

int itable_disk::k1_get(size_t index, iv_int * value, size_t * k2_count, off_t * k2_offset)
{
	uint8_t bytes[12], bc = 0;
	int i, r;
	if(index >= k1_count)
		return -1;
	r = lseek(fd, k1_offset + entry_sizes[0] * index, SEEK_SET);
	if(r < 0)
		return r;
	r = read(fd, bytes, entry_sizes[0]);
	if(r != entry_sizes[0])
		return (r < 0) ? r : -1;
	/* read big endian order */
	*k2_count = 0;
	for(i = 0; i < count_size; i++)
		*k2_count = (*k2_count << 8) | bytes[bc++];
	*value = 0;
	for(i = 0; i < key_sizes[0]; i++)
		*value = (*value << 8) | bytes[bc++];
	*k2_offset = 0;
	for(i = 0; i < off_sizes[0]; i++)
		*k2_offset = (*k2_offset << 8) | bytes[bc++];
	return 0;
}

int itable_disk::k1_find(iv_int k1, size_t * k2_count, off_t * k2_offset, size_t * index)
{
	/* binary search */
	size_t min = 0, max = k1_count - 1;
	if(!k1_count)
		return -1;
	while(min <= max)
	{
		iv_int value;
		/* watch out for overflow! */
		size_t mid = min + (max - min) / 2;
		if(k1_get(mid, &value, k2_count, k2_offset) < 0)
			break;
		if(value < k1)
			min = mid + 1;
		else if(value > k1)
			max = mid - 1;
		else
		{
			if(index)
				*index = mid;
			return 0;
		}
	}
	return -1;
}

int itable_disk::k2_get(size_t k2_count, off_t k2_offset, size_t index, iv_int * value, off_t * offset)
{
	uint8_t bytes[8], bc = 0;
	int i, r;
	if(index >= k2_count)
		return -1;
	r = lseek(fd, k2_offset + entry_sizes[1] * index, SEEK_SET);
	if(r < 0)
		return r;
	r = read(fd, bytes, entry_sizes[1]);
	if(r != entry_sizes[1])
		return (r < 0) ? r : -1;
	/* read big endian order */
	*value = 0;
	for(i = 0; i < key_sizes[1]; i++)
		*value = (*value << 8) | bytes[bc++];
	*offset = 0;
	for(i = 0; i < off_sizes[1]; i++)
		*offset = (*offset << 8) | bytes[bc++];
	return 0;
}

int itable_disk::k2_find(size_t k2_count, off_t k2_offset, iv_int k2, off_t * offset, size_t * index)
{
	/* binary search */
	size_t min = 0, max = k2_count - 1;
	if(!k2_count)
		return -1;
	while(min <= max)
	{
		iv_int value;
		/* watch out for overflow! */
		size_t mid = min + (max - min) / 2;
		if(k2_get(k2_count, k2_offset, mid, &value, offset) < 0)
			break;
		if(value < k2)
			min = mid + 1;
		else if(value > k2)
			max = mid - 1;
		else
		{
			if(index)
				*index = mid;
			return 0;
		}
	}
	return -1;
}

bool itable_disk::has(iv_int k1)
{
	size_t k2_count;
	off_t k2_offset;
	return 0 <= k1_find(k1, &k2_count, &k2_offset);
}

bool itable_disk::has(const char * k1)
{
	ssize_t k1i;
	if(k1t != STRING)
		return false;
	k1i = st_locate(&st, k1);
	if(k1i < 0)
		return false;
	return has((iv_int) k1i);
}

bool itable_disk::has(iv_int k1, iv_int k2)
{
	size_t k2_count;
	off_t k2_offset, offset;
	int r = k1_find(k1, &k2_count, &k2_offset);
	if(r < 0)
		return false;
	return 0 <= k2_find(k2_count, k1_offset + k2_offset, k2, &offset);
}

bool itable_disk::has(iv_int k1, const char * k2)
{
	ssize_t k2i;
	if(k2t != STRING)
		return false;
	k2i = st_locate(&st, k2);
	if(k2i < 0)
		return false;
	return has(k1, (iv_int) k2i);
}

bool itable_disk::has(const char * k1, iv_int k2)
{
	ssize_t k1i;
	if(k1t != STRING)
		return false;
	k1i = st_locate(&st, k1);
	if(k1i < 0)
		return false;
	return has((iv_int) k1i, k2);
}

bool itable_disk::has(const char * k1, const char * k2)
{
	ssize_t k1i, k2i;
	if(k1t != STRING || k2t != STRING)
		return false;
	k1i = st_locate(&st, k1);
	if(k1i < 0)
		return false;
	k2i = st_locate(&st, k2);
	if(k2i < 0)
		return false;
	return has((iv_int) k1i, (iv_int) k2i);
}

off_t itable_disk::get(iv_int k1, iv_int k2)
{
	size_t k2_count;
	off_t k2_offset, offset;
	int r = k1_find(k1, &k2_count, &k2_offset);
	if(r < 0)
		return INVAL_OFF_T;
	r = k2_find(k2_count, k1_offset + k2_offset, k2, &offset);
	if(r < 0)
		return INVAL_OFF_T;
	return off_base + offset;
}

off_t itable_disk::get(iv_int k1, const char * k2)
{
	ssize_t k2i;
	if(k2t != STRING)
		return INVAL_OFF_T;
	k2i = st_locate(&st, k2);
	if(k2i < 0)
		return INVAL_OFF_T;
	return get(k1, (iv_int) k2i);
}

off_t itable_disk::get(const char * k1, iv_int k2)
{
	ssize_t k1i;
	if(k1t != STRING)
		return INVAL_OFF_T;
	k1i = st_locate(&st, k1);
	if(k1i < 0)
		return INVAL_OFF_T;
	return get((iv_int) k1i, k2);
}

off_t itable_disk::get(const char * k1, const char * k2)
{
	ssize_t k1i, k2i;
	if(k1t != STRING || k2t != STRING)
		return INVAL_OFF_T;
	k1i = st_locate(&st, k1);
	if(k1i < 0)
		return INVAL_OFF_T;
	k2i = st_locate(&st, k2);
	if(k2i < 0)
		return INVAL_OFF_T;
	return get((iv_int) k1i, (iv_int) k2i);
}

int itable_disk::iter(struct it * it)
{
	int r = k1_get(0, &it->k1, &it->k2_count, &it->k2_offset);
	if(r < 0)
		return r;
	it->k1i = 0;
	it->k2i = 0;
	it->k2_offset += k1_offset;
	return 0;
}

int itable_disk::iter(struct it * it, iv_int k1)
{
	int r = k1_find(k1, &it->k2_count, &it->k2_offset, &it->k1i);
	if(r < 0)
		return r;
	it->k2i = 0;
	it->k2_offset += k1_offset;
	return 0;
}

int itable_disk::iter(struct it * it, const char * k1)
{
	ssize_t k1i;
	if(k1t != STRING)
		return -1;
	k1i = st_locate(&st, k1);
	if(k1i < 0)
		return -1;
	return iter(it, (iv_int) k1i);
}

int itable_disk::next(struct it * it, iv_int * k1, iv_int * k2, off_t * off)
{
	int r;
	if(it->k1i >= k1_count)
		return -ENOENT;
	for(;;)
	{
		/* same primary key */
		if(it->k2i < it->k2_count)
		{
			*k1 = it->k1;
			r = k2_get(it->k2_count, it->k2_offset, it->k2i++, k2, off);
			if(r < 0)
				return r;
			*off += off_base;
			return 0;
		}
		if(++it->k1i >= k1_count)
			return -ENOENT;
		it->k2i = 0;
		r = k1_get(it->k1i, &it->k1, &it->k2_count, &it->k2_offset);
		if(r < 0)
			return r;
		it->k2_offset += k1_offset;
	}
	/* never get here */
}

int itable_disk::next(struct it * it, iv_int * k1, const char ** k2, off_t * off)
{
	int r;
	iv_int k2i;
	if(k2t != STRING)
		return -1;
	r = next(it, k1, &k2i, off);
	if(r < 0)
		return r;
	*k2 = st_get(&st, k2i);
	if(!*k2)
		return -1;
	return 0;
}

int itable_disk::next(struct it * it, const char ** k1, iv_int * k2, off_t * off)
{
	int r;
	iv_int k1i;
	if(k1t != STRING)
		return -1;
	r = next(it, &k1i, k2, off);
	if(r < 0)
		return r;
	*k1 = st_get(&st, k1i);
	if(!*k1)
		return -1;
	return 0;
}

#if ST_LRU < 2
#error ST_LRU must be at least 2
#endif
int itable_disk::next(struct it * it, const char ** k1, const char ** k2, off_t * off)
{
	int r;
	iv_int k1i, k2i;
	if(k1t != STRING || k2t != STRING)
		return -1;
	r = next(it, &k1i, &k2i, off);
	if(r < 0)
		return r;
	*k1 = st_get(&st, k1i);
	if(!*k1)
		return -1;
	*k2 = st_get(&st, k2i);
	if(!*k2)
		return -1;
	return 0;
}

int itable_disk::next(struct it * it, iv_int * k1, size_t * k2_count)
{
	int r;
	if(it->k1i >= k1_count)
		return -ENOENT;
	if(it->k1i)
	{
		off_t k2_offset;
		r = k1_get(it->k1i, &it->k1, k2_count, &k2_offset);
		if(r < 0)
			return r;
	}
	else
		*k2_count = it->k2_count;
	*k1 = it->k1;
	it->k1i++;
	return 0;
}

int itable_disk::next(struct it * it, const char ** k1, size_t * k2_count)
{
	int r;
	iv_int k1i;
	if(k1t != STRING)
		return -1;
	r = next(it, &k1i, k2_count);
	if(r < 0)
		return r;
	*k1 = st_get(&st, k1i);
	if(!*k1)
		return -1;
	return 0;
}

/* get a unique pointer for the string by putting it in a string hash map */
int itable_disk::add_string(const char ** string, hash_map_t * string_map, size_t * max_strlen)
{
	char * copy;
	size_t length = strlen(*string);
	if(length > *max_strlen)
		*max_strlen = length;
	copy = (char *) hash_map_find_val(string_map, *string);
	if(!copy)
	{
		int r;
		copy = strdup(*string);
		if(!copy)
			return -ENOMEM;
		r = hash_map_insert(string_map, copy, copy);
		if(r < 0)
		{
			free(copy);
			return r;
		}
	}
	*string = copy;
	return 0;
}

ssize_t itable_disk::locate_string(const char ** array, ssize_t size, const char * string)
{
	/* binary search */
	ssize_t min = 0, max = size - 1;
	while(min <= max)
	{
		int c;
		/* watch out for overflow! */
		ssize_t index = min + (max - min) / 2;
		c = strcmp(array[index], string);
		if(c < 0)
			min = index + 1;
		else if(c > 0)
			max = index - 1;
		else
			return index;
	}
	return -1;
}

int itable_disk::create(int dfd, const char * file, itable * source)
{
	struct it iter;
	hash_map_t * string_map = NULL;
	const char ** string_array = NULL;
	off_t min_off = 0, max_off = 0, off;
	size_t k1_count = 0, k2_count = 0, k2_count_max = 0, max_strlen = 0, strings = 0;
	union { iv_int i; const char * s; } k1, old_k1, k2;
	iv_int k1_max = 0, k2_max = 0;
	int r = source->iter(&iter);
	if(r < 0)
		return r;
	if(source->k1_type() == STRING || source->k2_type() == STRING)
	{
		string_map = hash_map_create_str();
		if(!string_map)
			return -ENOMEM;
	}
	for(;;)
	{
		if(source->k1_type() == STRING)
		{
			if(source->k2_type() == STRING)
				r = source->next(&iter, &k1.s, &k2.s, &off);
			else
				r = source->next(&iter, &k1.s, &k2.i, &off);
		}
		else
		{
			if(source->k2_type() == STRING)
				r = source->next(&iter, &k1.i, &k2.s, &off);
			else
				r = source->next(&iter, &k1.i, &k2.i, &off);
		}
		if(r)
			break;
		if(source->k1_type() == STRING)
		{
			if((r = add_string(&k1.s, string_map, &max_strlen)) < 0)
				break;
		}
		else if(k1.i > k1_max)
			k1_max = k1.i;
		if(source->k2_type() == STRING)
		{
			if((r = add_string(&k2.s, string_map, &max_strlen)) < 0)
				break;
		}
		else if(k2.i > k2_max)
			k2_max = k2.i;
		if(!k1_count)
			min_off = off;
		/* we can compare strings by pointer because add_string() makes them unique */
		if(!k1_count || ((source->k1_type() == STRING) ? (k1.s != old_k1.s) : (k1.i != old_k1.i)))
		{
			if(k2_count > k2_count_max)
				k2_count_max = k2_count;
			k2_count = 0;
			if(source->k1_type() == STRING)
				old_k1.s = k1.s;
			else
				old_k1.i = k1.i;
			k1_count++;
		}
		if(off < min_off)
			min_off = off;
		if(off > max_off)
			max_off = off;
		k2_count++;
	}
	if(r != -ENOENT)
	{
		if(string_map)
		{
			hash_map_it2_t hm_it;
		fail_strings:
			hm_it = hash_map_it2_create(string_map);
			while(hash_map_it2_next(&hm_it))
				free((void *) hm_it.val);
			hash_map_destroy(string_map);
		}
		return (r < 0) ? r : -1;
	}
	/* now we have k1_count, k2_count_max, min_off, and max_off, and,
	 * if appropriate, k1_max, k2_max, string_map, and max_strlen */
	if(string_map)
	{
		size_t i = 0;
		hash_map_it2_t hm_it;
		strings = hash_map_size(string_map);
		string_array = (const char **) malloc(sizeof(*string_array) * strings);
		if(!string_array)
		{
			r = -ENOMEM;
			goto fail_strings;
		}
		hm_it = hash_map_it2_create(string_map);
		while(hash_map_it2_next(&hm_it))
			string_array[i++] = (const char *) hm_it.val;
		assert(i == strings);
		hash_map_destroy(string_map);
		string_map = NULL;
	}
	
	/* now write the file */
	tx_fd fd;
	off_t out_off;
	struct itable_header header;
	uint8_t bytes[12], bc;
	header.k1_count = k1_count;
	header.off_base = min_off;
	if(source->k1_type() == STRING)
		header.key_sizes[0] = byte_size(strings - 1);
	else
		header.key_sizes[0] = byte_size(k1_max);
	if(source->k2_type() == STRING)
		header.key_sizes[1] = byte_size(strings - 1);
	else
		header.key_sizes[1] = byte_size(k2_max);
	header.count_size = byte_size(k2_count_max);
	header.off_sizes[0] = 4; /* TODO: find real value */
	header.off_sizes[1] = byte_size(max_off -= min_off);
	bytes[0] = (source->k1_type() == STRING) ? 2 : 1;
	bytes[1] = (source->k2_type() == STRING) ? 2 : 1;
	
	/* ok, screw error handling for a while */
	fd = tx_open(dfd, file, O_RDWR | O_CREAT, 0644);
	tx_write(fd, bytes, 0, 2);
	out_off = 2;
	if(string_array)
		st_create(fd, &out_off, string_array, strings);
	tx_write(fd, &header, out_off, sizeof(header));
	out_off += sizeof(header);
	off = (header.count_size + header.key_sizes[0] + header.off_sizes[0]) * k1_count;
	/* now the k1 array */
	source->iter(&iter);
	for(;;)
	{
		uint32_t value;
		if(source->k1_type() == STRING)
			r = source->next(&iter, &k1.s, &k2_count);
		else
			r = source->next(&iter, &k1.i, &k2_count);
		if(r)
			break;
		bc = 0;
		value = k2_count;
		layout_bytes(bytes, &bc, value, header.count_size);
		if(source->k1_type() == STRING)
			value = locate_string(string_array, strings, k1.s);
		else
			value = k1.i;
		layout_bytes(bytes, &bc, value, header.key_sizes[0]);
		value = off;
		off += (header.key_sizes[1] + header.off_sizes[1]) * k2_count;
		layout_bytes(bytes, &bc, value, header.off_sizes[0]);
		tx_write(fd, bytes, out_off, bc);
		out_off += bc;
	}
	/* and the k2 arrays */
	source->iter(&iter);
	for(;;)
	{
		uint32_t value;
		if(source->k1_type() == STRING)
		{
			if(source->k2_type() == STRING)
				r = source->next(&iter, &k1.s, &k2.s, &off);
			else
				r = source->next(&iter, &k1.s, &k2.i, &off);
		}
		else
		{
			if(source->k2_type() == STRING)
				r = source->next(&iter, &k1.i, &k2.s, &off);
			else
				r = source->next(&iter, &k1.i, &k2.i, &off);
		}
		if(r)
			break;
		bc = 0;
		if(source->k2_type() == STRING)
			value = locate_string(string_array, strings, k2.s);
		else
			value = k2.i;
		layout_bytes(bytes, &bc, value, header.key_sizes[1]);
		value = off - min_off;
		layout_bytes(bytes, &bc, value, header.off_sizes[1]);
		tx_write(fd, bytes, out_off, bc);
		out_off += bc;
	}
	
	tx_close(fd);
	return 0;
}

/* XXX HACK for testing... */
extern "C" {
int command_itable(int argc, const char * argv[])
{
	itable_disk it;
	itable::it iter;
	const char * col;
	size_t count;
	iv_int row;
	off_t off;
	int r;
	if(argc < 2)
		return 0;
	r = it.init(AT_FDCWD, argv[1]);
	printf("it.init(%s) = %d\n", argv[1], r);
	if(r < 0)
		return r;
	r = it.iter(&iter);
	printf("it.iter() = %d\n", r);
	if(r < 0)
		return r;
	while(!(r = it.next(&iter, &row, &col, &off)))
		printf("row = 0x%x, col = %s, offset = 0x%x\n", row, col, (int) off);
	printf("it.next() = %d\n", r);
	r = it.iter(&iter);
	printf("it.iter() = %d\n", r);
	if(r < 0)
		return r;
	while(!(r = it.next(&iter, &row, &count)))
		printf("row = 0x%x\n", row);
	if(argc > 2)
	{
		printf("%s -> %s\n", argv[1], argv[2]);
		r = tx_start();
		printf("tx_start() = %d\n", r);
		r = itable_disk::create(AT_FDCWD, argv[2], &it);
		printf("create() = %d\n", r);
		r = tx_end(0);
		printf("tx_end() = %d\n", r);
		if(r >= 0)
		{
			argv[1] = argv[0];
			return command_itable(argc - 1, &argv[1]);
		}
	}
	return 0;
}
}
