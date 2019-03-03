#ifndef __PORT_TYPEDEFS_H
#define __PORT_TYPEDEFS_H

#include "libretro.h"
#include "streams/file_stream_transforms.h"

extern int kNetGame;
extern int bRunPause;

#ifdef USE_CYCLONE
	#define SEK_CORE_C68K (0)
	#define SEK_CORE_M68K (1)
	extern int nSekCpuCore;  // 0 - c68k, 1 - m68k
#endif

#ifndef _MSC_VER
	#include <stdint.h>
	/* fastcall only works on x86_32 */
	#ifndef FASTCALL
		#undef __fastcall
		#define __fastcall
	#else
		#undef __fastcall
		#define __fastcall __attribute__((fastcall))
	#endif
#else
	#undef _UNICODE
	#undef __fastcall
	#define __fastcall
	#include "compat/msvc.h"
	#include "compat/posix_string.h"
#endif

#define _T(x) x
#define _tfopen fopen
#define _stprintf sprintf
#define _tcslen strlen
#define _tcscpy strcpy
#define _tcstol strtol
#define _istspace isspace
#define _tcsncmp strncmp
#define _tcsncpy strncpy
#define _tcsicmp strcasecmp
#define _stricmp strcasecmp
#define stricmp strcasecmp

typedef char TCHAR;

#endif
