/* Wraps C++ internal utilities  needed in C
Bertrand A. D.*/

#include "utypes.h"
U_CAPI void  printUChar(const UChar*      uniString);
U_CAPI void  printChar(   const char*         charString);

U_CAPI void T_PlatformUtilities_pathnameInContext( char *fullname, int32_t maxsize, const char * relPath);
U_CAPI const char *T_PlatformUtilities_getDefaultDataDirectory(void);
