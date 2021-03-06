/*
 * CHMPX
 *
 * Copyright 2014 Yahoo! JAPAN corporation.
 *
 * CHMPX is inprocess data exchange by MQ with consistent hashing.
 * CHMPX is made for the purpose of the construction of
 * original messaging system and the offer of the client
 * library.
 * CHMPX transfers messages between the client and the server/
 * slave. CHMPX based servers are dispersed by consistent
 * hashing and are automatically layouted. As a result, it
 * provides a high performance, a high scalability.
 *
 * For the full copyright and license information, please view
 * the LICENSE file that was distributed with this source code.
 *
 * AUTHOR:   Takeshi Nakatani
 * CREATE:   Tue July 1 2014
 * REVISION:
 *
 */
#ifndef	CHMCOMMON_H
#define	CHMCOMMON_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

//---------------------------------------------------------
// Macros for compiler
//---------------------------------------------------------
#ifndef	CHMPX_NOWEAK
#define	CHMPX_ATTR_WEAK				__attribute__ ((weak,unused))
#else
#define	CHMPX_ATTR_WEAK
#endif

#ifndef	CHMPX_NOPADDING
#define	CHMPX_ATTR_PACKED			__attribute__ ((packed))
#else
#define	CHMPX_ATTR_PACKED
#endif

#if defined(__cplusplus)
#define	DECL_EXTERN_C_START			extern "C" {
#define	DECL_EXTERN_C_END			}
#else	// __cplusplus
#define	DECL_EXTERN_C_START
#define	DECL_EXTERN_C_END
#endif	// __cplusplus

//---------------------------------------------------------
// Templates & macros
//---------------------------------------------------------
#if defined(__cplusplus)
template<typename T> inline bool CHMEMPTYSTR(const T& pstr)
{
	return (NULL == (pstr) || '\0' == *(pstr)) ? true : false;
}
#else	// __cplusplus
#define	CHMEMPTYSTR(pstr)	(NULL == (pstr) || '\0' == *(pstr))
#endif	// __cplusplus

#define CHMPXSTRJOIN(first, second)			first ## second

#if defined(__cplusplus)
#define	CHM_OFFSET(baseaddr, offset, type)	(offset ? reinterpret_cast<type>(reinterpret_cast<off_t>(baseaddr) + offset) : reinterpret_cast<type>(baseaddr)) 	// convert pointer with offset
#define	CHM_ABS(baseaddr, offset, type)		(offset ? reinterpret_cast<type>(reinterpret_cast<off_t>(baseaddr) + reinterpret_cast<off_t>(offset)) : 0) 			// To Absorute address
#define	CHM_REL(baseaddr, address, type)	(address ? reinterpret_cast<type>(reinterpret_cast<off_t>(address) - reinterpret_cast<off_t>(baseaddr)) : 0)		// To Relative address
#else	// __cplusplus
#define	CHM_OFFSET(baseaddr, offset, type)	(offset ? (type)((off_t)baseaddr + offset) : (type)baseaddr) 	// convert pointer with offset
#define	CHM_ABS(baseaddr, offset, type)		(offset ? (type)((off_t)baseaddr + (off_t)offset) : 0) 			// To Absorute address
#define	CHM_REL(baseaddr, address, type)	(address ? (type)((off_t)address - (off_t)baseaddr) : 0)		// To Relative address
#endif	// __cplusplus

//---------------------------------------------------------
// Symbols
//---------------------------------------------------------
#define	CHM_INVALID_HANDLE			(-1)
#define	CHM_INVALID_SOCK			(-1)
#define	CHM_INVALID_TID				0
#define	CHM_MAX_PATH_LEN			1024

#if defined(__cplusplus)
#define	CHM_INVALID_CHMPXHANDLE		static_cast<uint64_t>(CHM_INVALID_HANDLE)
#else	// __cplusplus
#define	CHM_INVALID_CHMPXHANDLE		(uint64_t)(CHM_INVALID_HANDLE)
#endif	// __cplusplus

//---------------------------------------------------------
// For endian
//---------------------------------------------------------
#ifndef	_BSD_SOURCE
#define _BSD_SOURCE
#define	SET_LOCAL_BSD_SOURCE	1
#endif

#ifdef	HAVE_ENDIAN_H
#include <endian.h>
#else
#ifdef	HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#endif
#endif

#ifdef	SET_LOCAL_BSD_SOURCE
#undef _BSD_SOURCE
#endif

//---------------------------------------------------------
// Compatibility
//---------------------------------------------------------
// For clock_gettime
#ifndef	CLOCK_BOOTTIME
#define	CLOCK_BOOTTIME		CLOCK_MONOTONIC
#endif

//---------------------------------------------------------
// types
//---------------------------------------------------------
#define	__STDC_FORMAT_MACROS
#include <inttypes.h>

#endif	// CHMCOMMON_H

/*
 * VIM modelines
 *
 * vim:set ts=4 fenc=utf-8:
 */
