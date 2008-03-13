/* This file is part of Toilet. Toilet is copyright 2007-2008 The Regents
 * of the University of California. It is distributed under the terms of
 * version 2 of the GNU GPL. See the file LICENSE for details. */

#ifndef __ITABLE_H
#define __ITABLE_H

#include <limits.h>
#include <unistd.h>
#include <sys/types.h>

#ifndef __cplusplus
#error itable.h is a C++ header file
#endif

/* itables are immutable on-disk maps from two keys (the primary key and the
 * secondary key) to an offset. The keys can be either integers or strings; the
 * offsets are of type off_t but may be stored more compactly. The itable's data
 * is sorted on disk first by the primary key and then by the secondary key. */

typedef unsigned int iv_int;
#define IV_INT_MIN 0
#define IV_INT_MAX UINT_MAX

/* This is the abstract base class of several different kinds of itables. Aside
 * from the obvious implementation that reads a single file storing all the
 * data, there is also a layering itable that allows one itable to overlay
 * another, or for atables (append-only tables) to overlay itables. */

class itable
{
public:
	enum ktype { NONE, INT, STRING };
	
	/* get the types of the keys */
	virtual inline ktype k1_type();
	virtual inline ktype k2_type();
	
	/* test whether there is an entry for the given key */
	virtual bool has(iv_int k1) = 0;
	virtual bool has(const char * k1) = 0;
	
	virtual bool has(iv_int k1, iv_int k2) = 0;
	virtual bool has(iv_int k1, const char * k2) = 0;
	virtual bool has(const char * k1, iv_int k2) = 0;
	virtual bool has(const char * k1, const char * k2) = 0;
	
	/* get the offset for the given key */
	virtual off_t get(iv_int k1, iv_int k2) = 0;
	virtual off_t get(iv_int k1, const char * k2) = 0;
	virtual off_t get(const char * k1, iv_int k2) = 0;
	virtual off_t get(const char * k1, const char * k2) = 0;
	
	/* get the next key >= the given key */
	virtual int next(iv_int k1, iv_int * key) = 0;
	virtual int next(const char * k1, const char ** key) = 0;
	
	virtual int next(iv_int k1, iv_int k2, iv_int * key) = 0;
	virtual int next(iv_int k1, const char * k2, const char ** key) = 0;
	virtual int next(const char * k1, iv_int k2, iv_int * key) = 0;
	virtual int next(const char * k1, const char * k2, const char ** key) = 0;
	
	inline itable();
	inline virtual ~itable();
	
protected:
	ktype k1t, k2t;
};

class itable_disk : public itable
{
public:
	/* test whether there is an entry for the given key */
	virtual bool has(iv_int k1);
	virtual bool has(const char * k1);
	
	virtual bool has(iv_int k1, iv_int k2);
	virtual bool has(iv_int k1, const char * k2);
	virtual bool has(const char * k1, iv_int k2);
	virtual bool has(const char * k1, const char * k2);
	
	/* get the offset for the given key */
	virtual off_t get(iv_int k1, iv_int k2);
	virtual off_t get(iv_int k1, const char * k2);
	virtual off_t get(const char * k1, iv_int k2);
	virtual off_t get(const char * k1, const char * k2);
	
	/* get the next key >= the given key */
	virtual int next(iv_int k1, iv_int * key);
	virtual int next(const char * k1, const char ** key);
	
	virtual int next(iv_int k1, iv_int k2, iv_int * key);
	virtual int next(iv_int k1, const char * k2, const char ** key);
	virtual int next(const char * k1, iv_int k2, iv_int * key);
	virtual int next(const char * k1, const char * k2, const char ** key);
	
	inline itable_disk();
	int init(int dfd, const char * file);
	void deinit();
	inline virtual ~itable_disk();
private:
	int fd;
};

/* itable inlines */

inline itable::ktype itable::k1_type()
{
	return k1t;
}

inline itable::ktype itable::k2_type()
{
	return k2t;
}

inline itable::itable()
	: k1t(NONE), k2t(NONE)
{
}

inline itable::~itable()
{
}

/* itable_disk inlines */

inline itable_disk::itable_disk()
	: fd(-1)
{
}

inline itable_disk::~itable_disk()
{
	deinit();
}

#endif /* __ITABLE_H */