/* This file is a part of NXVM project. */

#define NXVM_DEBUG_VCPUINS

#include "stdio.h"

#include "vcpu.h"
#include "vram.h"
#include "vcpuins.h"
#include "vpic.h"
#include "system/vapi.h"

#ifdef NXVM_DEBUG_VCPUINS
// NOTE: INT_I8() is modified for the INT test. Please correct it finally!
// NOTE: Need to modify the INT processor! All INTs should call INT(t_nubit8 intid);
#endif

#define MOD	((modrm&0xc0)>>6)
#define REG	((modrm&0x38)>>3)
#define RM	((modrm&0x07)>>0)

#define ADD_FLAG	(VCPU_FLAG_OF | VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_AF | VCPU_FLAG_CF | VCPU_FLAG_PF)
#define	 OR_FLAG	(VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_PF)
#define ADC_FLAG	(VCPU_FLAG_OF | VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_AF | VCPU_FLAG_CF | VCPU_FLAG_PF)
#define SBB_FLAG	(VCPU_FLAG_OF | VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_AF | VCPU_FLAG_CF | VCPU_FLAG_PF)
#define AND_FLAG	(VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_PF)
#define SUB_FLAG	(VCPU_FLAG_OF | VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_AF | VCPU_FLAG_CF | VCPU_FLAG_PF)
#define XOR_FLAG	(VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_PF)
#define CMP_FLAG	(VCPU_FLAG_OF | VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_AF | VCPU_FLAG_CF | VCPU_FLAG_PF)
#define INC_FLAG	(VCPU_FLAG_OF | VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_AF | VCPU_FLAG_PF)
#define DEC_FLAG	(VCPU_FLAG_OF | VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_AF | VCPU_FLAG_PF)
#define TEST_FLAG	(VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_PF)
#define SHL_FLAG	(VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_PF)
#define SHR_FLAG	(VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_PF)
#define SAL_FLAG	(VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_PF)
#define SAR_FLAG	(VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_PF)
#define AAM_FLAG	(VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_PF)
#define AAD_FLAG	(VCPU_FLAG_SF | VCPU_FLAG_ZF | VCPU_FLAG_PF)

#define GetFlag(flg) (!!(vcpu.flags&flg))
#define SetFlag(flg,bl) ((!!bl)?(vcpu.flags|=flg):(vcpu.flags&=~flg))

#define U_DEST_8	(*(t_nubit8 *)dest)
#define U_DEST_16	(*(t_nubit16 *)dest)
#define U_SRC_8		(*(t_nubit8 *)src)
#define U_SRC_16	(*(t_nubit16 *)src)
#define S_DEST_8	(*(t_nsbit8 *)dest)
#define S_DEST_16	(*(t_nsbit16 *)dest)
#define S_SRC_8		(*(t_nsbit8 *)src)
#define S_SRC_16	(*(t_nsbit16 *)src)
#define MSB_DEST_8	(!!(U_DEST_8&0x80))
#define MSB_DEST_16	(!!(U_DEST_16&0x8000))
#define LSB_DEST_8	(U_DEST_8&0x01)
#define LSB_DEST_16	(U_DEST_16&0x0001)

static t_nubitcc flgoperand1,flgoperand2,flgresult,flglen;

static enum {
	ADD8,ADD16,
	//OR8,OR16,
	ADC8,ADC16,
	SBB8,SBB16,
	//AND8,AND16,
	SUB8,SUB16,
	//XOR8,XOR16,
	CMP8,CMP16
	//TEST8,TEST16
} flginstype;

t_faddrcc vcpuinsInPort[0x10000];	
t_faddrcc vcpuinsOutPort[0x10000];
t_faddrcc vcpuinsInsSet[0x100];

static t_nubit16 insDS;
static t_nubit16 insSS;
static t_vaddrcc rm,r,imm;
static enum {RT_NONE,RT_REPZ,RT_REPZNZ} reptype;
static void CaseError(const char *str)
{
	vapiPrint("The NXVM CPU has encountered an internal case error: %s.\n",str);
	vcputermflag = 1;
}

static void CalcCF()
{
	switch(flginstype) {
	case ADD8:
	case ADD16:
		SetFlag(VCPU_FLAG_CF,(flgresult < flgoperand1) || (flgresult < flgoperand2));
		break;
	case ADC8:
	case ADC16:
		SetFlag(VCPU_FLAG_CF,flgresult <= flgoperand1);
		break;
	case SBB8:
		SetFlag(VCPU_FLAG_CF,(flgoperand1 < flgresult) || (flgoperand2 == 0xff));
		break;
	case SBB16:
		SetFlag(VCPU_FLAG_CF,(flgoperand1 < flgresult) || (flgoperand2 == 0xffff));
		break;
	case SUB8:
	case SUB16:
	case CMP8:
	case CMP16:
		SetFlag(VCPU_FLAG_CF,flgoperand1 < flgoperand2);
		break;
	default:CaseError("CalcCF::flginstype");break;}
}
static void CalcOF()
{
	switch(flginstype) {
	case ADD8:
	case ADC8:
		SetFlag(VCPU_FLAG_OF,((flgoperand1&0x0080) == (flgoperand2&0x0080)) && ((flgoperand1&0x0080) != (flgresult&0x0080)));
		break;
	case ADD16:
	case ADC16:
		SetFlag(VCPU_FLAG_OF,((flgoperand1&0x8000) == (flgoperand2&0x8000)) && ((flgoperand1&0x8000) != (flgresult&0x8000)));
		break;
	case SBB8:
	case SUB8:
	case CMP8:
		SetFlag(VCPU_FLAG_OF,((flgoperand1&0x0080) != (flgoperand2&0x0080)) && ((flgoperand2&0x0080) == (flgresult&0x0080)));
		break;
	case SBB16:
	case SUB16:
	case CMP16:
		SetFlag(VCPU_FLAG_OF,((flgoperand1&0x8000) != (flgoperand2&0x8000)) && ((flgoperand2&0x8000) == (flgresult&0x8000)));
		break;
	default:CaseError("CalcOF::flginstype");break;}
}
static void CalcAF()
{
	SetFlag(VCPU_FLAG_AF,((flgoperand1^flgoperand2)^flgresult)&0x10);
}
static void CalcPF()
{
	t_nubit8 res8 = flgresult & 0xff;
	t_nubitcc count = 0;
	while(res8)
	{
		res8 &= res8-1; 
		count++;
	}
	SetFlag(VCPU_FLAG_PF,!(count%2));
}
static void CalcZF()
{
	SetFlag(VCPU_FLAG_ZF,!flgresult);
}
static void CalcSF()
{
	switch(flglen) {
	case 8:	SetFlag(VCPU_FLAG_SF,!!(flgresult&0x0080));break;
	case 16:SetFlag(VCPU_FLAG_SF,!!(flgresult&0x8000));break;
	default:CaseError("CalcSF::flglen");break;}
}
static void CalcTF() {}
static void CalcIF() {}
static void CalcDF() {}

static void SetFlags(t_nubit16 flags)
{
	if(flags & VCPU_FLAG_CF) CalcCF();
	if(flags & VCPU_FLAG_PF) CalcPF();
	if(flags & VCPU_FLAG_AF) CalcAF();
	if(flags & VCPU_FLAG_ZF) CalcZF();
	if(flags & VCPU_FLAG_SF) CalcSF();
	if(flags & VCPU_FLAG_TF) CalcTF();
	if(flags & VCPU_FLAG_IF) CalcIF();
	if(flags & VCPU_FLAG_DF) CalcDF();
	if(flags & VCPU_FLAG_OF) CalcOF();
}
static void GetMem()
{
	// returns rm
	rm = vramGetAddress(insDS,vramGetWord(vcpu.cs,vcpu.ip));
	vcpu.ip += 2;
}
static void GetImm(t_nubitcc immbit)
{
	// returns imm
	imm = vramGetAddress(vcpu.cs,vcpu.ip);
	switch(immbit) {
	case 8:		vcpu.ip += 1;break;
	case 16:	vcpu.ip += 2;break;
	case 32:	vcpu.ip += 4;break;
	default:CaseError("GetImm::immbit");break;}
}
static void GetModRegRM(t_nubitcc regbit,t_nubitcc rmbit)
{
	// returns rm and r
	t_nubit8 modrm = vramGetByte(vcpu.cs,vcpu.ip++);
	rm = r = (t_vaddrcc)NULL;
	switch(MOD) {
	case 0:
		switch(RM) {
		case 0:	rm = vramGetAddress(insDS,vcpu.bx+vcpu.si);break;
		case 1:	rm = vramGetAddress(insDS,vcpu.bx+vcpu.di);break;
		case 2:	rm = vramGetAddress(insSS,vcpu.bp+vcpu.si);break;
		case 3:	rm = vramGetAddress(insSS,vcpu.bp+vcpu.di);break;
		case 4:	rm = vramGetAddress(insDS,vcpu.si);break;
		case 5:	rm = vramGetAddress(insDS,vcpu.di);break;
		case 6:	rm = vramGetAddress(insDS,vramGetWord(vcpu.cs,vcpu.ip));vcpu.ip += 2;break;
		case 7:	rm = vramGetAddress(insDS,vcpu.bx);break;
		default:CaseError("GetModRegRM::MOD0::RM");break;}
		break;
	case 1:
		switch(RM) {
		case 0:	rm = vramGetAddress(insDS,vcpu.bx+vcpu.si);break;
		case 1:	rm = vramGetAddress(insDS,vcpu.bx+vcpu.di);break;
		case 2:	rm = vramGetAddress(insSS,vcpu.bp+vcpu.si);break;
		case 3:	rm = vramGetAddress(insSS,vcpu.bp+vcpu.di);break;
		case 4:	rm = vramGetAddress(insDS,vcpu.si);break;
		case 5:	rm = vramGetAddress(insDS,vcpu.di);break;
		case 6:	rm = vramGetAddress(insSS,vcpu.bp);break;
		case 7:	rm = vramGetAddress(insDS,vcpu.bx);break;
		default:CaseError("GetModRegRM::MOD1::RM");break;}
		rm += vramGetByte(vcpu.cs,vcpu.ip);vcpu.ip += 1;
		break;
	case 2:
		switch(RM) {
		case 0:	rm = vramGetAddress(insDS,vcpu.bx+vcpu.si);break;
		case 1:	rm = vramGetAddress(insDS,vcpu.bx+vcpu.di);break;
		case 2:	rm = vramGetAddress(insSS,vcpu.bp+vcpu.si);break;
		case 3:	rm = vramGetAddress(insSS,vcpu.bp+vcpu.di);break;
		case 4:	rm = vramGetAddress(insDS,vcpu.si);break;
		case 5:	rm = vramGetAddress(insDS,vcpu.di);break;
		case 6:	rm = vramGetAddress(insSS,vcpu.bp);break;
		case 7:	rm = vramGetAddress(insDS,vcpu.bx);break;
		default:CaseError("GetModRegRM::MOD2::RM");break;}
		rm += vramGetWord(vcpu.cs,vcpu.ip);vcpu.ip += 2;
		break;
	case 3:
		switch(RM) {
		case 0:	if(rmbit == 8) rm = (t_vaddrcc)(&vcpu.al); else rm = (t_vaddrcc)(&vcpu.ax); break;
		case 1:	if(rmbit == 8) rm = (t_vaddrcc)(&vcpu.cl); else rm = (t_vaddrcc)(&vcpu.cx); break;
		case 2:	if(rmbit == 8) rm = (t_vaddrcc)(&vcpu.dl); else rm = (t_vaddrcc)(&vcpu.dx); break;
		case 3:	if(rmbit == 8) rm = (t_vaddrcc)(&vcpu.bl); else rm = (t_vaddrcc)(&vcpu.bx); break;
		case 4:	if(rmbit == 8) rm = (t_vaddrcc)(&vcpu.ah); else rm = (t_vaddrcc)(&vcpu.sp); break;
		case 5:	if(rmbit == 8) rm = (t_vaddrcc)(&vcpu.ch); else rm = (t_vaddrcc)(&vcpu.bp); break;
		case 6:	if(rmbit == 8) rm = (t_vaddrcc)(&vcpu.dh); else rm = (t_vaddrcc)(&vcpu.si); break;
		case 7:	if(rmbit == 8) rm = (t_vaddrcc)(&vcpu.bh); else rm = (t_vaddrcc)(&vcpu.di); break;
		default:CaseError("GetModRegRM::MOD3::RM");break;}
		break;
	default:CaseError("GetModRegRM::MOD");break;}
	switch(regbit) {
	case 0:		r = REG;					break;
	case 4:
		switch(REG) {
		case 0:	r = (t_vaddrcc)(&vcpu.es);	break;
		case 1:	r = (t_vaddrcc)(&vcpu.cs);	break;
		case 2:	r = (t_vaddrcc)(&vcpu.ss);	break;
		case 3:	r = (t_vaddrcc)(&vcpu.ds);	break;
		default:CaseError("GetModRegRM::regbit4::REG");break;}
		break;
	case 8:
		switch(REG) {
		case 0:	r = (t_vaddrcc)(&vcpu.al);	break;
		case 1:	r = (t_vaddrcc)(&vcpu.cl);	break;
		case 2:	r = (t_vaddrcc)(&vcpu.dl);	break;
		case 3:	r = (t_vaddrcc)(&vcpu.bl);	break;
		case 4:	r = (t_vaddrcc)(&vcpu.ah);	break;
		case 5:	r = (t_vaddrcc)(&vcpu.ch);	break;
		case 6:	r = (t_vaddrcc)(&vcpu.dh);	break;
		case 7:	r = (t_vaddrcc)(&vcpu.bh);	break;
		default:CaseError("GetModRegRM::regbit8::REG");break;}
		break;
	case 16:
		switch(REG) {
		case 0: r = (t_vaddrcc)(&vcpu.ax);	break;
		case 1:	r = (t_vaddrcc)(&vcpu.cx);	break;
		case 2:	r = (t_vaddrcc)(&vcpu.dx);	break;
		case 3:	r = (t_vaddrcc)(&vcpu.bx);	break;
		case 4:	r = (t_vaddrcc)(&vcpu.sp);	break;
		case 5:	r = (t_vaddrcc)(&vcpu.bp);	break;
		case 6:	r = (t_vaddrcc)(&vcpu.si);	break;
		case 7: r = (t_vaddrcc)(&vcpu.di);	break;
		default:CaseError("GetModRegRM::regbit16::REG");break;}
		break;
	default:CaseError("GetModRegRM::regbit");break;}
}
static void GetModRegRMEA()
{
	// returns rm and r
	t_nubit8 modrm = vramGetByte(vcpu.cs,vcpu.ip++);
	rm = r = (t_vaddrcc)NULL;
	switch(MOD) {
	case 0:
		switch(RM) {
		case 0:	rm = vcpu.bx+vcpu.si;break;
		case 1:	rm = vcpu.bx+vcpu.di;break;
		case 2:	rm = vcpu.bp+vcpu.si;break;
		case 3:	rm = vcpu.bp+vcpu.di;break;
		case 4:	rm = vcpu.si;break;
		case 5:	rm = vcpu.di;break;
		case 6:	rm = vramGetWord(vcpu.cs,vcpu.ip);vcpu.ip += 2;break;
		case 7:	rm = vcpu.bx;break;
		default:CaseError("GetModRegRMEA::MOD0::RM");break;}
		break;
	case 1:
		switch(RM) {
		case 0:	rm = vcpu.bx+vcpu.si;break;
		case 1:	rm = vcpu.bx+vcpu.di;break;
		case 2:	rm = vcpu.bp+vcpu.si;break;
		case 3:	rm = vcpu.bp+vcpu.di;break;
		case 4:	rm = vcpu.si;break;
		case 5:	rm = vcpu.di;break;
		case 6:	rm = vcpu.bp;break;
		case 7:	rm = vcpu.bx;break;
		default:CaseError("GetModRegRMEA::MOD1::RM");break;}
		rm += vramGetByte(vcpu.cs,vcpu.ip);vcpu.ip += 1;
		break;
	case 2:
		switch(RM) {
		case 0:	rm = vcpu.bx+vcpu.si;break;
		case 1:	rm = vcpu.bx+vcpu.di;break;
		case 2:	rm = vcpu.bp+vcpu.si;break;
		case 3:	rm = vcpu.bp+vcpu.di;break;
		case 4:	rm = vcpu.si;break;
		case 5:	rm = vcpu.di;break;
		case 6:	rm = vramGetAddress(insSS,vcpu.bp);break;
		case 7:	rm = vcpu.bx;break;
		default:CaseError("GetModRegRMEA::MOD2::RM");break;}
		rm += vramGetWord(vcpu.cs,vcpu.ip);vcpu.ip += 2;
		break;
	default:CaseError("GetModRegRMEA::MOD");break;}
	switch(REG) {
	case 0: r = (t_vaddrcc)(&vcpu.ax);	break;
	case 1:	r = (t_vaddrcc)(&vcpu.cx);	break;
	case 2:	r = (t_vaddrcc)(&vcpu.dx);	break;
	case 3:	r = (t_vaddrcc)(&vcpu.bx);	break;
	case 4:	r = (t_vaddrcc)(&vcpu.sp);	break;
	case 5:	r = (t_vaddrcc)(&vcpu.bp);	break;
	case 6:	r = (t_vaddrcc)(&vcpu.si);	break;
	case 7: r = (t_vaddrcc)(&vcpu.di);	break;
	default:CaseError("GetModRegRMEA::REG");break;}
}

static void ADD(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		flginstype = ADD8;
		flgoperand1 = U_DEST_8;
		flgoperand2 = U_SRC_8;
		flgresult = (flgoperand1+flgoperand2)&0xff;
		U_DEST_8 = (t_nubit8)flgresult;
	case 12:
		flglen = 16;
		flginstype = ADD16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = *(t_nsbit8 *)src;
		flgresult = (flgoperand1+flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	case 16:
		flglen = 16;
		flginstype = ADD16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = U_SRC_16;
		flgresult = (flgoperand1+flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	default:CaseError("ADD::len");break;}
	SetFlags(ADD_FLAG);
}
static void OR(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		//flginstype = OR8;
		flgoperand1 = U_DEST_8;
		flgoperand2 = U_SRC_8;
		flgresult = (flgoperand1|flgoperand2)&0xff;
		U_DEST_8 = (t_nubit8)flgresult;
		break;
	case 12:
		flglen = 16;
		//flginstype = OR16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = *(t_nsbit8 *)src;
		flgresult = (flgoperand1|flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	case 16:
		flglen = 16;
		//flginstype = OR16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = U_SRC_16;
		flgresult = (flgoperand1|flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	default:CaseError("OR::len");break;}
	SetFlag(VCPU_FLAG_OF,0);
	SetFlag(VCPU_FLAG_CF,0);
	SetFlag(VCPU_FLAG_AF,0);
	SetFlags(OR_FLAG);
}
static void ADC(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		flginstype = ADC8;
		flgoperand1 = U_DEST_8;
		flgoperand2 = U_SRC_8;
		flgresult = (flgoperand1+flgoperand2+GetFlag(VCPU_FLAG_CF))&0xff;
		U_DEST_8 = (t_nubit8)flgresult;
		break;
	case 12:
		flglen = 16;
		flginstype = ADC16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = *(t_nsbit8 *)src;
		flgresult = (flgoperand1+flgoperand2+GetFlag(VCPU_FLAG_CF))&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	case 16:
		flglen = 16;
		flginstype = ADC16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = U_SRC_16;
		flgresult = (flgoperand1+flgoperand2+GetFlag(VCPU_FLAG_CF))&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	default:CaseError("ADC::len");break;}
	SetFlags(ADC_FLAG);
}
static void SBB(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		flginstype = SBB8;
		flgoperand1 = U_DEST_8;
		flgoperand2 = U_SRC_8;
		flgresult = (flgoperand1-(flgoperand2+GetFlag(VCPU_FLAG_CF)))&0xff;
		U_DEST_8 = (t_nubit8)flgresult;
		break;
	case 12:
		flglen = 16;
		flginstype = SBB16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = *(t_nsbit8 *)src;
		flgresult = (flgoperand1-(flgoperand2+GetFlag(VCPU_FLAG_CF)))&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	case 16:
		flglen = 16;
		flginstype = SBB16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = U_SRC_16;
		flgresult = (flgoperand1-(flgoperand2+GetFlag(VCPU_FLAG_CF)))&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	default:CaseError("SBB::len");break;}
	SetFlags(SBB_FLAG);
}
static void AND(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		//flginstype = AND8;
		flgoperand1 = U_DEST_8;
		flgoperand2 = U_SRC_8;
		flgresult = (flgoperand1&flgoperand2)&0xff;
		U_DEST_8 = (t_nubit8)flgresult;
		break;
	case 12:
		flglen = 16;
		//flginstype = AND16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = *(t_nsbit8 *)src;
		flgresult = (flgoperand1&flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	case 16:
		flglen = 16;
		//flginstype = AND16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = U_SRC_16;
		flgresult = (flgoperand1&flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	default:CaseError("AND::len");break;}
	SetFlag(VCPU_FLAG_OF,0);
	SetFlag(VCPU_FLAG_CF,0);
	SetFlag(VCPU_FLAG_AF,0);
	SetFlags(AND_FLAG);
}
static void SUB(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		flginstype = SUB8;
		flgoperand1 = U_DEST_8;
		flgoperand2 = U_SRC_8;
		flgresult = (flgoperand1-flgoperand2)&0xff;
		U_DEST_8 = (t_nubit8)flgresult;
		break;
	case 12:
		flglen = 16;
		flginstype = SUB16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = S_SRC_8;
		flgresult = (flgoperand1-flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	case 16:
		flglen = 16;
		flginstype = SUB16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = U_SRC_16;
		flgresult = (flgoperand1-flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	default:CaseError("SUB::len");break;}
	SetFlags(SUB_FLAG);
}
static void XOR(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		//flginstype = XOR8;
		flgoperand1 = U_DEST_8;
		flgoperand2 = U_SRC_8;
		flgresult = (flgoperand1^flgoperand2)&0xff;
		U_DEST_8 = (t_nubit8)flgresult;
		break;
	case 12:
		flglen = 16;
		//flginstype = XOR16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = *(t_nsbit8 *)src;
		flgresult = (flgoperand1^flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	case 16:
		flglen = 16;
		//flginstype = XOR16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = U_SRC_16;
		flgresult = (flgoperand1^flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	default:CaseError("XOR::len");break;}
	SetFlag(VCPU_FLAG_OF,0);
	SetFlag(VCPU_FLAG_CF,0);
	SetFlag(VCPU_FLAG_AF,0);
	SetFlags(XOR_FLAG);
}
static void CMP(void *op1, void *op2, t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		flginstype = CMP8;
		flgoperand1 = *(t_nubit8 *)op1;
		flgoperand2 = *(t_nubit8 *)op2;
		flgresult = (flgoperand1-flgoperand2)&0xff;
		break;
	case 12:
		flglen = 16;
		flginstype = CMP16;
		flgoperand1 = *(t_nubit16 *)op1;
		flgoperand2 = *(t_nsbit8 *)op2;
		flgresult = (flgoperand1-flgoperand2)&0xffff;
		break;
	case 16:
		flglen = 16;
		flginstype = CMP16;
		flgoperand1 = *(t_nubit16 *)op1;
		flgoperand2 = *(t_nubit16 *)op2;
		flgresult = (flgoperand1-flgoperand2)&0xffff;
		break;
	default:CaseError("CMP::len");break;}
	SetFlags(CMP_FLAG);
}
static void PUSH(void *src, t_nubit8 len)
{
	switch(len) {
	case 16:
		vcpu.sp -= 2;
		vramSetWord(vcpu.ss,vcpu.sp,U_SRC_16);
		break;
	default:CaseError("PUSH::len");break;}
}
static void POP(void *dest, t_nubit8 len)
{
	switch(len) {
	case 16:
		U_DEST_16 = vramGetWord(vcpu.ss,vcpu.sp);
		vcpu.sp += 2;
		break;
	default:CaseError("POP::len");break;}
}
static void INC(void *dest, t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		flginstype = ADD8;
		flgoperand1 = U_DEST_8;
		flgoperand2 = 0x01;
		flgresult = (flgoperand1+flgoperand2)&0xff;
		U_DEST_8 = (t_nubit8)flgresult;
		break;
	case 16:
		flglen = 16;
		flginstype = ADD16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = 0x0001;
		flgresult = (flgoperand1+flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	default:CaseError("INC::len");break;}
	SetFlags(INC_FLAG);
}
static void DEC(void *dest, t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		flginstype = SUB8;
		flgoperand1 = U_DEST_8;
		flgoperand2 = 0x01;
		flgresult = (flgoperand1-flgoperand2)&0xff;
		U_DEST_8 = (t_nubit8)flgresult;
		break;
	case 16:
		flglen = 16;
		flginstype = SUB16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = 0x0001;
		flgresult = (flgoperand1-flgoperand2)&0xffff;
		U_DEST_16 = (t_nubit16)flgresult;
		break;
	default:CaseError("DEC::len");break;}
	SetFlags(DEC_FLAG);
}
static void JCC(void *src, t_bool jflag,t_nubit8 len)
{
	switch(len) {
	case 8:
		if(jflag)
			vcpu.ip += U_SRC_8;;
		break;
	default:CaseError("JCC::len");break;}
}
static void TEST(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		//flginstype = TEST8;
		flgoperand1 = U_DEST_8;
		flgoperand2 = U_SRC_8;
		flgresult = (flgoperand1&flgoperand2)&0xff;
		break;
	case 16:
		flglen = 16;
		//flginstype = TEST16;
		flgoperand1 = U_DEST_16;
		flgoperand2 = U_SRC_16;
		flgresult = (flgoperand1&flgoperand2)&0xffff;
		break;
	default:CaseError("TEST::len");break;}
	SetFlag(VCPU_FLAG_OF,0);
	SetFlag(VCPU_FLAG_CF,0);
	SetFlag(VCPU_FLAG_AF,0);
	SetFlags(TEST_FLAG);
}
static void XCHG(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		flgoperand1 = U_DEST_8;
		flgoperand2 = U_SRC_8;
		U_DEST_8 = (t_nubit8)flgoperand2;
		U_SRC_8 = (t_nubit8)flgoperand1;
		break;
	case 16:
		flgoperand1 = U_DEST_16;
		flgoperand2 = U_SRC_16;
		U_DEST_16 = (t_nubit16)flgoperand2;
		U_SRC_16 = (t_nubit16)flgoperand1;
		break;
	default:CaseError("XCHG::len");break;}
}
static void MOV(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		U_DEST_8 = U_SRC_8;
		break;
	case 16:
		U_DEST_16 = U_SRC_16;
		break;
	default:CaseError("MOV::len");break;}
}
static void ROL(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	t_bool tempCF;
	if(src) count = U_SRC_8;
	else count = 1;
	tempcount = count;
	switch(len) {
	case 8:
		while(tempcount) {
			tempCF = MSB_DEST_8;
			U_DEST_8 = (U_DEST_8<<1)+(t_nubit8)tempCF;
			tempcount--;
		}
		SetFlag(VCPU_FLAG_CF,LSB_DEST_8);
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_8^GetFlag(VCPU_FLAG_CF));
		break;
	case 16:
		while(tempcount) {
			tempCF = MSB_DEST_16;
			U_DEST_16 = (U_DEST_16<<1)+(t_nubit16)tempCF;
			tempcount--;
		}
		SetFlag(VCPU_FLAG_CF,LSB_DEST_16);
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_16^GetFlag(VCPU_FLAG_CF));
		break;
	default:CaseError("ROL::len");break;}
}
static void ROR(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	t_bool tempCF;
	if(src) count = U_SRC_8;
	else count = 1;
	tempcount = count;
	switch(len) {
	case 8:
		while(tempcount) {
			tempCF = LSB_DEST_8;
			U_DEST_8 >>= 1;
			if(tempCF) U_DEST_8 |= 0x80;
			tempcount--;
		}
		SetFlag(VCPU_FLAG_CF,MSB_DEST_8);
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_8^(!!(U_DEST_8&0x40)));
		break;
	case 16:
		while(tempcount) {
			tempCF = LSB_DEST_16;
			U_DEST_16 >>= 1;
			if(tempCF) U_DEST_16 |= 0x8000;
			tempcount--;
		}
		SetFlag(VCPU_FLAG_CF,MSB_DEST_16);
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_16^(!!(U_DEST_16&0x4000)));
		break;
	default:CaseError("ROR::len");break;}
}
static void RCL(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	t_bool tempCF;
	if(src) count = U_SRC_8;
	else count = 1;
	tempcount = count;
	switch(len) {
	case 8:
		while(tempcount) {
			tempCF = MSB_DEST_8;
			U_DEST_8 = (U_DEST_8<<1)+GetFlag(VCPU_FLAG_CF);
			SetFlag(VCPU_FLAG_CF,tempCF);
			tempcount--;
		}
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_8^GetFlag(VCPU_FLAG_CF));
		break;
	case 16:
		while(tempcount) {
			tempCF = MSB_DEST_16;
			U_DEST_16 = (U_DEST_16<<1)+GetFlag(VCPU_FLAG_CF);
			SetFlag(VCPU_FLAG_CF,tempCF);
			tempcount--;
		}
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_16^GetFlag(VCPU_FLAG_CF));
		break;
	default:CaseError("RCL::len");break;}
}
static void RCR(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	t_bool tempCF;
	if(src) count = U_SRC_8;
	else count = 1;
	tempcount = count;
	switch(len) {
	case 8:
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_8^GetFlag(VCPU_FLAG_CF));
		while(tempcount) {
			tempCF = LSB_DEST_8;
			U_DEST_8 >>= 1;
			if(GetFlag(VCPU_FLAG_CF)) U_DEST_8 |= 0x80;
			SetFlag(VCPU_FLAG_CF,tempCF);
			tempcount--;
		}
		break;
	case 16:
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_16^GetFlag(VCPU_FLAG_CF));
		while(tempcount) {
			tempCF = LSB_DEST_16;
			U_DEST_16 >>= 1;
			if(GetFlag(VCPU_FLAG_CF)) U_DEST_16 |= 0x8000;
			SetFlag(VCPU_FLAG_CF,tempCF);
			tempcount--;
		}
		break;
	default:CaseError("RCR::len");break;}
}
static void SHL(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	if(src) count = U_SRC_8;
	else count = 1;
	switch(len) {
	case 8:
		tempcount = count&0x1f;
		while(tempcount) {
			SetFlag(VCPU_FLAG_CF,MSB_DEST_8);
			U_DEST_8 <<= 1;
			tempcount--;
		}
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_8^GetFlag(VCPU_FLAG_CF));
		else if(count != 0) {
			flgresult = U_DEST_8;
			SetFlags(SHL_FLAG);
		}
		break;
	case 16:
		tempcount = count&0x1f;
		while(tempcount) {
			SetFlag(VCPU_FLAG_CF,MSB_DEST_16);
			U_DEST_16 <<= 1;
			tempcount--;
		}
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_16^GetFlag(VCPU_FLAG_CF));
		else if(count != 0) {
			flgresult = U_DEST_16;
			SetFlags(SHL_FLAG);
		}
		break;
	default:CaseError("SHL::len");break;}
}
static void SHR(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount,tempdest8;
	t_nubit16 tempdest16;
	if(src) count = U_SRC_8;
	else count = 1;
	switch(len) {
	case 8:
		tempcount = count&0x1f;
		tempdest8 = U_DEST_8;
		while(tempcount) {
			SetFlag(VCPU_FLAG_CF,LSB_DEST_8);
			U_DEST_8 >>= 1;
			tempcount--;
		}
		if(count == 1) SetFlag(VCPU_FLAG_OF,!!(tempdest8&0x80));
		else if(count != 0) {
			flgresult = U_DEST_8;
			SetFlags(SHR_FLAG);
		}
		break;
	case 16:
		tempcount = count&0x1f;
		tempdest16 = U_DEST_16;
		while(tempcount) {
			SetFlag(VCPU_FLAG_CF,LSB_DEST_16);
			U_DEST_16 >>= 1;
			tempcount--;
		}
		if(count == 1) SetFlag(VCPU_FLAG_OF,!!(tempdest16&0x8000));
		else if(count != 0) {
			flgresult = U_DEST_16;
			SetFlags(SHR_FLAG);
		}
		break;
	default:CaseError("SHR::len");break;}
}
static void SAL(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	if(src) count = U_SRC_8;
	else count = 1;
	switch(len) {
	case 8:
		tempcount = count&0x1f;
		while(tempcount) {
			SetFlag(VCPU_FLAG_CF,MSB_DEST_8);
			U_DEST_8 <<= 1;
			tempcount--;
		}
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_8^GetFlag(VCPU_FLAG_CF));
		else if(count != 0) {
			flgresult = U_DEST_8;
			SetFlags(SAL_FLAG);
		}
		break;
	case 16:
		tempcount = count&0x1f;
		while(tempcount) {
			SetFlag(VCPU_FLAG_CF,MSB_DEST_16);
			U_DEST_16 <<= 1;
			tempcount--;
		}
		if(count == 1) SetFlag(VCPU_FLAG_OF,MSB_DEST_16^GetFlag(VCPU_FLAG_CF));
		else if(count != 0) {
			flgresult = U_DEST_16;
			SetFlags(SAL_FLAG);
		}
		break;
	default:CaseError("SAL::len");break;}
}
static void SAR(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount,tempdest8;
	t_nubit16 tempdest16;
	if(src) count = U_SRC_8;
	else count = 1;
	switch(len) {
	case 8:
		tempcount = count&0x1f;
		tempdest8 = U_DEST_8;
		while(tempcount) {
			SetFlag(VCPU_FLAG_CF,LSB_DEST_8);
			U_DEST_8 >>= 1;
			U_DEST_8 |= tempdest8&0x80;
			tempcount--;
		}
		if(count == 1) SetFlag(VCPU_FLAG_OF,0);
		else if(count != 0) {
			flgresult = U_DEST_8;
			SetFlags(SAR_FLAG);
		}
		break;
	case 16:
		tempcount = count&0x1f;
		tempdest16 = U_DEST_16;
		while(tempcount) {
			SetFlag(VCPU_FLAG_CF,LSB_DEST_16);
			U_DEST_16 >>= 1;
			U_DEST_16 |= tempdest16&0x8000;
			tempcount--;
		}
		if(count == 1) SetFlag(VCPU_FLAG_OF,0);
		else if(count != 0) {
			flgresult = U_DEST_16;
			SetFlags(SAR_FLAG);
		}
		break;
	default:CaseError("SAR::len");break;}
}
static void STRDIR(t_nubit8 len)
{
	switch(len) {
	case 8:
		if(GetFlag(VCPU_FLAG_DF)) {
			vcpu.di--;
			vcpu.si--;
		} else {
			vcpu.di++;
			vcpu.si++;
		}
		break;
	case 16:
		if(GetFlag(VCPU_FLAG_DF)) {
			vcpu.di -= 2;
			vcpu.si -= 2;
		} else {
			vcpu.di += 2;
			vcpu.si += 2;
		}
		break;
	default:CaseError("STRDIR::len");break;}
}
static void MOVS(t_nubit8 len)
{
	switch(len) {
	case 8:
		vramSetByte(vcpu.es,vcpu.di,vramGetByte(insDS,vcpu.si));
		STRDIR(8);
		/*if (eCPU.di+t<0xc0000 && eCPU.di+t>=0xa0000)
		WriteVideoRam(eCPU.di+t-0xa0000);*/
		//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOVSB\n");
		break;
	case 16:
		vramSetWord(vcpu.es,vcpu.di,vramGetWord(insDS,vcpu.si));
		STRDIR(16);
		/*if (eCPU.di+((t2=eCPU.es,t2<<4))<0xc0000 && eCPU.di+((t2=eCPU.es,t2<<4))>=0xa0000)
		{
			for (i=0;i<tmpOpdSize;i++)
			{
				WriteVideoRam(eCPU.di+((t2=eCPU.es,t2<<4))-0xa0000+i);
			}
		}*/
		//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOVSW\n");
		break;
	default:CaseError("MOVS::len");break;}
}
static void CMPS(t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		flginstype = CMP8;
		flgoperand1 = vramGetByte(insDS,vcpu.si);
		flgoperand2 = vramGetByte(vcpu.es,vcpu.di);
		flgresult = (flgoperand1-flgoperand2)&0xff;
		STRDIR(8);
		SetFlags(CMP_FLAG);
		//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CMPSB\n");
		break;
	case 16:
		flglen = 16;
		flginstype = CMP16;
		flgoperand1 = vramGetWord(insDS,vcpu.si);
		flgoperand2 = vramGetWord(vcpu.es,vcpu.di);
		flgresult = (flgoperand1-flgoperand2)&0xffff;
		STRDIR(16);
		SetFlags(CMP_FLAG);
		//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CMPSW\n");
		break;
	default:CaseError("CMPS::len");break;}
}
static void STOS(t_nubit8 len)
{
	switch(len) {
	case 8:
		vramSetByte(vcpu.es,vcpu.di,vcpu.al);
		STRDIR(8);
		/*if (eCPU.di+t<0xc0000 && eCPU.di+t>=0xa0000)
		WriteVideoRam(eCPU.di+t-0xa0000);*/
		//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  STOSB\n");
		break;
	case 16:
		vramSetWord(vcpu.es,vcpu.di,vcpu.ax);
		STRDIR(16);
		/*if (eCPU.di+((t2=eCPU.es,t2<<4))<0xc0000 && eCPU.di+((t2=eCPU.es,t2<<4))>=0xa0000)
		{
			for (i=0;i<tmpOpdSize;i++)
			{
				WriteVideoRam(eCPU.di+((t2=eCPU.es,t2<<4))-0xa0000+i);
			}
		}*/
		//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  STOSW\n");
		break;
	default:CaseError("STOS::len");break;}
}
static void LODS(t_nubit8 len)
{
	switch(len) {
	case 8:
		vcpu.al = vramGetByte(insDS,vcpu.si);
		STRDIR(8);
		//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  LODSB\n");
		break;
	case 16:
		vcpu.ax = vramGetWord(insDS,vcpu.si);
		STRDIR(16);
		//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  LODSW\n");
		break;
	default:CaseError("LODS::len");break;}
}
static void SCAS(t_nubit8 len)
{
	switch(len) {
	case 8:
		flglen = 8;
		flginstype = CMP8;
		flgoperand1 = vcpu.al;
		flgoperand2 = vramGetByte(vcpu.es,vcpu.di);
		flgresult = (flgoperand1-flgoperand2)&0xff;
		STRDIR(8);
		SetFlags(CMP_FLAG);
		break;
	case 16:
		flglen = 16;
		flginstype = CMP16;
		flgoperand1 = vcpu.ax;
		flgoperand2 = vramGetWord(vcpu.es,vcpu.di);
		flgresult = (flgoperand1-flgoperand2)&0xffff;
		STRDIR(16);
		SetFlags(CMP_FLAG);
		break;
	default:CaseError("SCAS::len");break;}
}
static void NOT(void *dest, t_nubit8 len)
{
	switch(len) {
	case 8:	U_DEST_8 = ~U_DEST_8;break;
	case 16:U_DEST_16 = ~U_DEST_16;break;
	default:CaseError("NOT::len");break;}
}
static void NEG(void *dest, t_nubit8 len)
{
	t_nubitcc zero = 0;
	switch(len) {
	case 8:	SUB((void *)&zero,(void *)dest,8);U_DEST_8 = (t_nubit8)zero;break;
	case 16:SUB((void *)&zero,(void *)dest,16);U_DEST_16 = (t_nubit16)zero;break;
	default:CaseError("NEG::len");break;}
}
static void MUL(void *src, t_nubit8 len)
{
	t_nubit32 tempresult;
	switch(len) {
	case 8:
		vcpu.ax = vcpu.al * U_SRC_8;
		SetFlag(VCPU_FLAG_OF,!!vcpu.ah);
		SetFlag(VCPU_FLAG_CF,!!vcpu.ah);
		break;
	case 16:
		tempresult = vcpu.ax * U_SRC_16;
		vcpu.dx = (tempresult>>16)&0xffff;
		vcpu.ax = tempresult&0xffff;
		SetFlag(VCPU_FLAG_OF,!!vcpu.dx);
		SetFlag(VCPU_FLAG_CF,!!vcpu.dx);
		break;
	default:CaseError("MUL::len");break;}
}
static void IMUL(void *src, t_nubit8 len)
{
	t_nsbit32 tempresult;
	switch(len) {
	case 8:
		vcpu.ax = (t_nsbit8)vcpu.al * S_SRC_8;
		if(vcpu.ax == vcpu.al) {
			SetFlag(VCPU_FLAG_OF,0);
			SetFlag(VCPU_FLAG_CF,0);
		} else {
			SetFlag(VCPU_FLAG_OF,1);
			SetFlag(VCPU_FLAG_CF,1);
		}
		break;
	case 16:
		tempresult = (t_nsbit16)vcpu.ax * S_SRC_16;
		vcpu.dx = (t_nubit16)((tempresult>>16)&0xffff);
		vcpu.ax = (t_nubit16)(tempresult&0xffff);
		if(tempresult == (t_nsbit32)vcpu.ax) {
			SetFlag(VCPU_FLAG_OF,0);
			SetFlag(VCPU_FLAG_CF,0);
		} else {
			SetFlag(VCPU_FLAG_OF,1);
			SetFlag(VCPU_FLAG_CF,1);
		}
		break;
	default:CaseError("IMUL::len");break;}
}
static void DIV(void *src, t_nubit8 len)
{
	t_nubit16 tempAX = vcpu.ax;
	t_nubit32 tempDXAX = (((t_nubit32)vcpu.dx)<<16)+vcpu.ax;
	switch(len) {
	case 8:
		if(U_SRC_8 == 0) {
			vcpu.itnlint = 0;
		} else {
			vcpu.al = (t_nubit8)(tempAX / U_SRC_8);
			vcpu.ah = (t_nubit8)(tempAX % U_SRC_8);
		}
		break;
	case 16:
		if(U_SRC_16 == 0) {
			vcpu.itnlint = 0;
		} else {
			vcpu.ax = (t_nubit16)(tempDXAX / U_SRC_16);
			vcpu.dx = (t_nubit16)(tempDXAX % U_SRC_16);
		}
		break;
	default:CaseError("DIV::len");break;}
}
static void IDIV(void *src, t_nubit8 len)
{
	t_nsbit16 tempAX = vcpu.ax;
	t_nsbit32 tempDXAX = (((t_nubit32)vcpu.dx)<<16)+vcpu.ax;
	switch(len) {
	case 8:
		if(U_SRC_8 == 0) {
			vcpu.itnlint = 0;
		} else {
			vcpu.al = (t_nubit8)(tempAX / S_SRC_8);
			vcpu.ah = (t_nubit8)(tempAX % S_SRC_8);
		}
		break;
	case 16:
		if(U_SRC_16 == 0) {
			vcpu.itnlint = 0;
		} else {
			vcpu.ax = (t_nubit16)(tempDXAX / S_SRC_16);
			vcpu.dx = (t_nubit16)(tempDXAX % S_SRC_16);
		}
		break;
	default:CaseError("IDIV::len");break;}
}
static void INT(t_nubit8 intid)
{
	PUSH((void *)&vcpu.flags,16);
	SetFlag((VCPU_FLAG_IF|VCPU_FLAG_TF|VCPU_FLAG_AF),0);
	PUSH((void *)&vcpu.cs,16);
	PUSH((void *)&vcpu.ip,16);
	vcpu.ip = vramGetWord(0x0000,intid*4+0);
	vcpu.cs = vramGetWord(0x0000,intid*4+2);
}

void OpError()
{
	vapiPrint("The NXVM CPU has encountered an illegal instruction.\nCS:");
	vapiPrintWord(vcpu.cs);
	vapiPrint(" IP:");
	vapiPrintWord(vcpu.ip);
	vapiPrint(" OP:");
	vapiPrintByte(vramGetByte(vcpu.cs,vcpu.ip+0));
	vapiPrint(" ");
	vapiPrintByte(vramGetByte(vcpu.cs,vcpu.ip+1));
	vapiPrint(" ");
	vapiPrintByte(vramGetByte(vcpu.cs,vcpu.ip+2));
	vapiPrint(" ");
	vapiPrintByte(vramGetByte(vcpu.cs,vcpu.ip+3));
	vapiPrint(" ");
	vapiPrintByte(vramGetByte(vcpu.cs,vcpu.ip+4));
	vapiPrint("\n");
	vcputermflag = 1;
}
void IO_NOP()
{
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  IO_NOP\n");
}
void ADD_RM8_R8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	ADD((void *)rm,(void *)r,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADD_RM8_R8\n");
}
void ADD_RM16_R16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	ADD((void *)rm,(void *)r,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADD_RM16_R16\n");
}
void ADD_R8_RM8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	ADD((void *)r,(void *)rm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADD_R8_RM8\n");
}
void ADD_R16_RM16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	ADD((void *)r,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADD_R16_RM16\n");
}
void ADD_AL_I8()
{
	vcpu.ip++;
	GetImm(8);
	ADD((void *)&vcpu.al,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADD_AL_I8\n");
}
void ADD_AX_I16()
{
	vcpu.ip++;
	GetImm(16);
	ADD((void *)&vcpu.ax,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADD_AX_I16\n");
}
void PUSH_ES()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.es,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_ES\n");
}
void POP_ES()
{
	vcpu.ip++;
	POP((void *)&vcpu.es,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_ES\n");
}
void OR_RM8_R8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	OR((void *)rm,(void *)r,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  OR_RM8_R8\n");
}
void OR_RM16_R16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	OR((void *)rm,(void *)r,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  OR_RM16_R16\n");
}
void OR_R8_RM8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	OR((void *)r,(void *)rm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  OR_R8_RM8\n");
}
void OR_R16_RM16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	OR((void *)r,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  OR_R16_RM16\n");
}
void OR_AL_I8()
{
	vcpu.ip++;
	GetImm(8);
	OR((void *)&vcpu.al,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  OR_AL_I8\n");
}
void OR_AX_I16()
{
	vcpu.ip++;
	GetImm(16);
	OR((void *)&vcpu.ax,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  OR_AX_I16\n");
}
void PUSH_CS()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.cs,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_CS\n");
}
void POP_CS()
{
	vcpu.ip++;
	POP((void *)&vcpu.cs,16);
	
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_CS\n");
}
/*void INS_0F()
{//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_0F\n");}*/
void ADC_RM8_R8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	ADC((void *)rm,(void *)r,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADC_RM8_R8\n");
}
void ADC_RM16_R16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	ADC((void *)rm,(void *)r,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADC_RM16_R16\n");
}
void ADC_R8_RM8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	ADC((void *)r,(void *)rm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADC_R8_RM8\n");
}
void ADC_R16_RM16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	ADC((void *)r,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADC_R16_RM16\n");
}
void ADC_AL_I8()
{
	vcpu.ip++;
	GetImm(8);
	ADC((void *)&vcpu.al,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADC_AL_I8\n");
}
void ADC_AX_I16()
{
	vcpu.ip++;
	GetImm(16);
	ADC((void *)&vcpu.ax,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ADC_AX_I16\n");
}
void PUSH_SS()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.ss,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_SS\n");
}
void POP_SS()
{
	vcpu.ip++;
	POP((void *)&vcpu.ss,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_SS\n");
}
void SBB_RM8_R8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	SBB((void *)rm,(void *)r,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SBB_RM8_R8\n");
}
void SBB_RM16_R16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	SBB((void *)rm,(void *)r,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SBB_RM16_R16\n");
}
void SBB_R8_RM8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	SBB((void *)r,(void *)rm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SBB_R8_RM8\n");
}
void SBB_R16_RM16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	SBB((void *)r,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SBB_R16_RM16\n");
}
void SBB_AL_I8()
{
	vcpu.ip++;
	GetImm(8);
	SBB((void *)&vcpu.al,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SBB_AL_I8\n");
}
void SBB_AX_I16()
{
	vcpu.ip++;
	GetImm(16);
	SBB((void *)&vcpu.ax,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SBB_AX_I16\n");
}
void PUSH_DS()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.ds,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_DS\n");
}
void POP_DS()
{
	vcpu.ip++;
	POP((void *)&vcpu.ds,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_DS\n");
}
void AND_RM8_R8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	AND((void *)rm,(void *)r,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  AND_RM8_R8\n");
}
void AND_RM16_R16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	AND((void *)rm,(void *)r,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  AND_RM16_R16\n");
}
void AND_R8_RM8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	AND((void *)r,(void *)rm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  AND_R8_RM8\n");
}
void AND_R16_RM16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	AND((void *)r,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  AND_R16_RM16\n");
}
void AND_AL_I8()
{
	vcpu.ip++;
	GetImm(8);
	AND((void *)&vcpu.al,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  AND_AL_I8\n");
}
void AND_AX_I16()
{
	vcpu.ip++;
	GetImm(16);
	AND((void *)&vcpu.ax,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  AND_AX_I16\n");
}
void ES()
{
	vcpu.ip++;
	insDS = vcpu.es;
	insSS = vcpu.es;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  ES:\n");
}
void DAA()
{
	t_nubit8 oldAL = vcpu.al;
	t_nubit8 newAL = vcpu.al + 0x06;
	vcpu.ip++;
	if(((vcpu.al & 0x0f) > 0x09) || GetFlag(VCPU_FLAG_AF)) {
		vcpu.al = newAL;
		SetFlag(VCPU_FLAG_CF,GetFlag(VCPU_FLAG_CF) || ((newAL < oldAL) || (newAL < 0x06)));
	} else SetFlag(VCPU_FLAG_AF,0);
	if(((vcpu.al & 0xf0) > 0x90) || GetFlag(VCPU_FLAG_CF)) {
		vcpu.al += 0x60;
		SetFlag(VCPU_FLAG_CF,1);
	} else SetFlag(VCPU_FLAG_CF,0);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  DAA\n");
}
void SUB_RM8_R8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	SUB((void *)rm,(void *)r,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SUB_RM8_R8\n");
}
void SUB_RM16_R16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	SUB((void *)rm,(void *)r,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SUB_RM16_R16\n");
}
void SUB_R8_RM8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	SUB((void *)r,(void *)rm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SUB_R8_RM8\n");
}
void SUB_R16_RM16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	SUB((void *)r,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SUB_R16_RM16\n");
}
void SUB_AL_I8()
{
	vcpu.ip++;
	GetImm(8);
	SUB((void *)&vcpu.al,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SUB_AL_I8\n");
}
void SUB_AX_I16()
{
	vcpu.ip++;
	GetImm(16);
	SUB((void *)&vcpu.ax,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SUB_AX_I16\n");
}
void CS()
{
	vcpu.ip++;
	insDS = vcpu.cs;
	insSS = vcpu.cs;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CS:\n");
}
void DAS()
{
	t_nubit8 oldAL = vcpu.al;
	vcpu.ip++;
	if(((vcpu.al & 0x0f) > 0x09) || GetFlag(VCPU_FLAG_AF)) {
		vcpu.al -= 0x06;
		SetFlag(VCPU_FLAG_CF,GetFlag(VCPU_FLAG_CF) || (oldAL < 0x06));
		SetFlag(VCPU_FLAG_AF,1);
	} else SetFlag(VCPU_FLAG_AF,0);
	if((vcpu.al > 0x9f) || GetFlag(VCPU_FLAG_CF)) {
		vcpu.al -= 0x60;
		SetFlag(VCPU_FLAG_CF,1);
	} else SetFlag(VCPU_FLAG_CF,0);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  DAS\n");
}
void XOR_RM8_R8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	XOR((void *)rm,(void *)r,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XOR_RM8_R8\n");
}
void XOR_RM16_R16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	XOR((void *)rm,(void *)r,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XOR_RM16_R16\n");
}
void XOR_R8_RM8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	XOR((void *)r,(void *)rm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XOR_R8_RM8\n");
}
void XOR_R16_RM16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	XOR((void *)r,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XOR_R16_RM16\n");
}
void XOR_AL_I8()
{
	vcpu.ip++;
	GetImm(8);
	XOR((void *)&vcpu.al,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XOR_AL_I8\n");
}
void XOR_AX_I16()
{
	vcpu.ip++;
	GetImm(16);
	XOR((void *)&vcpu.ax,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XOR_AX_I16\n");
}
void SS()
{
	vcpu.ip++;
	insDS = vcpu.ss;
	insSS = vcpu.ss;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SS:\n");
}
void AAA()
{
	vcpu.ip++;
	if(((vcpu.al&0x0f) > 0x09) || GetFlag(VCPU_FLAG_AF)) {
		vcpu.al += 0x06;
		vcpu.ah += 0x01;
		SetFlag(VCPU_FLAG_AF,1);
		SetFlag(VCPU_FLAG_CF,1);
	} else {
		SetFlag(VCPU_FLAG_AF,0);
		SetFlag(VCPU_FLAG_CF,0);
	}
	vcpu.al &= 0x0f;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  AAA\n");
}
void CMP_RM8_R8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	CMP((void *)rm,(void *)r,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CMP_RM8_R8\n");
}
void CMP_RM16_R16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	CMP((void *)rm,(void *)r,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CMP_RM16_R16\n");
}
void CMP_R8_RM8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	CMP((void *)r,(void *)rm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CMP_R8_RM8\n");
}
void CMP_R16_RM16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	CMP((void *)r,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CMP_R16_RM16\n");
}
void CMP_AL_I8()
{
	vcpu.ip++;
	GetImm(8);
	CMP((void *)&vcpu.al,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CMP_AL_I8\n");
}
void CMP_AX_I16()
{
	vcpu.ip++;
	GetImm(16);
	CMP((void *)&vcpu.ax,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CMP_AX_I16\n");
}
void DS()
{
	vcpu.ip++;
	insDS = vcpu.ds;
	insSS = vcpu.ds;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  DS:\n");
}
void AAS()
{
	vcpu.ip++;
	if(((vcpu.al&0x0f) > 0x09) || GetFlag(VCPU_FLAG_AF)) {
		vcpu.al -= 0x06;
		vcpu.ah += 0x01;
		SetFlag(VCPU_FLAG_AF,1);
		SetFlag(VCPU_FLAG_CF,1);
	} else {
		SetFlag(VCPU_FLAG_CF,0);
		SetFlag(VCPU_FLAG_AF,0);
	}
	vcpu.al &= 0x0f;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  AAS\n");
}
void INC_AX()
{
	vcpu.ip++;
	INC((void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INC_AX\n");
}
void INC_CX()
{
	vcpu.ip++;
	INC((void *)&vcpu.cx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INC_CX\n");
}
void INC_DX()
{
	vcpu.ip++;
	INC((void *)&vcpu.dx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INC_DX\n");
}
void INC_BX()
{
	vcpu.ip++;
	INC((void *)&vcpu.bx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INC_BX\n");
}
void INC_SP()
{
	vcpu.ip++;
	INC((void *)&vcpu.sp,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INC_SP\n");
}
void INC_BP()
{
	vcpu.ip++;
	INC((void *)&vcpu.bp,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INC_BP\n");
}
void INC_SI()
{
	vcpu.ip++;
	INC((void *)&vcpu.si,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INC_SI\n");
}
void INC_DI()
{
	vcpu.ip++;
	INC((void *)&vcpu.di,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INC_DI\n");
}
void DEC_AX()
{
	vcpu.ip++;
	DEC((void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  DEC_AX\n");
}
void DEC_CX()
{
	vcpu.ip++;
	DEC((void *)&vcpu.cx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  DEC_CX\n");
}
void DEC_DX()
{
	vcpu.ip++;
	DEC((void *)&vcpu.dx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  DEC_DX\n");
}
void DEC_BX()
{
	vcpu.ip++;
	DEC((void *)&vcpu.bx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  DEC_BX\n");
}
void DEC_SP()
{
	vcpu.ip++;
	DEC((void *)&vcpu.sp,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  DEC_SP\n");
}
void DEC_BP()
{
	vcpu.ip++;
	DEC((void *)&vcpu.bp,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  DEC_BP\n");
}
void DEC_SI()
{
	vcpu.ip++;
	DEC((void *)&vcpu.si,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  DEC_SI\n");
}
void DEC_DI()
{
	vcpu.ip++;
	DEC((void *)&vcpu.di,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  DEC_DI\n");
}
void PUSH_AX()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_AX\n");
}
void PUSH_CX()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.cx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_CX\n");
}
void PUSH_DX()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.dx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_DX\n");
}
void PUSH_BX()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.bx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_BX\n");
}
void PUSH_SP()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.sp,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_SP\n");
}
void PUSH_BP()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.bp,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_BP\n");
}
void PUSH_SI()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.si,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_SI\n");
}
void PUSH_DI()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.di,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_DI\n");
}
void POP_AX()
{
	vcpu.ip++;
	POP((void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_AX\n");
}
void POP_CX()
{
	vcpu.ip++;
	POP((void *)&vcpu.cx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_CX\n");
}
void POP_DX()
{
	vcpu.ip++;
	POP((void *)&vcpu.dx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_DX\n");
}
void POP_BX()
{
	vcpu.ip++;
	POP((void *)&vcpu.bx,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_BX\n");
}
void POP_SP()
{
	vcpu.ip++;
	POP((void *)&vcpu.sp,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_SP\n");
}
void POP_BP()
{
	vcpu.ip++;
	POP((void *)&vcpu.bp,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_BP\n");
}
void POP_SI()
{
	vcpu.ip++;
	POP((void *)&vcpu.si,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_SI\n");
}
void POP_DI()
{
	vcpu.ip++;
	POP((void *)&vcpu.di,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_DI\n");
}
/*void OpdSize()
{//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  OpdSize\n");}
void AddrSize()
{//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  AddrSize\n");}
void PUSH_I16()
{//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSH_I16\n");}*/
void JO()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,GetFlag(VCPU_FLAG_OF),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JO\n");
}
void JNO()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,!GetFlag(VCPU_FLAG_OF),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JNO\n");
}
void JC()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,GetFlag(VCPU_FLAG_CF),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JC\n");
}
void JNC()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,!GetFlag(VCPU_FLAG_CF),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JNC\n");
}
void JZ()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,GetFlag(VCPU_FLAG_ZF),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JZ\n");
}
void JNZ()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,!GetFlag(VCPU_FLAG_ZF),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JNZ\n");
}
void JBE()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,(GetFlag(VCPU_FLAG_CF) || GetFlag(VCPU_FLAG_ZF)),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JBE\n");
}
void JA()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,(!GetFlag(VCPU_FLAG_CF) && !GetFlag(VCPU_FLAG_ZF)),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JA\n");
}
void JS()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,GetFlag(VCPU_FLAG_SF),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JS\n");
}
void JNS()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,!GetFlag(VCPU_FLAG_SF),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JNS\n");
}
void JP()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,GetFlag(VCPU_FLAG_PF),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JP\n");
}
void JNP()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,!GetFlag(VCPU_FLAG_PF),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JNP\n");
}
void JL()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,(GetFlag(VCPU_FLAG_SF) != GetFlag(VCPU_FLAG_OF)),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JL\n");
}
void JNL()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,(GetFlag(VCPU_FLAG_SF) == GetFlag(VCPU_FLAG_OF)),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JNL\n");
}
void JLE()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,((GetFlag(VCPU_FLAG_SF) != GetFlag(VCPU_FLAG_OF)) || GetFlag(VCPU_FLAG_ZF)),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JLE\n");
}
void JG()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void *)imm,((GetFlag(VCPU_FLAG_SF) == GetFlag(VCPU_FLAG_OF)) && !GetFlag(VCPU_FLAG_ZF)),8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JG\n");
}
void INS_80()
{
	vcpu.ip++;
	GetModRegRM(0,8);
	GetImm(8);
	switch(r) {
	case 0:	ADD((void *)rm,(void *)imm,8);break;
	case 1:	OR ((void *)rm,(void *)imm,8);break;
	case 2:	ADC((void *)rm,(void *)imm,8);break;
	case 3:	SBB((void *)rm,(void *)imm,8);break;
	case 4:	AND((void *)rm,(void *)imm,8);break;
	case 5:	SUB((void *)rm,(void *)imm,8);break;
	case 6:	XOR((void *)rm,(void *)imm,8);break;
	case 7:	CMP((void *)rm,(void *)imm,8);break;
	default:CaseError("INS_80::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_80\n");
}
void INS_81()
{
	vcpu.ip++;
	GetModRegRM(0,16);
	GetImm(16);
	switch(r) {
	case 0:	ADD((void *)rm,(void *)imm,16);break;
	case 1:	OR ((void *)rm,(void *)imm,16);break;
	case 2:	ADC((void *)rm,(void *)imm,16);break;
	case 3:	SBB((void *)rm,(void *)imm,16);break;
	case 4:	AND((void *)rm,(void *)imm,16);break;
	case 5:	SUB((void *)rm,(void *)imm,16);break;
	case 6:	XOR((void *)rm,(void *)imm,16);break;
	case 7:	CMP((void *)rm,(void *)imm,16);break;
	default:CaseError("INS_81::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_81\n");
}
void INS_82()
{
	INS_80();
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_82\n");
}
void INS_83()
{
	vcpu.ip++;
	GetModRegRM(0,16);
	GetImm(8);
	switch(r) {
	case 0:	ADD((void *)rm,(void *)imm,12);break;
	case 1:	OR ((void *)rm,(void *)imm,12);break;
	case 2:	ADC((void *)rm,(void *)imm,12);break;
	case 3:	SBB((void *)rm,(void *)imm,12);break;
	case 4:	AND((void *)rm,(void *)imm,12);break;
	case 5:	SUB((void *)rm,(void *)imm,12);break;
	case 6:	XOR((void *)rm,(void *)imm,12);break;
	case 7:	CMP((void *)rm,(void *)imm,12);break;
	default:CaseError("INS_83::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_83\n");
}
void TEST_RM8_R8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	TEST((void *)rm,(void *)r,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  TEST_RM8_R8\n");
}
void TEST_RM16_R16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	TEST((void *)rm,(void *)r,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  TEST_RM16_R16\n");
}
void XCHG_R8_RM8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	XCHG((void *)r,(void *)rm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XCHG_R8_RM8\n");
}
void XCHG_R16_RM16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	XCHG((void *)r,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XCHG_R16_RM16\n");
}
void MOV_RM8_R8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	MOV((void *)rm,(void *)r,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_RM8_R8\n");
}
void MOV_RM16_R16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	MOV((void *)rm,(void *)r,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_RM16_R16\n");
}
void MOV_R8_RM8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	MOV((void *)r,(void *)rm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_R8_RM8\n");
}
void MOV_R16_RM16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	MOV((void *)r,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_R16_RM16\n");
}
void MOV_RM16_SEG()
{
	vcpu.ip++;
	GetModRegRM(4,16);
	MOV((void *)rm,(void *)r,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_RM16_SEG\n");
}
void LEA_R16_M16()
{
	vcpu.ip++;
	GetModRegRMEA();
	*(t_nubit16 *)r = rm&0xffff;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  LEA_R16_M16\n");
}
void MOV_SEG_RM16()
{
	vcpu.ip++;
	GetModRegRM(4,16);
	MOV((void *)r,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_SEG_RM16\n");
}
void POP_RM16()
{
	vcpu.ip++;
	GetModRegRM(0,16);
	switch(r) {
	case 0:	POP((void *)rm,16);
	default:CaseError("POP_RM16::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POP_RM16\n");
}
void NOP()
{
	vcpu.ip++;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  NOP\n");
}
void XCHG_CX_AX()
{
	vcpu.ip++;
	XCHG((void *)&vcpu.cx,(void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XCHG_CX_AX\n");
}
void XCHG_DX_AX()
{
	vcpu.ip++;
	XCHG((void *)&vcpu.dx,(void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XCHG_DX_AX\n");
}
void XCHG_BX_AX()
{
	vcpu.ip++;
	XCHG((void *)&vcpu.bx,(void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XCHG_BX_AX\n");
}
void XCHG_SP_AX()
{
	vcpu.ip++;
	XCHG((void *)&vcpu.sp,(void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XCHG_SP_AX\n");
}
void XCHG_BP_AX()
{
	vcpu.ip++;
	XCHG((void *)&vcpu.bp,(void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XCHG_BP_AX\n");
}
void XCHG_SI_AX()
{
	vcpu.ip++;
	XCHG((void *)&vcpu.si,(void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XCHG_SI_AX\n");
}
void XCHG_DI_AX()
{
	vcpu.ip++;
	XCHG((void *)&vcpu.di,(void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XCHG_DI_AX\n");
}
void CBW()
{
	vcpu.ip++;
	vcpu.ax = (t_nsbit8)vcpu.al;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CBW\n");
}
void CWD()
{
	vcpu.ip++;
	if(vcpu.ax&0x8000) vcpu.dx = 0xffff;
	else vcpu.dx = 0x0000;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CWD\n");
}
void CALL_PTR16_16()
{
	t_nubit16 newcs,newip;
	vcpu.ip++;
	GetImm(16);
	newip = *(t_nubit16 *)imm;
	GetImm(16);
	newcs = *(t_nubit16 *)imm;
	PUSH((void *)&vcpu.cs,16);
	PUSH((void *)&vcpu.ip,16);
	vcpu.ip = newip;
	vcpu.cs = newcs;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CALL_PTR16_16\n");
}
void WAIT()
{
	vcpu.ip++;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  WAIT\n");
}
void PUSHF()
{
	vcpu.ip++;
	PUSH((void *)&vcpu.flags,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  PUSHF\n");
}
void POPF()
{
	vcpu.ip++;
	POP((void *)&vcpu.flags,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  POPF\n");
}
void SAHF()
{
	vcpu.ip++;
	*(t_nubit8 *)&vcpu.flags = vcpu.ah;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  SAHF\n");
}
void LAHF()
{
	vcpu.ip++;
	vcpu.ah = *(t_nubit8 *)&vcpu.flags;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  LAHF\n");
}
void MOV_AL_M8()
{
	vcpu.ip++;
	GetMem();
	MOV((void *)&vcpu.al,(void *)rm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_AL_M8\n");
}
void MOV_AX_M16()
{
	vcpu.ip++;
	GetMem();
	MOV((void *)&vcpu.ax,(void *)rm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_AX_M16\n");
}
void MOV_M8_AL()
{
	vcpu.ip++;
	GetMem();
	MOV((void *)rm,(void *)&vcpu.al,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_M8_AL\n");
}
void MOV_M16_AX()
{
	vcpu.ip++;
	GetMem();
	MOV((void *)rm,(void *)&vcpu.ax,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_M16_AX\n");
}
void MOVSB()
{
	vcpu.ip++;
	if(reptype == RT_NONE) MOVS(8);
	else {
		while(vcpu.cx) {
			vcpuinsExecInt();
			MOVS(8);
			vcpu.cx--;
		}
	}
}
void MOVSW()
{
	vcpu.ip++;
	if(reptype == RT_NONE) MOVS(16);
	else {
		while(vcpu.cx) {
			vcpuinsExecInt();
			MOVS(16);
			vcpu.cx--;
		}
	}
}
void CMPSB()
{
	vcpu.ip++;
	if(reptype == RT_NONE) CMPS(8);
	else {
		while(vcpu.cx) {
			vcpuinsExecInt();
			CMPS(8);
			vcpu.cx--;
			if((reptype == RT_REPZ && !GetFlag(VCPU_FLAG_ZF)) || (reptype == RT_REPZNZ && GetFlag(VCPU_FLAG_ZF))) break;
		}
	}
}
void CMPSW()
{
	vcpu.ip++;
	if(reptype == RT_NONE) CMPS(16);
	else {
		while(vcpu.cx) {
			vcpuinsExecInt();
			CMPS(16);
			vcpu.cx--;
			if((reptype == RT_REPZ && !GetFlag(VCPU_FLAG_ZF)) || (reptype == RT_REPZNZ && GetFlag(VCPU_FLAG_ZF))) break;
		}
	}
}
void TEST_AL_I8()
{
	vcpu.ip++;
	GetImm(8);
	TEST((void *)&vcpu.al,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  TEST_AL_I8\n");
}
void TEST_AX_I16()
{
	vcpu.ip++;
	GetImm(16);
	TEST((void *)&vcpu.ax,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  TEST_AX_I16\n");
}
void STOSB()
{
	vcpu.ip++;
	if(reptype == RT_NONE) STOS(8);
	else {
		while(vcpu.cx) {
			vcpuinsExecInt();
			STOS(8);
			vcpu.cx--;
		}
	}
}
void STOSW()
{
	vcpu.ip++;
	if(reptype == RT_NONE) STOS(16);
	else {
		while(vcpu.cx) {
			vcpuinsExecInt();
			STOS(16);
			vcpu.cx--;
		}
	}
}
void LODSB()
{
	vcpu.ip++;
	if(reptype == RT_NONE) LODS(8);
	else {
		while(vcpu.cx) {
			vcpuinsExecInt();
			LODS(8);
			vcpu.cx--;
		}
	}
}
void LODSW()
{
	vcpu.ip++;
	if(reptype == RT_NONE) LODS(16);
	else {
		while(vcpu.cx) {
			vcpuinsExecInt();
			LODS(16);
			vcpu.cx--;
		}
	}
}
void SCASB()
{
	vcpu.ip++;
	if(reptype == RT_NONE) SCAS(8);
	else {
		while(vcpu.cx) {
			vcpuinsExecInt();
			SCAS(8);
			vcpu.cx--;
			if((reptype == RT_REPZ && !GetFlag(VCPU_FLAG_ZF)) || (reptype == RT_REPZNZ && GetFlag(VCPU_FLAG_ZF))) break;
		}
	}
}
void SCASW()
{
	vcpu.ip++;
	if(reptype == RT_NONE) SCAS(16);
	else {
		while(vcpu.cx) {
			vcpuinsExecInt();
			SCAS(16);
			vcpu.cx--;
			if((reptype == RT_REPZ && !GetFlag(VCPU_FLAG_ZF)) || (reptype == RT_REPZNZ && GetFlag(VCPU_FLAG_ZF))) break;
		}
	}
}
void MOV_AL_I8()
{
	vcpu.ip++;
	GetImm(8);
	MOV((void *)&vcpu.al,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_AL_I8\n");
}
void MOV_CL_I8()
{
	vcpu.ip++;
	GetImm(8);
	MOV((void *)&vcpu.cl,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_CL_I8\n");
}
void MOV_DL_I8()
{
	vcpu.ip++;
	GetImm(8);
	MOV((void *)&vcpu.dl,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_DL_I8\n");
}
void MOV_BL_I8()
{
	vcpu.ip++;
	GetImm(8);
	MOV((void *)&vcpu.bl,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_BL_I8\n");
}
void MOV_AH_I8()
{
	vcpu.ip++;
	GetImm(8);
	MOV((void *)&vcpu.ah,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_AH_I8\n");
}
void MOV_CH_I8()
{
	vcpu.ip++;
	GetImm(8);
	MOV((void *)&vcpu.ch,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_CH_I8\n");
}
void MOV_DH_I8()
{
	vcpu.ip++;
	GetImm(8);
	MOV((void *)&vcpu.dh,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_DH_I8\n");
}
void MOV_BH_I8()
{
	vcpu.ip++;
	GetImm(8);
	MOV((void *)&vcpu.bh,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_BH_I8\n");
}
void MOV_AX_I16()
{
	vcpu.ip++;
	GetImm(16);
	MOV((void *)&vcpu.ax,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_AX_I16\n");
}
void MOV_CX_I16()
{
	vcpu.ip++;
	GetImm(16);
	MOV((void *)&vcpu.cx,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_CX_I16\n");
}
void MOV_DX_I16()
{
	vcpu.ip++;
	GetImm(16);
	MOV((void *)&vcpu.dx,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_DX_I16\n");
}
void MOV_BX_I16()
{
	vcpu.ip++;
	GetImm(16);
	MOV((void *)&vcpu.bx,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_BX_I16\n");
}
void MOV_SP_I16()
{
	vcpu.ip++;
	GetImm(16);
	MOV((void *)&vcpu.sp,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_SP_I16\n");
}
void MOV_BP_I16()
{
	vcpu.ip++;
	GetImm(16);
	MOV((void *)&vcpu.bp,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_BP_I16\n");
}
void MOV_SI_I16()
{
	vcpu.ip++;
	GetImm(16);
	MOV((void *)&vcpu.si,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_SI_I16\n");
}
void MOV_DI_I16()
{
	vcpu.ip++;
	GetImm(16);
	MOV((void *)&vcpu.di,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_DI_I16\n");
}
void INS_C0()
{
	vcpu.ip++;
	GetModRegRM(0,8);
	GetImm(8);
	switch(r) {
	case 0:	ROL((void *)rm,(void *)imm,8);break;
	case 1:	ROR((void *)rm,(void *)imm,8);break;
	case 2:	RCL((void *)rm,(void *)imm,8);break;
	case 3:	RCR((void *)rm,(void *)imm,8);break;
	case 4:	SHL((void *)rm,(void *)imm,8);break;
	case 5:	SHR((void *)rm,(void *)imm,8);break;
	case 6:	SAL((void *)rm,(void *)imm,8);break;
	case 7:	SAR((void *)rm,(void *)imm,8);break;
	default:CaseError("INS_C0::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_C0\n");
}
void INS_C1()
{
	vcpu.ip++;
	GetModRegRM(0,16);
	GetImm(8);
	switch(r) {
	case 0:	ROL((void *)rm,(void *)imm,16);break;
	case 1:	ROR((void *)rm,(void *)imm,16);break;
	case 2:	RCL((void *)rm,(void *)imm,16);break;
	case 3:	RCR((void *)rm,(void *)imm,16);break;
	case 4:	SHL((void *)rm,(void *)imm,16);break;
	case 5:	SHR((void *)rm,(void *)imm,16);break;
	case 6:	SAL((void *)rm,(void *)imm,16);break;
	case 7:	SAR((void *)rm,(void *)imm,16);break;
	default:CaseError("INS_C1::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_C1\n");
}
void RET_I8()
{
	t_nubit8 addsp;
	vcpu.ip++;
	GetImm(8);
	addsp = *(t_nubit8 *)imm;
	POP((void *)&vcpu.ip,16);
	vcpu.sp += addsp;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  RET_I8\n");
}
void RET()
{
	POP((void *)&vcpu.ip,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  RET\n");
}
void LES_R16_M16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	MOV((void *)r,(void *)rm,16);
	MOV((void *)&vcpu.es,(void *)(rm+2),16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  LES_R16_M16\n");
}
void LDS_R16_M16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	MOV((void *)r,(void *)rm,16);
	MOV((void *)&vcpu.ds,(void *)(rm+2),16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  LDS_R16_M16\n");
}
void MOV_M8_I8()
{
	vcpu.ip++;
	GetModRegRM(8,8);
	GetImm(8);
	MOV((void *)rm,(void *)imm,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_M8_I8\n");
}
void MOV_M16_I16()
{
	vcpu.ip++;
	GetModRegRM(16,16);
	GetImm(16);
	MOV((void *)rm,(void *)imm,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  MOV_M16_I16\n");
}
void RETF_I16()
{
	t_nubit16 addsp;
	vcpu.ip++;
	GetImm(16);
	addsp = *(t_nubit16 *)imm;
	POP((void *)&vcpu.ip,16);
	POP((void *)&vcpu.cs,16);
	vcpu.sp += addsp;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  RETF_I16\n");
}
void RETF()
{
	POP((void *)&vcpu.ip,16);
	POP((void *)&vcpu.cs,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  RETF\n");
}
void INT3()
{
	vcpu.ip++;
	vcpu.itnlint = 3;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INT3\n");
}
void INT_I8()
{
	vcpu.ip++;
	GetImm(8);
	vcpu.itnlint = *(t_nubit8 *)imm;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INT_I8\n");
}
void INTO()
{
	vcpu.ip++;
	if(GetFlag(VCPU_FLAG_OF)) vcpu.itnlint = 4;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INTO\n");
}
void IRET()
{
	POP((void *)&vcpu.ip,16);
	POP((void *)&vcpu.cs,16);
	POP((void *)&vcpu.flags,16);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  IRET\n");
}
void INS_D0()
{
	vcpu.ip++;
	GetModRegRM(0,8);
	switch(r) {
	case 0:	ROL((void *)rm,NULL,8);break;
	case 1:	ROR((void *)rm,NULL,8);break;
	case 2:	RCL((void *)rm,NULL,8);break;
	case 3:	RCR((void *)rm,NULL,8);break;
	case 4:	SHL((void *)rm,NULL,8);break;
	case 5:	SHR((void *)rm,NULL,8);break;
	case 6:	SAL((void *)rm,NULL,8);break;
	case 7:	SAR((void *)rm,NULL,8);break;
	default:CaseError("INS_D0::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_D0\n");
}
void INS_D1()
{
	vcpu.ip++;
	GetModRegRM(0,16);
	switch(r) {
	case 0:	ROL((void *)rm,NULL,16);break;
	case 1:	ROR((void *)rm,NULL,16);break;
	case 2:	RCL((void *)rm,NULL,16);break;
	case 3:	RCR((void *)rm,NULL,16);break;
	case 4:	SHL((void *)rm,NULL,16);break;
	case 5:	SHR((void *)rm,NULL,16);break;
	case 6:	SAL((void *)rm,NULL,16);break;
	case 7:	SAR((void *)rm,NULL,16);break;
	default:CaseError("INS_D1::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_D1\n");
}
void INS_D2()
{
	vcpu.ip++;
	GetModRegRM(0,8);
	switch(r) {
	case 0:	ROL((void *)rm,(void *)&vcpu.cl,8);break;
	case 1:	ROR((void *)rm,(void *)&vcpu.cl,8);break;
	case 2:	RCL((void *)rm,(void *)&vcpu.cl,8);break;
	case 3:	RCR((void *)rm,(void *)&vcpu.cl,8);break;
	case 4:	SHL((void *)rm,(void *)&vcpu.cl,8);break;
	case 5:	SHR((void *)rm,(void *)&vcpu.cl,8);break;
	case 6:	SAL((void *)rm,(void *)&vcpu.cl,8);break;
	case 7:	SAR((void *)rm,(void *)&vcpu.cl,8);break;
	default:CaseError("INS_D2::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_D2\n");
}
void INS_D3()
{
	vcpu.ip++;
	GetModRegRM(0,16);
	switch(r) {
	case 0:	ROL((void *)rm,(void *)&vcpu.cl,16);break;
	case 1:	ROR((void *)rm,(void *)&vcpu.cl,16);break;
	case 2:	RCL((void *)rm,(void *)&vcpu.cl,16);break;
	case 3:	RCR((void *)rm,(void *)&vcpu.cl,16);break;
	case 4:	SHL((void *)rm,(void *)&vcpu.cl,16);break;
	case 5:	SHR((void *)rm,(void *)&vcpu.cl,16);break;
	case 6:	SAL((void *)rm,(void *)&vcpu.cl,16);break;
	case 7:	SAR((void *)rm,(void *)&vcpu.cl,16);break;
	default:CaseError("INS_D3::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_D3\n");
}
void AAM()
{
	t_nubit8 base,tempAL;
	vcpu.ip++;
	GetImm(8);
	base = *(t_nubit8 *)imm;
	if(base == 0) vcpu.itnlint = 0;
	else {
		tempAL = vcpu.al;
		vcpu.ah = tempAL/base;
		vcpu.al = tempAL%base;
		flgresult = vcpu.al;
		SetFlags(AAM_FLAG);
	}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  AAM\n");
}
void AAD()
{
	t_nubit8 base,tempAL,tempAH;
	vcpu.ip++;
	GetImm(8);
	base = *(t_nubit8 *)imm;
	if(base == 0) vcpu.itnlint = 0;
	else {
		tempAL = vcpu.al;
		tempAH = vcpu.ah;
		vcpu.al = (tempAL+(tempAH*base))&0xff;
		vcpu.ah = 0x00;
		flgresult = vcpu.al;
		SetFlags(AAD_FLAG);
	}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  AAD\n");
}
void XLAT()
{
	vcpu.ip++;
	vcpu.al = vramGetByte(insDS,vcpu.bx+vcpu.al);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  XLAT\n");
}
/*
void INS_D9()
{//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_D9\n");
}
void INS_DB()
{//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_DB\n");
}
*/
void LOOPNZ()
{
	t_nubit8 rel8;
	vcpu.ip++;
	GetImm(8);
	rel8 = *(t_nubit8 *)imm;
	vcpu.cx--;
	if(vcpu.cx && !GetFlag(VCPU_FLAG_ZF)) vcpu.ip += rel8;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  LOOPNZ\n");
}
void LOOPZ()
{
	t_nubit8 rel8;
	vcpu.ip++;
	GetImm(8);
	rel8 = *(t_nubit8 *)imm;
	vcpu.cx--;
	if(vcpu.cx && GetFlag(VCPU_FLAG_ZF)) vcpu.ip += rel8;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  LOOPZ\n");
}
void LOOP()
{
	t_nubit8 rel8;
	vcpu.ip++;
	GetImm(8);
	rel8 = *(t_nubit8 *)imm;
	vcpu.cx--;
	if(vcpu.cx) vcpu.ip += rel8;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  LOOP\n");
}
void JCXZ_REL8()
{
	vcpu.ip++;
	GetImm(8);
	JCC((void*)imm,!vcpu.cx,8);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JCXZ_REL8\n");
}
void IN_AL_I8()
{
	vcpu.ip++;
	GetImm(8);
	FUNEXEC(vcpuinsInPort[*(t_nubit8 *)imm]);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  IN_AL_I8\n");
}
void IN_AX_I8()
{
	t_nubitcc i;
	vcpu.ip++;
	GetImm(8);
	for(i = 0;i < 2;++i) {
		FUNEXEC(vcpuinsInPort[*(t_nubit8 *)imm+i]);
		*(t_nubit8 *)(&vcpu.al+i) = vcpu.al;
	}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  IN_AX_I8\n");
}
void OUT_I8_AL()
{
	vcpu.ip++;
	GetImm(8);
	FUNEXEC(vcpuinsOutPort[*(t_nubit8 *)imm]);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  OUT_I8_AL\n");
}
void OUT_I8_AX()
{
	t_nubitcc i;
	t_nubit8 tempAL = vcpu.al;
	vcpu.ip++;
	GetImm(8);
	for(i = 0;i < 2;++i) {
		vcpu.al = *(t_nubit8 *)(&vcpu.al+i);
		FUNEXEC(vcpuinsOutPort[*(t_nubit8 *)imm+i]);
	}
	vcpu.al = tempAL;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  OUT_I8_AX\n");
}
void CALL_REL16()
{
	vcpu.ip++;
	GetImm(16);
	PUSH((void *)&vcpu.ip,16);
	vcpu.ip += *(t_nubit16 *)imm;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CALL_REL16\n");
}
void JMP_REL16()
{
	vcpu.ip++;
	GetImm(16);
	vcpu.ip += *(t_nubit16 *)imm;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JMP_REL16\n");
}
void JMP_PTR16_16()
{
	t_nubit16 newip,newcs;
	vcpu.ip++;
	GetImm(16);
	newip = *(t_nubit16 *)imm;
	GetImm(16);
	newcs = *(t_nubit16 *)imm;
	vcpu.ip = newip;
	vcpu.cs = newcs;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JMP_PTR16_16\n");
}
void JMP_REL8()
{
	vcpu.ip++;
	GetImm(8);
	vcpu.ip += *(t_nubit8 *)imm;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  JMP_REL8\n");
}
void IN_AL_DX()
{
	vcpu.ip++;
	FUNEXEC(vcpuinsInPort[vcpu.dx]);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  IN_AL_DX\n");
}
void IN_AX_DX()
{
	t_nubitcc i;
	vcpu.ip++;
	for(i = 0;i < 2;++i) {
		FUNEXEC(vcpuinsInPort[vcpu.dx+i]);
		*(t_nubit8 *)(&vcpu.al+i) = vcpu.al;
	}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  IN_AX_DX\n");
}
void OUT_DX_AL()
{
	vcpu.ip++;
	FUNEXEC(vcpuinsOutPort[vcpu.dx]);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  OUT_DX_AL\n");
}
void OUT_DX_AX()
{
	t_nubitcc i;
	t_nubit8 tempAL = vcpu.al;
	vcpu.ip++;
	GetImm(8);
	for(i = 0;i < 2;++i) {
		vcpu.al = *(t_nubit8 *)(&vcpu.al+i);
		FUNEXEC(vcpuinsOutPort[vcpu.dx+i]);
	}
	vcpu.al = tempAL;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  OUT_DX_AX\n");
}
void LOCK()
{
	vcpu.ip++;
	/* Not Implemented */
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  LOCK\n");
}
void REPNZ()
{
	// CMPS,SCAS
	vcpu.ip++;
	reptype = RT_REPZNZ;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  REPNZ\n");
}
void REP()
{	// MOVS,LODS,STOS,CMPS,SCAS
	vcpu.ip++;
	reptype = RT_REPZ;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  REP\n");
}
void HLT()
{
	vcpu.ip++;
	/* Not Implemented */
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  HLT\n");
}
void CMC()
{
	vcpu.ip++;
	vcpu.flags ^= VCPU_FLAG_CF;
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CMC\n");
}
void INS_F6()
{
	vcpu.ip++;
	GetModRegRM(0,8);
	switch(r) {
	case 0:	GetImm(8);
			TEST((void *)rm,(void *)imm,8);
			break;
	case 2:	NOT ((void *)rm,8);	break;
	case 3:	NEG ((void *)rm,8);	break;
	case 4:	MUL ((void *)rm,8);	break;
	case 5:	IMUL((void *)rm,8);	break;
	case 6:	DIV ((void *)rm,8);	break;
	case 7:	IDIV((void *)rm,8);	break;
	default:CaseError("INS_F6::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_F6\n");
}
void INS_F7()
{
	vcpu.ip++;
	GetModRegRM(0,16);
	switch(r) {
	case 0:	GetImm(16);
			TEST((void *)rm,(void *)imm,16);
			break;
	case 2:	NOT ((void *)rm,16);	break;
	case 3:	NEG ((void *)rm,16);	break;
	case 4:	MUL ((void *)rm,16);	break;
	case 5:	IMUL((void *)rm,16);	break;
	case 6:	DIV ((void *)rm,16);	break;
	case 7:	IDIV((void *)rm,16);	break;
	default:CaseError("INS_F7::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_F7\n");
}
void CLC()
{
	vcpu.ip++;
	SetFlag(VCPU_FLAG_CF,0);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CLC\n");
}
void STC()
{
	vcpu.ip++;
	SetFlag(VCPU_FLAG_CF,1);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  STC\n");
}
void CLI()
{
	vcpu.ip++;
	SetFlag(VCPU_FLAG_IF,0);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CLI\n");
}
void STI()
{
	vcpu.ip++;
	SetFlag(VCPU_FLAG_IF,1);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  STI\n");
}
void CLD()
{
	vcpu.ip++;
	SetFlag(VCPU_FLAG_DF,0);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  CLD\n");
}
void STD()
{
	vcpu.ip++;
	SetFlag(VCPU_FLAG_DF,1);
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  STD\n");
}
void INS_FE()
{
	vcpu.ip++;
	GetModRegRM(0,8);
	switch(r) {
	case 0:	INC((void *)rm,8);	break;
	case 1:	DEC((void *)rm,8);	break;
	default:CaseError("INS_FE::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_FE\n");
}
void INS_FF()
{
	vcpu.ip++;
	GetModRegRM(0,16);
	switch(r) {
	case 0:	INC((void *)rm,16);	break;
	case 1:	DEC((void *)rm,16);	break;
	case 2:	/* CALL_RM16 */
		PUSH((void *)&vcpu.ip,16);
		vcpu.ip = *(t_nubit16 *)rm;
		break;
	case 3:	/* CALL_M16_16 */
		PUSH((void *)&vcpu.cs,16);
		PUSH((void *)&vcpu.ip,16);
		vcpu.ip = *(t_nubit16 *)rm;
		vcpu.cs = *(t_nubit16 *)(rm+1);
		break;
	case 4:	/* JMP_RM16 */
		vcpu.ip = *(t_nubit16 *)rm;
		break;
	case 5:	/* JMP_M16_16 */
		vcpu.ip = *(t_nubit16 *)rm;
		vcpu.cs = *(t_nubit16 *)(rm+1);
		break;
	case 6:	/* PUSH_RM16 */
		PUSH((void *)rm,16);
		break;
	default:CaseError("INS_FF::r");break;}
	//vapiPrintAddr(vcpu.cs,vcpu.ip);vapiPrint("  INS_FF\n");
}

t_bool vcpuinsIsPrefix(t_nubit8 opcode)
{
	switch(opcode) {
	case 0xf0: case 0xf2: case 0xf3:
	case 0x2e: case 0x36: case 0x3e: case 0x26:
	//case 0x64: case 0x65: case 0x66: case 0x67:
				return 1;break;
	default:	return 0;break;
	}
}
void vcpuinsClearPrefix()
{
	insDS = vcpu.ds;
	insSS = vcpu.ss;
	reptype = RT_NONE;
}

static void _debug_dosint(t_nubit8 intid)
{// MSDOS INT FOR TEST
	t_nubit16 i;
	t_nubit8 c;
	switch(intid) {
	case 0x20:
		vcputermflag = 1;
		break;
	case 0x21:
		switch(vcpu.ah) {
		case 0x00:
			vcputermflag = 1;
			break;
		case 0x02:
			vapiPrint("%c",vcpu.dl);
			break;
		case 0x09:
			i = 0x0000;
			while((c = vramGetByte(vcpu.ds,vcpu.dx+i)) != '$' && i < 0x0100) {
				i++;
				vapiPrint("%c",c);
			}
			break;
		default:CaseError("_DEBUG_DOSINT::intid0x21::vcpu.ah");break;}
		break;
	default:CaseError("_DEBUG_DOSINT::intid");break;}
}

void vcpuinsExecIns()
{
	t_nubit8 opcode = vramGetByte(vcpu.cs,vcpu.ip);
	FUNEXEC(vcpuinsInsSet[opcode]);
	if(!vcpuinsIsPrefix(opcode)) vcpuinsClearPrefix();
}
void vcpuinsExecInt()
{
	t_nubit8 intr;
	if(vcpu.itnlint != -1) {
#ifdef NXVM_DEBUG_VCPUINS
		if(vcpu.itnlint >= 0x20 && vcpu.itnlint <= 0x3f)
			_debug_dosint((t_nubit8)vcpu.itnlint);
		else
#endif
		INT((t_nubit8)vcpu.itnlint);
	}
	vcpu.itnlint = -1;
	if(vcpu.nmi) INT(0x02);
	vcpu.nmi = 0;
#ifdef NXVM_DEBUG_VCPUINS
	STI();
#endif
	if(GetFlag(VCPU_FLAG_IF) && vpicIsINTR()) {	
		intr = vpicGetINTR();
//#ifndef NXVM_DEBUG_VCPUINS
		INT(intr);
//#endif
		vpicRespondINTR(intr);
		//vapiPrint("m.isr=%x,s.isr=%x\n",vpic1.isr,vpic2.isr);
		//vapiPause();
	}
	if(GetFlag(VCPU_FLAG_TF)) INT(0x01);
}

void CPUInsInit()
{
	int i;
	vcpu.itnlint = -1;
	vcpu.nmi = 0;
	vcpuinsClearPrefix();
	for(i = 0;i < 0x10000;++i) {
		vcpuinsInPort[i] = (t_faddrcc)IO_NOP;
		vcpuinsOutPort[i] = (t_faddrcc)IO_NOP;
	}
	vcpuinsInsSet[0x00] = (t_faddrcc)ADD_RM8_R8;
	vcpuinsInsSet[0x01] = (t_faddrcc)ADD_RM16_R16;
	vcpuinsInsSet[0x02] = (t_faddrcc)ADD_R8_RM8;
	vcpuinsInsSet[0x03] = (t_faddrcc)ADD_R16_RM16;
	vcpuinsInsSet[0x04] = (t_faddrcc)ADD_AL_I8;
	vcpuinsInsSet[0x05] = (t_faddrcc)ADD_AX_I16;
	vcpuinsInsSet[0x06] = (t_faddrcc)PUSH_ES;
	vcpuinsInsSet[0x07] = (t_faddrcc)POP_ES;
	vcpuinsInsSet[0x08] = (t_faddrcc)OR_RM8_R8;
	vcpuinsInsSet[0x09] = (t_faddrcc)OR_RM16_R16;
	vcpuinsInsSet[0x0a] = (t_faddrcc)OR_R8_RM8;
	vcpuinsInsSet[0x0b] = (t_faddrcc)OR_R16_RM16;
	vcpuinsInsSet[0x0c] = (t_faddrcc)OR_AL_I8;
	vcpuinsInsSet[0x0d] = (t_faddrcc)OR_AX_I16;
	vcpuinsInsSet[0x0e] = (t_faddrcc)PUSH_CS;
	vcpuinsInsSet[0x0f] = (t_faddrcc)POP_CS;
	//vcpuinsInsSet[0x0f] = (t_faddrcc)INS_0F;
	vcpuinsInsSet[0x10] = (t_faddrcc)ADC_RM8_R8;
	vcpuinsInsSet[0x11] = (t_faddrcc)ADC_RM16_R16;
	vcpuinsInsSet[0x12] = (t_faddrcc)ADC_R8_RM8;
	vcpuinsInsSet[0x13] = (t_faddrcc)ADC_R16_RM16;
	vcpuinsInsSet[0x14] = (t_faddrcc)ADC_AL_I8;
	vcpuinsInsSet[0x15] = (t_faddrcc)ADC_AX_I16;
	vcpuinsInsSet[0x16] = (t_faddrcc)PUSH_SS;
	vcpuinsInsSet[0x17] = (t_faddrcc)POP_SS;
	vcpuinsInsSet[0x18] = (t_faddrcc)SBB_RM8_R8;
	vcpuinsInsSet[0x19] = (t_faddrcc)SBB_RM16_R16;
	vcpuinsInsSet[0x1a] = (t_faddrcc)SBB_R8_RM8;
	vcpuinsInsSet[0x1b] = (t_faddrcc)SBB_R16_RM16;
	vcpuinsInsSet[0x1c] = (t_faddrcc)SBB_AL_I8;
	vcpuinsInsSet[0x1d] = (t_faddrcc)SBB_AX_I16;
	vcpuinsInsSet[0x1e] = (t_faddrcc)PUSH_DS;
	vcpuinsInsSet[0x1f] = (t_faddrcc)POP_DS;
	vcpuinsInsSet[0x20] = (t_faddrcc)AND_RM8_R8;
	vcpuinsInsSet[0x21] = (t_faddrcc)AND_RM16_R16;
	vcpuinsInsSet[0x22] = (t_faddrcc)AND_R8_RM8;
	vcpuinsInsSet[0x23] = (t_faddrcc)AND_R16_RM16;
	vcpuinsInsSet[0x24] = (t_faddrcc)AND_AL_I8;
	vcpuinsInsSet[0x25] = (t_faddrcc)AND_AX_I16;
	vcpuinsInsSet[0x26] = (t_faddrcc)ES;
	vcpuinsInsSet[0x27] = (t_faddrcc)DAA;
	vcpuinsInsSet[0x28] = (t_faddrcc)SUB_RM8_R8;
	vcpuinsInsSet[0x29] = (t_faddrcc)SUB_RM16_R16;
	vcpuinsInsSet[0x2a] = (t_faddrcc)SUB_R8_RM8;
	vcpuinsInsSet[0x2b] = (t_faddrcc)SUB_R16_RM16;
	vcpuinsInsSet[0x2c] = (t_faddrcc)SUB_AL_I8;
	vcpuinsInsSet[0x2d] = (t_faddrcc)SUB_AX_I16;
	vcpuinsInsSet[0x2e] = (t_faddrcc)CS;
	vcpuinsInsSet[0x2f] = (t_faddrcc)DAS;
	vcpuinsInsSet[0x30] = (t_faddrcc)XOR_RM8_R8;
	vcpuinsInsSet[0x31] = (t_faddrcc)XOR_RM16_R16;
	vcpuinsInsSet[0x32] = (t_faddrcc)XOR_R8_RM8;
	vcpuinsInsSet[0x33] = (t_faddrcc)XOR_R16_RM16;
	vcpuinsInsSet[0x34] = (t_faddrcc)XOR_AL_I8;
	vcpuinsInsSet[0x35] = (t_faddrcc)XOR_AX_I16;
	vcpuinsInsSet[0x36] = (t_faddrcc)SS;
	vcpuinsInsSet[0x37] = (t_faddrcc)AAA;
	vcpuinsInsSet[0x38] = (t_faddrcc)CMP_RM8_R8;
	vcpuinsInsSet[0x39] = (t_faddrcc)CMP_RM16_R16;
	vcpuinsInsSet[0x3a] = (t_faddrcc)CMP_R8_RM8;
	vcpuinsInsSet[0x3b] = (t_faddrcc)CMP_R16_RM16;
	vcpuinsInsSet[0x3c] = (t_faddrcc)CMP_AL_I8;
	vcpuinsInsSet[0x3d] = (t_faddrcc)CMP_AX_I16;
	vcpuinsInsSet[0x3e] = (t_faddrcc)DS;
	vcpuinsInsSet[0x3f] = (t_faddrcc)AAS;
	vcpuinsInsSet[0x40] = (t_faddrcc)INC_AX;
	vcpuinsInsSet[0x41] = (t_faddrcc)INC_CX;
	vcpuinsInsSet[0x42] = (t_faddrcc)INC_DX;
	vcpuinsInsSet[0x43] = (t_faddrcc)INC_BX;
	vcpuinsInsSet[0x44] = (t_faddrcc)INC_SP;
	vcpuinsInsSet[0x45] = (t_faddrcc)INC_BP;
	vcpuinsInsSet[0x46] = (t_faddrcc)INC_SI;
	vcpuinsInsSet[0x47] = (t_faddrcc)INC_DI;
	vcpuinsInsSet[0x48] = (t_faddrcc)DEC_AX;
	vcpuinsInsSet[0x49] = (t_faddrcc)DEC_CX;
	vcpuinsInsSet[0x4a] = (t_faddrcc)DEC_DX;
	vcpuinsInsSet[0x4b] = (t_faddrcc)DEC_BX;
	vcpuinsInsSet[0x4c] = (t_faddrcc)DEC_SP;
	vcpuinsInsSet[0x4d] = (t_faddrcc)DEC_BP;
	vcpuinsInsSet[0x4e] = (t_faddrcc)DEC_SI;
	vcpuinsInsSet[0x4f] = (t_faddrcc)DEC_DI;
	vcpuinsInsSet[0x50] = (t_faddrcc)PUSH_AX;
	vcpuinsInsSet[0x51] = (t_faddrcc)PUSH_CX;
	vcpuinsInsSet[0x52] = (t_faddrcc)PUSH_DX;
	vcpuinsInsSet[0x53] = (t_faddrcc)PUSH_BX;
	vcpuinsInsSet[0x54] = (t_faddrcc)PUSH_SP;
	vcpuinsInsSet[0x55] = (t_faddrcc)PUSH_BP;
	vcpuinsInsSet[0x56] = (t_faddrcc)PUSH_SI;
	vcpuinsInsSet[0x57] = (t_faddrcc)PUSH_DI;
	vcpuinsInsSet[0x58] = (t_faddrcc)POP_AX;
	vcpuinsInsSet[0x59] = (t_faddrcc)POP_CX;
	vcpuinsInsSet[0x5a] = (t_faddrcc)POP_DX;
	vcpuinsInsSet[0x5b] = (t_faddrcc)POP_BX;
	vcpuinsInsSet[0x5c] = (t_faddrcc)POP_SP;
	vcpuinsInsSet[0x5d] = (t_faddrcc)POP_BP;
	vcpuinsInsSet[0x5e] = (t_faddrcc)POP_SI;
	vcpuinsInsSet[0x5f] = (t_faddrcc)POP_DI;
	vcpuinsInsSet[0x60] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x61] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x62] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x63] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x64] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x65] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x66] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x67] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x68] = (t_faddrcc)OpError;
	//vcpuinsInsSet[0x66] = (t_faddrcc)OpdSize;
	//vcpuinsInsSet[0x67] = (t_faddrcc)AddrSize;
	//vcpuinsInsSet[0x68] = (t_faddrcc)PUSH_I16;
	vcpuinsInsSet[0x69] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x6a] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x6b] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x6c] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x6d] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x6e] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x6f] = (t_faddrcc)OpError;
	vcpuinsInsSet[0x70] = (t_faddrcc)JO;
	vcpuinsInsSet[0x71] = (t_faddrcc)JNO;
	vcpuinsInsSet[0x72] = (t_faddrcc)JC;
	vcpuinsInsSet[0x73] = (t_faddrcc)JNC;
	vcpuinsInsSet[0x74] = (t_faddrcc)JZ;
	vcpuinsInsSet[0x75] = (t_faddrcc)JNZ;
	vcpuinsInsSet[0x76] = (t_faddrcc)JBE;
	vcpuinsInsSet[0x77] = (t_faddrcc)JA;
	vcpuinsInsSet[0x78] = (t_faddrcc)JS;
	vcpuinsInsSet[0x79] = (t_faddrcc)JNS;
	vcpuinsInsSet[0x7a] = (t_faddrcc)JP;
	vcpuinsInsSet[0x7b] = (t_faddrcc)JNP;
	vcpuinsInsSet[0x7c] = (t_faddrcc)JL;
	vcpuinsInsSet[0x7d] = (t_faddrcc)JNL;
	vcpuinsInsSet[0x7e] = (t_faddrcc)JLE;
	vcpuinsInsSet[0x7f] = (t_faddrcc)JG;
	vcpuinsInsSet[0x80] = (t_faddrcc)INS_80;
	vcpuinsInsSet[0x81] = (t_faddrcc)INS_81;
	vcpuinsInsSet[0x82] = (t_faddrcc)INS_82;
	vcpuinsInsSet[0x83] = (t_faddrcc)INS_83;
	vcpuinsInsSet[0x84] = (t_faddrcc)TEST_RM8_R8;
	vcpuinsInsSet[0x85] = (t_faddrcc)TEST_RM16_R16;
	vcpuinsInsSet[0x86] = (t_faddrcc)XCHG_R8_RM8;
	vcpuinsInsSet[0x87] = (t_faddrcc)XCHG_R16_RM16;
	vcpuinsInsSet[0x88] = (t_faddrcc)MOV_RM8_R8;
	vcpuinsInsSet[0x89] = (t_faddrcc)MOV_RM16_R16;
	vcpuinsInsSet[0x8a] = (t_faddrcc)MOV_R8_RM8;
	vcpuinsInsSet[0x8b] = (t_faddrcc)MOV_R16_RM16;
	vcpuinsInsSet[0x8c] = (t_faddrcc)MOV_RM16_SEG;
	vcpuinsInsSet[0x8d] = (t_faddrcc)LEA_R16_M16;
	vcpuinsInsSet[0x8e] = (t_faddrcc)MOV_SEG_RM16;
	vcpuinsInsSet[0x8f] = (t_faddrcc)POP_RM16;
	vcpuinsInsSet[0x90] = (t_faddrcc)NOP;
	vcpuinsInsSet[0x91] = (t_faddrcc)XCHG_CX_AX;
	vcpuinsInsSet[0x92] = (t_faddrcc)XCHG_DX_AX;
	vcpuinsInsSet[0x93] = (t_faddrcc)XCHG_BX_AX;
	vcpuinsInsSet[0x94] = (t_faddrcc)XCHG_SP_AX;
	vcpuinsInsSet[0x95] = (t_faddrcc)XCHG_BP_AX;
	vcpuinsInsSet[0x96] = (t_faddrcc)XCHG_SI_AX;
	vcpuinsInsSet[0x97] = (t_faddrcc)XCHG_DI_AX;
	vcpuinsInsSet[0x98] = (t_faddrcc)CBW;
	vcpuinsInsSet[0x99] = (t_faddrcc)CWD;
	vcpuinsInsSet[0x9a] = (t_faddrcc)CALL_PTR16_16;
	vcpuinsInsSet[0x9b] = (t_faddrcc)WAIT;
	vcpuinsInsSet[0x9c] = (t_faddrcc)PUSHF;
	vcpuinsInsSet[0x9d] = (t_faddrcc)POPF;
	vcpuinsInsSet[0x9e] = (t_faddrcc)SAHF;
	vcpuinsInsSet[0x9f] = (t_faddrcc)LAHF;
	vcpuinsInsSet[0xa0] = (t_faddrcc)MOV_AL_M8;
	vcpuinsInsSet[0xa1] = (t_faddrcc)MOV_AX_M16;
	vcpuinsInsSet[0xa2] = (t_faddrcc)MOV_M8_AL;
	vcpuinsInsSet[0xa3] = (t_faddrcc)MOV_M16_AX;
	vcpuinsInsSet[0xa4] = (t_faddrcc)MOVSB;
	vcpuinsInsSet[0xa5] = (t_faddrcc)MOVSW;
	vcpuinsInsSet[0xa6] = (t_faddrcc)CMPSB;
	vcpuinsInsSet[0xa7] = (t_faddrcc)CMPSW;
	vcpuinsInsSet[0xa8] = (t_faddrcc)TEST_AL_I8;
	vcpuinsInsSet[0xa9] = (t_faddrcc)TEST_AX_I16;
	vcpuinsInsSet[0xaa] = (t_faddrcc)STOSB;
	vcpuinsInsSet[0xab] = (t_faddrcc)STOSW;
	vcpuinsInsSet[0xac] = (t_faddrcc)LODSB;
	vcpuinsInsSet[0xad] = (t_faddrcc)LODSW;
	vcpuinsInsSet[0xae] = (t_faddrcc)SCASB;
	vcpuinsInsSet[0xaf] = (t_faddrcc)SCASW;
	vcpuinsInsSet[0xb0] = (t_faddrcc)MOV_AL_I8;
	vcpuinsInsSet[0xb1] = (t_faddrcc)MOV_CL_I8;
	vcpuinsInsSet[0xb2] = (t_faddrcc)MOV_DL_I8;
	vcpuinsInsSet[0xb3] = (t_faddrcc)MOV_BL_I8;
	vcpuinsInsSet[0xb4] = (t_faddrcc)MOV_AH_I8;
	vcpuinsInsSet[0xb5] = (t_faddrcc)MOV_CH_I8;
	vcpuinsInsSet[0xb6] = (t_faddrcc)MOV_DH_I8;
	vcpuinsInsSet[0xb7] = (t_faddrcc)MOV_BH_I8;
	vcpuinsInsSet[0xb8] = (t_faddrcc)MOV_AX_I16;
	vcpuinsInsSet[0xb9] = (t_faddrcc)MOV_CX_I16;
	vcpuinsInsSet[0xba] = (t_faddrcc)MOV_DX_I16;
	vcpuinsInsSet[0xbb] = (t_faddrcc)MOV_BX_I16;
	vcpuinsInsSet[0xbc] = (t_faddrcc)MOV_SP_I16;
	vcpuinsInsSet[0xbd] = (t_faddrcc)MOV_BP_I16;
	vcpuinsInsSet[0xbe] = (t_faddrcc)MOV_SI_I16;
	vcpuinsInsSet[0xbf] = (t_faddrcc)MOV_DI_I16;
	vcpuinsInsSet[0xc0] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xc1] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xc2] = (t_faddrcc)RET_I8;
	vcpuinsInsSet[0xc3] = (t_faddrcc)RET;
	vcpuinsInsSet[0xc4] = (t_faddrcc)LES_R16_M16;
	vcpuinsInsSet[0xc5] = (t_faddrcc)LDS_R16_M16;
	vcpuinsInsSet[0xc6] = (t_faddrcc)MOV_M8_I8;
	vcpuinsInsSet[0xc7] = (t_faddrcc)MOV_M16_I16;
	vcpuinsInsSet[0xc8] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xc9] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xca] = (t_faddrcc)RETF_I16;
	vcpuinsInsSet[0xcb] = (t_faddrcc)RETF;
	vcpuinsInsSet[0xcc] = (t_faddrcc)INT3;
	vcpuinsInsSet[0xcd] = (t_faddrcc)INT_I8;
	vcpuinsInsSet[0xce] = (t_faddrcc)INTO;
	vcpuinsInsSet[0xcf] = (t_faddrcc)IRET;
	vcpuinsInsSet[0xd0] = (t_faddrcc)INS_D0;
	vcpuinsInsSet[0xd1] = (t_faddrcc)INS_D1;
	vcpuinsInsSet[0xd2] = (t_faddrcc)INS_D2;
	vcpuinsInsSet[0xd3] = (t_faddrcc)INS_D3;
	vcpuinsInsSet[0xd4] = (t_faddrcc)AAM;
	vcpuinsInsSet[0xd5] = (t_faddrcc)AAD;
	vcpuinsInsSet[0xd6] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xd7] = (t_faddrcc)XLAT;
	vcpuinsInsSet[0xd8] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xd9] = (t_faddrcc)OpError;
	//vcpuinsInsSet[0xd9] = (t_faddrcc)INS_D9;
	vcpuinsInsSet[0xda] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xdb] = (t_faddrcc)OpError;
	//vcpuinsInsSet[0xdb] = (t_faddrcc)INS_DB;
	vcpuinsInsSet[0xdc] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xdd] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xde] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xdf] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xe0] = (t_faddrcc)LOOPNZ;
	vcpuinsInsSet[0xe1] = (t_faddrcc)LOOPZ;
	vcpuinsInsSet[0xe2] = (t_faddrcc)LOOP;
	vcpuinsInsSet[0xe3] = (t_faddrcc)JCXZ_REL8;
	vcpuinsInsSet[0xe4] = (t_faddrcc)IN_AL_I8;
	vcpuinsInsSet[0xe5] = (t_faddrcc)IN_AX_I8;
	vcpuinsInsSet[0xe6] = (t_faddrcc)OUT_I8_AL;
	vcpuinsInsSet[0xe7] = (t_faddrcc)OUT_I8_AX;
	vcpuinsInsSet[0xe8] = (t_faddrcc)CALL_REL16;
	vcpuinsInsSet[0xe9] = (t_faddrcc)JMP_REL16;
	vcpuinsInsSet[0xea] = (t_faddrcc)JMP_PTR16_16;
	vcpuinsInsSet[0xeb] = (t_faddrcc)JMP_REL8;
	vcpuinsInsSet[0xec] = (t_faddrcc)IN_AL_DX;
	vcpuinsInsSet[0xed] = (t_faddrcc)IN_AX_DX;
	vcpuinsInsSet[0xee] = (t_faddrcc)OUT_DX_AL;
	vcpuinsInsSet[0xef] = (t_faddrcc)OUT_DX_AX;
	vcpuinsInsSet[0xf0] = (t_faddrcc)LOCK;
	vcpuinsInsSet[0xf1] = (t_faddrcc)OpError;
	vcpuinsInsSet[0xf2] = (t_faddrcc)REPNZ;
	vcpuinsInsSet[0xf3] = (t_faddrcc)REP;
	vcpuinsInsSet[0xf4] = (t_faddrcc)HLT;
	vcpuinsInsSet[0xf5] = (t_faddrcc)CMC;
	vcpuinsInsSet[0xf6] = (t_faddrcc)INS_F6;
	vcpuinsInsSet[0xf7] = (t_faddrcc)INS_F7;
	vcpuinsInsSet[0xf8] = (t_faddrcc)CLC;
	vcpuinsInsSet[0xf9] = (t_faddrcc)STC;
	vcpuinsInsSet[0xfa] = (t_faddrcc)CLI;
	vcpuinsInsSet[0xfb] = (t_faddrcc)STI;
	vcpuinsInsSet[0xfc] = (t_faddrcc)CLD;
	vcpuinsInsSet[0xfd] = (t_faddrcc)STD;
	vcpuinsInsSet[0xfe] = (t_faddrcc)INS_FE;
	vcpuinsInsSet[0xff] = (t_faddrcc)INS_FF;
}
void CPUInsTerm() {}