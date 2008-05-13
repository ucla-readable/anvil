/* This file is part of Toilet. Toilet is copyright 2007-2008 The Regents
 * of the University of California. It is distributed under the terms of
 * version 2 of the GNU GPL. See the file LICENSE for details. */

#ifndef __INDEX_H
#define __INDEX_H

#include "toilet.h"

/* rename all the symbols for the time being */
#define toilet_index_init ctoilet_index_init
#define toilet_index_drop ctoilet_index_drop
#define toilet_open_index ctoilet_open_index
#define toilet_close_index ctoilet_close_index
#define toilet_index_type ctoilet_index_type
#define toilet_index_add ctoilet_index_add
#define toilet_index_change ctoilet_index_change
#define toilet_index_remove ctoilet_index_remove
#define toilet_index_size ctoilet_index_size
#define toilet_index_list ctoilet_index_list
#define toilet_index_count ctoilet_index_count
#define toilet_index_find ctoilet_index_find
#define toilet_index_count_range ctoilet_index_count_range
#define toilet_index_find_range ctoilet_index_find_range

#ifdef __cplusplus

#include "multimap.h"
#include "diskhash.h"
#include "disktree.h"

struct t_index {
	/* note that I_BOTH == I_HASH | I_TREE */
	enum i_type { I_NONE = 0, I_HASH = 1, I_TREE = 2, I_BOTH = 3 } type;
	t_type data_type;
	/* value -> blob of row IDs */
	struct {
		diskhash * disk;
	} hash;
	struct {
		disktree * disk;
	} tree;
};

/* Annoyingly, C++ enums do not by themselves support these bitwise operations,
 * due to type errors. Rather than force the expansion of |= and &= everywhere
 * to add casts, we do it here as inline operators. */

inline t_index::i_type & operator|=(t_index::i_type &x, const t_index::i_type &y)
{
	x = (t_index::i_type) (x | y);
	return x;
}

inline t_index::i_type & operator&=(t_index::i_type &x, const t_index::i_type &y)
{
	x = (t_index::i_type) (x & y);
	return x;
}

extern "C" {
#endif

int toilet_index_init(int dfd, const char * path, t_type type);
int toilet_index_drop(int dfd, const char * store);

t_index * toilet_open_index(uint8_t * id, int dfd, const char * path, const char * name);
void toilet_close_index(t_index * index);

t_type toilet_index_type(t_index * index);

int toilet_index_add(t_index * index, t_row_id id, t_type type, t_value * value);
int toilet_index_change(t_index * index, t_row_id id, t_type type, t_value * old_value, t_value * new_value);
int toilet_index_remove(t_index * index, t_row_id id, t_type type, t_value * value);

/* all rows */
ssize_t toilet_index_size(t_index * index);
t_rowset * toilet_index_list(t_index * index, t_type type);

/* matching rows only */
ssize_t toilet_index_count(t_index * index, t_type type, t_value * value);
t_rowset * toilet_index_find(t_index * index, t_type type, t_value * value);
ssize_t toilet_index_count_range(t_index * index, t_type type, t_value * low_value, t_value * high_value);
t_rowset * toilet_index_find_range(t_index * index, t_type type, t_value * low_value, t_value * high_value);

#ifdef __cplusplus
}
#endif

#endif /* __INDEX_H */
