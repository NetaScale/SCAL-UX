/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/**
 * @file defs.h
 * @brief Miscellaneous definitions used by kernel objects.
 */

#ifndef LIBKERN_DEFS_H_
#define LIBKERN_DEFS_H_

/**
 * @name Reference-counting annotations
 * @{
 */

/**
 * These are purely informative for now; GCC doesn't support them.
 *
 * These explicit attributes supplement the system of conventions on the
 * reference-counting behaviour of method/functions according to their name:
 *  - Functions named *alloc, *copy, or *new return a retained object.
 *  - Functions named *get return an unretained object.
 */

/** Marks a parameter as being retained by the function. */
#define GEN_RETAIN
/** Marks a parameter as being released by the function. */
#define GEN_RELEASE
/**
 * Mark a returned object as retaining a reference for the caller. The caller is
 * responsible for the release of this reference.
 */
#define GEN_RETURNS_RETAINED
/**
 * Mark a returned object as not retaining a reference for the caller.
 * The caller is NOT responsible for releasing the reference.
 * The safe use of the object is still provided for by the reference held on the
 * object storing the returned object.
 */
#define GEN_RETURNS_UNRETAINED
/**
 * Marks an object as having its callers' reference consumed by the callee.
 * The caller is no longer responsible for releasing the reference; but it can
 * safely continue to use the object as it is now retained by another object on
 * which the callee operates (this is the distinction from GEN_RELEASE.)
 */
#define GEN_CONSUMES

/** @} */

/**
 * @name Locking annotations
 * @{
 */

/**
 * These are also purely informative for now; GCC doesn't support them, Clang
 * does, but only for C++.
 */

#if defined(__clang__)
#define THREAD_ATTR(x) __attribute__((x))
#else
#define THREAD_ATTR(x)
#endif

/** Marks the structure as defining a capability. */
#define CAPABILITY(x) THREAD_ATTR(capability(x))
/** Marks access to a variable as being guarded by the given capabilities. */
#define GUARDED_BY(x) THREAD_ATTR(guarded_by(x))
/**
 * Marks a function/method as requiring that exclusive access is held to the
 * given capabilities.
 */
#define LOCK_REQUIRES(...) THREAD_ATTR(requires_capability(__VA_ARGS__))
/** Marks a function/method as exclusively acquiring the given capability. */
#define LOCK_ACQUIRE(...) THREAD_ATTR(acquire_capability(__VA_ARGS__))
/** Marks a function/method as releasing the given capability. */
#define LOCK_RELEASE(...) THREAD_ATTR(release_capability(__VA_ARGS__))

/** @} */

#endif /* LIBKERN_DEFS_H_ */
