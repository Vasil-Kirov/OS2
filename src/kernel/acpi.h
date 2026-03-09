
#ifndef _ACPI_H
#define _ACPI_H

#include <kcommon.h>

typedef struct __attribute__ ((packed)) {
	char Signature[8];
	u8 Checksum;
	char OEMID[6];
	u8 Revision;
	u32 RsdtAddress;      // deprecated since version 2.0

	u32 Length;
	u64 XsdtAddress;
	u8 ExtendedChecksum;
	u8 reserved[3];
} XSDP;

typedef struct __attribute__ ((packed)) {
	char Signature[8];
	u8 Checksum;
	char OEMID[6];
	u8 Revision;
	u32 RsdtAddress;
} RSDP;

typedef struct {
	char Signature[4];
	u32 Length;
	u8 Revision;
	u8 Checksum;
	char OEMID[6];
	char OEMTableID[8];
	u32 OEMRevision;
	u32 CreatorID;
	u32 CreatorRevision;
} ACPISDTHeader;

typedef struct
{
  uint8_t AddressSpace;
  uint8_t BitWidth;
  uint8_t BitOffset;
  uint8_t AccessSize;
  uint64_t Address;
} FADT_GenericAddressStructure;

typedef struct 
{
    ACPISDTHeader h;
    uint32_t FirmwareCtrl;
    uint32_t Dsdt;

    // field used in ACPI 1.0; no longer in use, for compatibility only
    uint8_t  Reserved;

    uint8_t  PreferredPowerManagementProfile;
    uint16_t SCI_Interrupt;
    uint32_t SMI_CommandPort;
    uint8_t  AcpiEnable;
    uint8_t  AcpiDisable;
    uint8_t  S4BIOS_REQ;
    uint8_t  PSTATE_Control;
    uint32_t PM1aEventBlock;
    uint32_t PM1bEventBlock;
    uint32_t PM1aControlBlock;
    uint32_t PM1bControlBlock;
    uint32_t PM2ControlBlock;
    uint32_t PMTimerBlock;
    uint32_t GPE0Block;
    uint32_t GPE1Block;
    uint8_t  PM1EventLength;
    uint8_t  PM1ControlLength;
    uint8_t  PM2ControlLength;
    uint8_t  PMTimerLength;
    uint8_t  GPE0Length;
    uint8_t  GPE1Length;
    uint8_t  GPE1Base;
    uint8_t  CStateControl;
    uint16_t WorstC2Latency;
    uint16_t WorstC3Latency;
    uint16_t FlushSize;
    uint16_t FlushStride;
    uint8_t  DutyOffset;
    uint8_t  DutyWidth;
    uint8_t  DayAlarm;
    uint8_t  MonthAlarm;
    uint8_t  Century;

    // reserved in ACPI 1.0; used since ACPI 2.0+
    uint16_t BootArchitectureFlags;

    uint8_t  Reserved2;
    uint32_t Flags;

    // 12 byte structure; see below for details
    FADT_GenericAddressStructure ResetReg;

    uint8_t  ResetValue;
    uint8_t  Reserved3[3];
  
    // 64bit pointers - Available on ACPI 2.0+
    uint64_t                X_FirmwareControl;
    uint64_t                X_Dsdt;

    FADT_GenericAddressStructure X_PM1aEventBlock;
    FADT_GenericAddressStructure X_PM1bEventBlock;
    FADT_GenericAddressStructure X_PM1aControlBlock;
    FADT_GenericAddressStructure X_PM1bControlBlock;
    FADT_GenericAddressStructure X_PM2ControlBlock;
    FADT_GenericAddressStructure X_PMTimerBlock;
    FADT_GenericAddressStructure X_GPE0Block;
    FADT_GenericAddressStructure X_GPE1Block;
} FADT;

typedef struct {
	u64 base_addr;
	u16 pci_group_number;
	u8 start_bus;
	u8 end_bus;
	u32 reserved;
} MCFG_ConfigSpace;

typedef struct {
	ACPISDTHeader h;
	char reserved[8];
	MCFG_ConfigSpace addrs[0];
} MCFG;

bool rsdp_check_header(RSDP *rsdp);

/*
 * @rsdp - pointer to RSDP struct
 * @table_name - 4 byte identifier of the table
 * @length_out - out parameter, receives the size of the table + header
 *  
 * @return - physical address of the table, or 0 on failure
 */
uintptr_t rsdp_find_table(RSDP *rsdp, char table_name[4], u32 *length_out);

#endif // _ACPI_H
