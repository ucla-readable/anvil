/* This file is part of Toilet. Toilet is copyright 2007-2008 The Regents
 * of the University of California. It is distributed under the terms of
 * version 2 of the GNU GPL. See the file LICENSE for details. */

#ifndef __DTABLE_H
#define __DTABLE_H

#ifndef __cplusplus
#error dtable.h is a C++ header file
#endif

#include "blob.h"
#include "dtype.h"

/* data tables */

class dtable;

class dtable_iter
{
public:
	virtual bool valid() const = 0;
	/* Since these iterators are virtual, we will have a pointer to them
	 * rather than an actual instance when we're using them. As a result, it
	 * is not as useful to override operators, because we'd have to
	 * dereference the local variable in order to use the overloaded
	 * operators. In particular we'd need ++*it instead of just ++it, yet
	 * both would compile without error. So, we use next() here. */
	virtual bool next() = 0;
	virtual dtype key() const = 0;
	virtual metablob meta() const = 0;
	virtual blob value() const = 0;
	virtual const dtable * source() const = 0;
	virtual ~dtable_iter() {}
};

class dtable_factory
{
public:
	virtual dtable * open(int dfd, const char * name) const = 0;
	/* non-existent entries in the source which are present in the shadow
	 * (as existent entries) will be kept as non-existent entries in the
	 * result, otherwise they will be omitted since they are not needed */
	/* shadow may also be NULL in which case it is treated as empty */
	virtual int create(int dfd, const char * name, const dtable * source, const dtable * shadow) const = 0;
	virtual ~dtable_factory() {}
};

template<class T>
class dtable_static_factory : public dtable_factory
{
public:
	virtual dtable * open(int dfd, const char * name) const
	{
		T * disk = new T;
		int r = disk->init(dfd, name);
		if(r < 0)
		{
			delete disk;
			disk = NULL;
		}
		return disk;
	}
	
	virtual int create(int dfd, const char * name, const dtable * source, const dtable * shadow) const
	{
		return T::create(dfd, name, source, shadow);
	}
};

class dtable
{
public:
	virtual dtable_iter * iterator() const = 0;
	virtual blob lookup(dtype key, const dtable ** source) const = 0;
	inline blob find(dtype key) const { const dtable * source; return lookup(key, &source); }
	inline dtype::ctype key_type() const { return ktype; }
	inline virtual ~dtable() {}
	
protected:
	dtype::ctype ktype;
};

class writable_dtable : virtual public dtable
{
public:
	virtual int append(dtype key, const blob & blob) = 0;
	virtual int remove(dtype key) = 0;
	inline virtual ~writable_dtable() {}
};

#endif /* __DTABLE_H */
