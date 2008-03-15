/* This file is part of Toilet. Toilet is copyright 2007-2008 The Regents
 * of the University of California. It is distributed under the terms of
 * version 2 of the GNU GPL. See the file LICENSE for details. */

#ifndef __ITABLE_OVERLAY_H
#define __ITABLE_OVERLAY_H

#include "itable.h"

#ifndef __cplusplus
#error itable_overlay.h is a C++ header file
#endif

class itable_overlay : public itable
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
	
	/* iterate through the offsets: set up iterators */
	virtual int iter(struct it * it);
	virtual int iter(struct it * it, iv_int k1);
	virtual int iter(struct it * it, const char * k1);
	
	/* return 0 for success and < 0 for failure (-ENOENT when done) */
	virtual int next(struct it * it, iv_int * k1, iv_int * k2, off_t * off);
	virtual int next(struct it * it, iv_int * k1, const char ** k2, off_t * off);
	virtual int next(struct it * it, const char ** k1, iv_int * k2, off_t * off);
	virtual int next(struct it * it, const char ** k1, const char ** k2, off_t * off);
	
	inline itable_overlay();
	int init(itable * it1, ...);
	void deinit();
	inline virtual ~itable_overlay();
	
private:
};

inline itable_overlay::itable_overlay()
{
}

inline itable_overlay::~itable_overlay()
{
	deinit();
}

#endif /* __ITABLE_OVERLAY_H */
