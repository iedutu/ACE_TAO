// -*- C++ -*-

//=============================================================================
/**
 *  @file    os_stddef.h
 *
 *  standard type definitions
 *
 *  @author Don Hinton <dhinton@dresystems.com>
 *  @author This code was originally in various places including ace/OS.h.
 */
//=============================================================================

// From http://www.UNIX-systems.org/single_unix_specification/

#ifndef ACE_OS_INCLUDE_OS_STDDEF_H
#define ACE_OS_INCLUDE_OS_STDDEF_H

#include /**/ "ace/pre.h"

#include /**/ "ace/config-lite.h"

#if !defined (ACE_LACKS_PRAGMA_ONCE)
# pragma once
#endif /* ACE_LACKS_PRAGMA_ONCE */

#include /**/ <stddef.h>

// Place all additions (especially function declarations) within extern "C" {}
#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/*
  Integer type whose range of values can represent distinct wide-character
  codes for all members of the largest character set specified among the
  locales supported by the compilation environment: the null character has
  the code value 0 and each member of the portable character set has a code
  value equal to its value when used as the lone character in an integer
  character constant.
*/
#if defined (ACE_LACKS_WCHAR_T)
#  if !defined (ACE_WCHAR_T_TYPE)
#    define ACE_WCHAR_T_TYPE long
#  endif /* !ACE_WCHAR_T_TYPE */
   typedef ACE_WCHAR_T_TYPE wchar_t;
#endif /* ACE_LACKS_WCHAR_T */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#include /**/ "ace/post.h"
#endif /* ACE_OS_INCLUDE_OS_STDDEF_H */
