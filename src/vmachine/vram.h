/* This file is a part of NXVM project. */

/* Random-access Memory */

#ifndef NXVM_VRAM_H
#define NXVM_VRAM_H

#ifdef __cplusplus
/*extern "C" {*/
#endif

#include "vglobal.h"

typedef struct {
	t_vaddrcc base;                         /* memory base address is 20 bit */
	t_nubitcc size;                                   /* memory size in byte */
} t_ram;

extern t_ram vram;

#define vramIsAddrInMem(addr) \
	(((addr) >= vram.base) && ((addr) < (vram.base + vram.size)))
#define vramGetAddr(segment, offset)  (vram.base + ((segment) << 4) + (offset))
#define vramVarByte(segment, offset)  (*(t_nubit8  *)(vramGetAddr(segment, offset)))
#define vramVarWord(segment, offset)  (*(t_nubit16 *)(vramGetAddr(segment, offset)))
#define vramVarDWord(segment, offset) (*(t_nubit32 *)(vramGetAddr(segment, offset)))

void vramAlloc(t_nubitcc newsize);
void vramInit();
void vramRefresh();
void vramFinal();

#ifdef __cplusplus
/*}_EOCD_*/
#endif

#endif