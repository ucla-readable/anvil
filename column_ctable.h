/* This file is part of Toilet. Toilet is copyright 2007-2009 The Regents
 * of the University of California. It is distributed under the terms of
 * version 2 of the GNU GPL. See the file LICENSE for details. */

#ifndef __COLUMN_CTABLE_H
#define __COLUMN_CTABLE_H

#ifndef __cplusplus
#error column_ctable.h is a C++ header file
#endif

#include <map>

#include "ctable.h"
#include "ctable_factory.h"

#define COLUMN_CTABLE_MAGIC 0x36BC4B9D
#define COLUMN_CTABLE_VERSION 0

class column_ctable : public ctable
{
public:
	virtual dtable::key_iter * keys() const;
	virtual iter * iterator() const;
	virtual iter * iterator(const dtype & key) const;
	virtual blob find(const dtype & key, const istr & column) const;
	virtual bool contains(const dtype & key) const;
	
	inline virtual bool writable() const
	{
		return column_table[0]->writable();
	}
	
	virtual int insert(const dtype & key, const istr & column, const blob & value, bool append = false);
	virtual int remove(const dtype & key, const istr & column);
	virtual int remove(const dtype & key);
	
	int init(int dfd, const char * file, const params & config = params());
	void deinit();
	
	inline column_ctable() : columns(0), column_name(NULL), column_table(NULL) {}
	inline virtual ~column_ctable() { if(columns) deinit(); }
	
	virtual int maintain();
	
	virtual int set_blob_cmp(const blob_comparator * cmp);
	
	DECLARE_CT_OPEN_FACTORY(column_ctable);
	
	static int create(int dfd, const char * file, const params & config, dtype::ctype key_type);
	
private:
	struct ctable_header
	{
		uint32_t magic;
		uint32_t version;
		uint32_t columns;
	} __attribute__((packed));
	
	class iter : public ctable::iter
	{
	public:
		virtual bool valid() const;
		virtual bool next();
		virtual bool prev();
		virtual bool first();
		virtual bool last();
		virtual dtype key() const;
		virtual bool seek(const dtype & key);
		virtual bool seek(const dtype_test & test);
		virtual dtype::ctype key_type() const;
		virtual const istr & column() const;
		virtual blob value() const;
		inline iter(const column_ctable * base);
		virtual ~iter()
		{
			for(size_t i = 0; i < base->columns; i++)
				delete source[i];
			delete[] source;
		}
		
	private:
		bool all_next_skip();
		bool all_prev_skip();
		
		size_t number;
		dtable::iter ** source;
		const column_ctable * base;
	};
	
	typedef std::map<istr, size_t, strcmp_less> name_map;
	
	size_t columns;
	istr * column_name;
	name_map column_map;
	dtable ** column_table;
};

#endif /* __COLUMN_CTABLE_H */