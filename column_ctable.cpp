/* This file is part of Anvil. Anvil is copyright 2007-2009 The Regents
 * of the University of California. It is distributed under the terms of
 * version 2 of the GNU GPL. See the file LICENSE for details. */

#define _ATFILE_SOURCE

#include <set>

#include "openat.h"

#include "util.h"
#include "rwfile.h"
#include "rofile.h"
#include "transaction.h"
#include "dtable_factory.h"
#include "column_ctable.h"

/* FIXME: column_ctable fails if you do not have all extant blobs for a row */
/* FIXME: there are probably some bugs/inconsistencies with nonexistent values */

column_ctable::iter::iter(const column_ctable * base)
	: base(base)
{
	source = new dtable::iter *[base->column_count];
	assert(source);
	for(size_t i = 0; i < base->column_count; i++)
	{
		source[i] = base->column_table[i]->iterator();
		assert(source[i]);
	}
	number = source[0]->valid() ? 0 : base->column_count;
}

bool column_ctable::iter::valid() const
{
	return number < base->column_count;
}

bool column_ctable::iter::all_next_skip()
{
	bool valid;
	do {
		valid = source[0]->next();
		for(size_t i = 1; i < base->column_count; i++)
			source[i]->next();
	} while(valid && !source[0]->meta().exists());
	return valid;
}

bool column_ctable::iter::all_prev_skip()
{
	bool valid;
	do {
		valid = source[0]->prev();
		for(size_t i = 1; i < base->column_count; i++)
			source[i]->prev();
	} while(valid && !source[0]->meta().exists());
	if(!valid)
		while(source[0]->valid() && source[0]->meta().exists())
			for(size_t i = 0; i < base->column_count; i++)
				source[i]->next();
	return valid;
}

bool column_ctable::iter::next(bool row)
{
	if(number >= base->column_count)
		return false;
	if(!row && ++number < base->column_count)
		return true;
	number = all_next_skip() ? 0 : base->column_count;
	return number < base->column_count;
}

bool column_ctable::iter::prev(bool row)
{
	if(!row && number && number < base->column_count)
	{
		number--;
		return true;
	}
	number = all_prev_skip() ? base->column_count - 1 : base->column_count;
	return number < base->column_count;
}

bool column_ctable::iter::first()
{
	bool valid = source[0]->first();
	for(size_t i = 1; i < base->column_count; i++)
		source[i]->first();
	if(valid && !source[0]->meta().exists())
		valid = next();
	return valid;
}

bool column_ctable::iter::last()
{
	bool valid = source[0]->last();
	for(size_t i = 1; i < base->column_count; i++)
		source[i]->last();
	if(valid && !source[0]->meta().exists())
		valid = prev();
	return valid;
}

dtype column_ctable::iter::key() const
{
	return source[0]->key();
}

bool column_ctable::iter::seek(const dtype & key)
{
	/* bug? what if we find the nonexistent value? */
	bool found = source[0]->seek(key);
	for(size_t i = 1; i < base->column_count; i++)
		source[i]->seek(key);
	if(found || !source[0]->valid())
	{
		number = found ? 0 : base->column_count;
		return found;
	}
	number = all_next_skip() ? 0 : base->column_count;
	return false;
}

bool column_ctable::iter::seek(const dtype_test & test)
{
	/* bug? what if we find the nonexistent value? */
	bool found = source[0]->seek(test);
	for(size_t i = 1; i < base->column_count; i++)
		source[i]->seek(test);
	if(found || !source[0]->valid())
	{
		number = found ? 0 : base->column_count;
		return found;
	}
	number = all_next_skip() ? 0 : base->column_count;
	return false;
}

dtype::ctype column_ctable::iter::key_type() const
{
	return base->key_type();
}

size_t column_ctable::iter::column() const
{
	return (number < base->column_count) ? number : (size_t) -1;
}

const istr & column_ctable::iter::name() const
{
	return (number < base->column_count) ? base->column_name[number] : istr::null;
}

blob column_ctable::iter::value() const
{
	return (number < base->column_count) ? source[number]->value() : blob();
}

blob column_ctable::iter::index(size_t column) const
{
	return (column < base->column_count) ? source[column]->value() : blob();
}

column_ctable::p_iter::p_iter(const column_ctable * base, const size_t * columns, size_t count)
	: base(base)
{
	assert(count);
	source = new dtable::iter *[base->column_count];
	assert(source);
	for(size_t i = 0; i < base->column_count; i++)
		source[i] = NULL;
	for(size_t i = 0; i < count; i++)
	{
		assert(columns[i] < base->column_count);
		assert(!source[columns[i]]);
		source[columns[i]] = base->column_table[columns[i]]->iterator();
		assert(source[columns[i]]);
	}
	start = (size_t) -1;
	for(size_t i = 0; i < base->column_count; i++)
		if(source[i])
		{
			start = i;
			break;
		}
	/* this is truly an assert, not lame error checking */
	assert(start != (size_t) -1);
}

bool column_ctable::p_iter::valid() const
{
	return source[start]->valid();
}

bool column_ctable::p_iter::next()
{
	bool valid;
	do {
		valid = source[start]->next();
		for(size_t i = start + 1; i < base->column_count; i++)
			if(source[i])
				source[i]->next();
	} while(valid && !source[start]->meta().exists());
	return valid;
}

bool column_ctable::p_iter::prev()
{
	bool valid;
	do {
		valid = source[start]->prev();
		for(size_t i = start + 1; i < base->column_count; i++)
			if(source[i])
				source[i]->prev();
	} while(valid && !source[start]->meta().exists());
	if(!valid)
		while(source[start]->valid() && source[start]->meta().exists())
			for(size_t i = start; i < base->column_count; i++)
				if(source[i])
					source[i]->next();
	return valid;
}

bool column_ctable::p_iter::first()
{
	bool valid = source[start]->first();
	for(size_t i = start + 1; i < base->column_count; i++)
		if(source[i])
			source[i]->first();
	if(valid && !source[start]->meta().exists())
		valid = next();
	return valid;
}

bool column_ctable::p_iter::last()
{
	bool valid = source[start]->last();
	for(size_t i = start + 1; i < base->column_count; i++)
		if(source[i])
			source[i]->last();
	if(valid && !source[0]->meta().exists())
		valid = prev();
	return valid;
}

dtype column_ctable::p_iter::key() const
{
	return source[start]->key();
}

bool column_ctable::p_iter::seek(const dtype & key)
{
	/* bug? what if we find the nonexistent value? */
	bool found = source[start]->seek(key);
	for(size_t i = start + 1; i < base->column_count; i++)
		if(source[i])
			source[i]->seek(key);
	if(found || !source[start]->valid())
		return found;
	next();
	return false;
}

bool column_ctable::p_iter::seek(const dtype_test & test)
{
	/* bug? what if we find the nonexistent value? */
	bool found = source[start]->seek(test);
	for(size_t i = start + 1; i < base->column_count; i++)
		if(source[i])
			source[i]->seek(test);
	if(found || !source[start]->valid())
		return found;
	next();
	return false;
}

dtype::ctype column_ctable::p_iter::key_type() const
{
	return base->key_type();
}

blob column_ctable::p_iter::value(size_t column) const
{
	assert(column < base->column_count);
	assert(source[column]);
	return source[column]->value();
}

dtable::key_iter * column_ctable::keys() const
{
	return column_table[0]->iterator();
}

ctable::iter * column_ctable::iterator() const
{
	return new iter(this);
}

ctable::p_iter * column_ctable::iterator(const size_t * columns, size_t count) const
{
	return new p_iter(this, columns, count);
}

blob column_ctable::find(const dtype & key, size_t column) const
{
	assert(column < column_count);
	return column_table[column]->find(key);
}

bool column_ctable::contains(const dtype & key) const
{
	return column_table[0]->contains(key);
}

int column_ctable::insert(const dtype & key, size_t column, const blob & value, bool append)
{
	assert(column < column_count);
	return column_table[column]->insert(key, value, append);
}

int column_ctable::remove(const dtype & key, size_t column)
{
	return insert(key, column, blob());
}

int column_ctable::remove(const dtype & key)
{
	int r = tx_start_r();
	if(r < 0)
		return r;
	for(size_t i = 0; i < column_count; i++)
	{
		r = column_table[i]->remove(key);
		assert(r >= 0);
	}
	r = tx_end_r();
	assert(r >= 0);
	return 0;
}

int column_ctable::set_blob_cmp(const blob_comparator * cmp)
{
	int r = 0;
	for(size_t i = 0; i < column_count; i++)
	{
		int r2 = column_table[i]->set_blob_cmp(cmp);
		if(r2 < 0)
			r = r2;
	}
	return r;
}

int column_ctable::maintain(bool force)
{
	int r = 0;
	for(size_t i = 0; i < column_count; i++)
	{
		int r2 = column_table[i]->maintain(force);
		if(r2 < 0)
			r = r2;
	}
	return r;
}

void column_ctable::deinit()
{
	if(column_count)
	{
		for(size_t i = 0; i < column_count; i++)
			column_table[i]->destroy();
		delete[] column_table;
		delete[] column_name;
		column_map.empty();
		column_count = 0;
		ctable::deinit();
	}
}

int column_ctable::init(int dfd, const char * file, const params & config, sys_journal * sysj)
{
	int cct_dfd, r;
	const dtable_factory * base = NULL;
	params base_config;
	bool have_base;
	
	off_t offset;
	ctable_header meta;
	rofile * meta_file;
	
	cct_dfd = openat(dfd, file, O_RDONLY);
	if(cct_dfd < 0)
		return cct_dfd;
	meta_file = rofile::open<4, 2>(cct_dfd, "cct_meta");
	if(!meta_file)
		goto fail_open;
	r = meta_file->read_type(0, &meta);
	if(r < 0)
		goto fail_header;
	if(meta.magic != COLUMN_CTABLE_MAGIC || meta.version != COLUMN_CTABLE_VERSION)
		goto fail_header;
	column_count = meta.columns;
	
	column_name = new istr[column_count];
	if(!column_name)
		goto fail_header;
	column_table = new dtable *[column_count];
	if(!column_table)
		goto fail_tables;
	
	have_base = config.has("base");
	if(have_base && !(base = dtable_factory::lookup(config, "base")))
		goto fail_config;
	if(!config.get("base_config", &base_config))
		goto fail_config;
	
	/* check that we have all the config we need */
	for(size_t i = 0; i < column_count; i++)
	{
		istr name;
		char string[32];
		params column_config;
		sprintf(string, "column%d_base", (int) i);
		name = string;
		if(config.has(name))
		{
			/* typecheck and lookup class */
			if(!dtable_factory::lookup(config, name))
				goto fail_names;
		}
		else if(!have_base)
			goto fail_names;
		sprintf(string, "column%d_config", (int) i);
		/* typecheck */
		if(!config.get(string, &column_config, params()))
			goto fail_names;
		column_table[i] = NULL;
	}
	
	offset = sizeof(meta);
	for(size_t i = 0; i < column_count; i++)
	{
		uint32_t length;
		r = meta_file->read_type(offset, &length);
		if(r < 0)
			goto fail_names;
		offset += sizeof(length);
		column_name[i] = meta_file->read_string(offset, length);
		if(!column_name[i])
			goto fail_names;
		offset += length;
		column_map[column_name[i]] = i;
	}
	
	for(size_t i = 0; i < column_count; i++)
	{
		istr name;
		char string[32];
		params column_config;
		const dtable_factory * column;
		sprintf(string, "column%d_base", (int) i);
		name = string;
		if(config.has(name))
			column = dtable_factory::lookup(config, name);
		else
			column = base;
		assert(column);
		sprintf(string, "column%d_config", (int) i);
		name = string;
		r = config.get(name, &column_config, base_config);
		assert(r);
		column_table[i] = column->open(cct_dfd, column_name[i], column_config, sysj);
		if(!column_table[i])
			goto fail_load;
	}
	
	cmp_name = column_table[0]->get_cmp_name();
	
	delete meta_file;
	close(cct_dfd);
	return 0;
	
fail_load:
	for(size_t i = 0; i < column_count; i++)
		if(column_table[i])
			column_table[i]->destroy();
fail_names:
	column_map.empty();
fail_config:
	delete[] column_table;
fail_tables:
	delete[] column_name;
fail_header:
	delete meta_file;
	column_count = 0;
fail_open:
	close(cct_dfd);
	return -1;
}

int column_ctable::create(int dfd, const char * file, const params & config, dtype::ctype key_type)
{
	int cct_dfd, columns, r;
	const dtable_factory * base = NULL;
	params base_config;
	istr column_name;
	bool have_base;
	std::set<istr, strcmp_less> names;
	
	ctable_header meta;
	rwfile meta_file;
	
	if(!config.get("columns", &columns, 0))
		return -EINVAL;
	if(columns < 1)
		return -EINVAL;
	
	have_base = config.has("base");
	if(have_base && !(base = dtable_factory::lookup(config, "base")))
		return -EINVAL;
	if(!config.get("base_config", &base_config))
		return -EINVAL;
	
	/* check that we have all the config we need */
	for(int i = 0; i < columns; i++)
	{
		char string[32];
		params column_config;
		sprintf(string, "column%d_base", i);
		column_name = string;
		if(config.has(column_name))
		{
			/* typecheck and lookup class */
			if(!dtable_factory::lookup(config, column_name))
				return -EINVAL;
		}
		else if(!have_base)
			return -EINVAL;
		sprintf(string, "column%d_config", i);
		/* typecheck */
		if(!config.get(string, &column_config, params()))
			return -EINVAL;
		sprintf(string, "column%d_name", i);
		if(!config.get(string, &column_name) || !column_name)
			return -EINVAL;
		if(names.count(column_name))
			return -EEXIST;
		names.insert(column_name);
	}
	names.clear();
	
	r = mkdirat(dfd, file, 0755);
	if(r < 0)
		return r;
	cct_dfd = openat(dfd, file, O_RDONLY);
	if(cct_dfd < 0)
		goto fail_open;
	
	meta.magic = COLUMN_CTABLE_MAGIC;
	meta.version = COLUMN_CTABLE_VERSION;
	meta.columns = columns;
	r = meta_file.create(cct_dfd, "cct_meta");
	if(r < 0)
		goto fail_meta;
	r = meta_file.append(&meta);
	if(r < 0)
		goto fail_loop;
	
	/* create the columns and record their names */
	for(int i = 0; i < columns; i++)
	{
		char string[32];
		uint32_t length;
		params column_config;
		const dtable_factory * column;
		sprintf(string, "column%d_base", i);
		column_name = string;
		if(config.has(column_name))
			column = dtable_factory::lookup(config, column_name);
		else
			column = base;
		assert(column);
		sprintf(string, "column%d_config", i);
		r = config.get(string, &column_config, base_config);
		assert(r);
		sprintf(string, "column%d_name", i);
		r = config.get(string, &column_name);
		assert(r && column_name);
		r = column->create(cct_dfd, column_name, column_config, key_type);
		if(r < 0)
			goto fail_loop;
		length = column_name.length();
		r = meta_file.append(&length);
		if(r < 0)
			goto fail_loop;
		r = meta_file.append(column_name);
		if(r < 0)
			goto fail_loop;
	}
	r = meta_file.flush();
	if(r < 0)
		goto fail_loop;
	meta_file.close();
	
	close(cct_dfd);
	return 0;
	
fail_loop:
	meta_file.close();
fail_meta:
	close(cct_dfd);
fail_open:
	util::rm_r(dfd, file);
	return -1;
}

DEFINE_CT_FACTORY(column_ctable);
