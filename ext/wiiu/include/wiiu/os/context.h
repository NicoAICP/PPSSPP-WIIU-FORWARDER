#pragma once
#include <wiiu/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OS_CONTEXT_TAG 0x4F53436F6E747874ull

typedef struct OSContext
{
   /*! Should always be set to the value OS_CONTEXT_TAG. */
   uint64_t tag;

   uint32_t gpr[32];
   uint32_t cr;
   uint32_t lr;
   uint32_t ctr;
   uint32_t xer;
   uint32_t srr0;
   uint32_t srr1;
   uint32_t exception_specific0;
   uint32_t exception_specific1;
   uint32_t crash_exception_type;
   uint32_t __unknown[0x2];
   uint32_t fpscr;
   double fpr[32];
   uint16_t spinLockCount;
   uint16_t state;
   uint32_t gqr[8];
   uint32_t __unknown0;
   double psf[32];
   uint64_t coretime[3];
   uint64_t starttime;
   uint32_t error;
   uint32_t __unknown1;
   uint32_t pmc1;
   uint32_t pmc2;
   uint32_t pmc3;
   uint32_t pmc4;
   uint32_t mmcr0;
   uint32_t mmcr1;
} OSContext;

#ifdef __cplusplus
}
#endif
