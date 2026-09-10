#include "partition_define.h"
static const struct excel_info PartInfo_Private[PART_NUM] = {
	{"preloader",	0x40000,	0x0,		EMMC, 0, EMMC_PART_BOOT1},
	{"mbr",			0x80000,	0x0,		EMMC, 0, EMMC_PART_USER},
	{"ebr1",		0x80000,	0x80000,	EMMC, 1, EMMC_PART_USER},
	{"pro_info",	0x300000,	0x100000,	EMMC, 0, EMMC_PART_USER},
	{"nvram",		0x500000,	0x400000,	EMMC, 0, EMMC_PART_USER},
	{"ztecfg",		0x400000,	0x900000,	EMMC, 0, EMMC_PART_USER},
	{"protect_f",	0xa00000,	0xd00000,	EMMC, 2, EMMC_PART_USER},
	{"protect_s",	0xa00000,	0x1700000,	EMMC, 3, EMMC_PART_USER},
	{"seccfg",		0x20000,	0x2100000,	EMMC, 0, EMMC_PART_USER},
	{"uboot",		0x60000,	0x2120000,	EMMC, 0, EMMC_PART_USER},
	{"bootimg",		0x1000000,	0x2180000,	EMMC, 0, EMMC_PART_USER},
	{"recovery",	0x1000000,	0x3180000,	EMMC, 0, EMMC_PART_USER},
	{"sec_ro",		0x600000,	0x4180000,	EMMC, 4, EMMC_PART_USER},
	{"misc",		0x80000,	0x4780000,	EMMC, 0, EMMC_PART_USER},
	{"logo",		0x300000,	0x4800000,	EMMC, 0, EMMC_PART_USER},
	{"ebr2",		0x80000,	0x4b00000,	EMMC, 0, EMMC_PART_USER},
	{"expdb",		0xa00000,	0x4b80000,	EMMC, 0, EMMC_PART_USER},
	{"tee1",		0x500000,	0x5580000,	EMMC, 0, EMMC_PART_USER},
	{"tee2",		0x500000,	0x5a80000,	EMMC, 0, EMMC_PART_USER},
	{"kb",			0x100000,	0x5f80000,	EMMC, 0, EMMC_PART_USER},
	{"dkb",			0x100000,	0x6080000,	EMMC, 0, EMMC_PART_USER},
	{"android",		0x40000000,	0x6180000,	EMMC, 5, EMMC_PART_USER},
	{"cache",		0x40000000,	0x46180000,	EMMC, 6, EMMC_PART_USER},
	{"usrdata",		0x2f100000,	0x86180000,	EMMC, 7, EMMC_PART_USER},
	{"fat",			0x0,		0xb5280000,	EMMC, 8, EMMC_PART_USER},
	{"bmtpool",		0x1500000,	0xFFFF00a8,	EMMC, 0, EMMC_PART_USER},
 };

#ifdef CONFIG_MTK_EMMC_SUPPORT
struct MBR_EBR_struct MBR_EBR_px[MBR_COUNT]={
	{"mbr", {1, 2, 3, 4, }},
	{"ebr1", {5, 6, 7, }},
	{"ebr2", {8, }},
};

EXPORT_SYMBOL(MBR_EBR_px);
#endif

