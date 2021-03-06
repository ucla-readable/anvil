/* This file is part of Anvil. Anvil is copyright 2007-2009 The Regents
 * of the University of California. It is distributed under the terms of
 * version 2 of the GNU GPL. See the file LICENSE for details. */

#ifndef __OVERLAY_DTABLE_H
#define __OVERLAY_DTABLE_H

#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>

#ifndef __cplusplus
#error overlay_dtable.h is a C++ header file
#endif

#include "dtable.h"

/* The overlay dtable just combines underlying dtables in the order specified.
 * Note that it does not propagate blob comparators to them, but it does need
 * its own blob comparator set if one is in use by the underlying dtables. */

class overlay_dtable : public dtable
{
public:
	virtual iter * iterator(ATX_OPT) const;
	virtual bool present(const dtype & key, bool * found, ATX_OPT) const;
	virtual blob lookup(const dtype & key, bool * found, ATX_OPT) const;
	
	virtual int set_blob_cmp(const blob_comparator * cmp);
	
	inline overlay_dtable() : tables(NULL), table_count(0) {}
	int init(dtable * dt1, ...);
	int init(dtable ** dts, size_t count);
	/* overlay_dtable has a public destructor (and no factory) */
	inline virtual ~overlay_dtable()
	{
		if(tables)
			deinit();
	}
	
protected:
	void deinit();
	
private:
	class iter : public iter_source<overlay_dtable>
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
		virtual metablob meta() const;
		virtual blob value() const;
		virtual const dtable * source() const;
		inline iter(const overlay_dtable * source);
		virtual ~iter();
		
	private:
		struct sub
		{
			dtable::iter * iter;
			bool empty, valid, shadow;
			dtype key;
			inline sub() : key(0u) {}
		};
		
		sub * subs;
		size_t current_index;
		enum direction {FORWARD, BACKWARD} lastdir;
		bool past_beginning;
	};
	
	dtable ** tables;
	size_t table_count;
};

#endif /* __OVERLAY_DTABLE_H */
