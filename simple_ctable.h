/* This file is part of Toilet. Toilet is copyright 2007-2008 The Regents
 * of the University of California. It is distributed under the terms of
 * version 2 of the GNU GPL. See the file LICENSE for details. */

#ifndef __SIMPLE_CTABLE_H
#define __SIMPLE_CTABLE_H

#ifndef __cplusplus
#error simple_ctable.h is a C++ header file
#endif

#include "ctable.h"

class simple_ctable : public ctable
{
public:
	virtual ctable_iter * iterator() const;
	virtual blob find(dtype key, const char * column) const;
	
	inline simple_ctable() {}
	inline int init(dtable * source)
	{
		dt_source = source;
		return 0;
	}
	inline virtual ~simple_ctable() {}
	
private:
	class iter : public ctable_iter
	{
	public:
		virtual bool valid() const;
		virtual bool next();
		virtual dtype key() const;
		virtual const char * column() const;
		virtual blob value() const;
		inline iter(dtable_iter * src);
		virtual ~iter()
		{
			if(columns)
				delete columns;
			delete source;
		}
		
	private:
		dtable_iter * source;
		/* need to keep the row around so its iterator will work */
		sub_blob row;
		sub_blob_iter * columns;
	};
};

#endif /* __SIMPLE_CTABLE_H */