
#ifndef _NVME_H
#define _NVME_H

#include <pci.h>

struct NVMeDevice;
int nvme_init(PCIe *pcie, struct NVMeDevice *out_dev);
bool nvme_write(struct NVMeDevice *dev, int nsid, u64 lba, u16 nblocks, uintptr_t buf_paddr);
bool nvme_read(struct NVMeDevice *dev, int nsid, u64 lba, u16 nblocks, uintptr_t buf_paddr);

typedef struct {
	u32 cmd;              // DWORD 0
	u32 NSID;             // DWORD 1
	u32 reserved[2];      // DWORD 2-3
	u32 metadata_ptr[2];  // DWORD 4-5
	u32 prp[4];           // DWORD 6-9
	u32 arg[6];           // DWORD 10-15
} NVMeSubmissionEntry;

typedef struct {
	u32 arg;
	u32 reserved;
	u32 sq;   // sq id [31:16], sq head ptr [15:0]
	u32 spc;  // status [31:17], phase[16:16], cmd_id[15:0]
} NVMeCompletionEntry;

typedef struct {
	uintptr_t sq_paddr;
	volatile NVMeSubmissionEntry *sq;
	size_t sq_size;
	size_t sq_tail;
	uintptr_t cq_paddr;
	volatile NVMeCompletionEntry *cq;
	size_t cq_size;
	size_t cq_head;
	int id;
	u8 phase;
} NVMeQueue;


// Identify Controller data structure (CNS = 0x01), 4096 bytes total
typedef struct __attribute__((packed)) {
	// Controller Capabilities and Features (bytes 0-255)
	u16 VID;              // 0:   PCI Vendor ID
	u16 SSVID;             // 2:   PCI Subsystem Vendor ID
	char SN[20];           // 4:   Serial Number (ASCII, space-padded)
	char MN[40];           // 24:  Model Number (ASCII, space-padded)
	char FR[8];            // 64:  Firmware Revision (ASCII)
	u8  RAB;               // 72:  Recommended Arbitration Burst
	u8  IEEE[3];           // 73:  IEEE OUI Identifier
	u8  CMIC;              // 76:  Controller Multi-Path I/O and Namespace Sharing Capabilities
	u8  MDTS;              // 77:  Max Data Transfer Size (as a power-of-2 multiple of MPS pages; 0 = no limit)
	u16 CNTLID;            // 78:  Controller ID
	u32 VER;               // 80:  Version
	u32 RTD3R;              // 84:  RTD3 Resume Latency
	u32 RTD3E;              // 88:  RTD3 Entry Latency
	u32 OAES;               // 92:  Optional Async Events Supported
	u32 CTRATT;             // 96:  Controller Attributes
	u8  reserved1[12];      // 100
	u8  FGUID[16];          // 112: FRU Globally Unique Identifier
	u8  reserved2[128];     // 128

	// Admin Command Set Attributes (bytes 256-511)
	u16 OACS;               // 256: Optional Admin Command Support
	u8  ACL;                // 258: Abort Command Limit
	u8  AERL;               // 259: Async Event Request Limit
	u8  FRMW;               // 260: Firmware Updates
	u8  LPA;                // 261: Log Page Attributes
	u8  ELPE;               // 262: Error Log Page Entries
	u8  NPSS;               // 263: Number of Power States Support
	u8  AVSCC;              // 264: Admin Vendor Specific Command Configuration
	u8  APSTA;              // 265: Autonomous Power State Transition Attributes
	u16 WCTEMP;             // 266: Warning Composite Temperature Threshold
	u16 CCTEMP;             // 268: Critical Composite Temperature Threshold
	u16 MTFA;               // 270: Max Time For Firmware Activation
	u32 HMPRE;               // 272: Host Memory Buffer Preferred Size
	u32 HMMIN;               // 276: Host Memory Buffer Minimum Size
	u8  TNVMCAP[16];         // 280: Total NVM Capacity
	u8  UNVMCAP[16];         // 296: Unallocated NVM Capacity
	u32 RPMBS;               // 312: Replay Protected Memory Block Support
	u16 EDSTT;               // 316: Extended Device Self-test Time
	u8  DSTO;                // 318: Device Self-test Options
	u8  FWUG;                // 319: Firmware Update Granularity
	u16 KAS;                 // 320: Keep Alive Support
	u16 HCTMA;                // 322: Host Controller Thermal Mgmt Attributes
	u16 MNTMT;                // 324: Minimum Thermal Mgmt Temperature
	u16 MXTMT;                // 326: Maximum Thermal Mgmt Temperature
	u32 SANICAP;              // 328: Sanitize Capabilities
	u8  reserved3[180];       // 332

	// NVM Command Set Attributes (bytes 512-703)
	u8  SQES;                // 512: Submission Queue Entry Size (bits 3:0 = required, 7:4 = max)
	u8  CQES;                // 513: Completion Queue Entry Size (same bit layout)
	u16 MAXCMD;               // 514: Maximum Outstanding Commands
	u32 NN;                   // 516: Number of Namespaces 
	u16 ONCS;                 // 520: Optional NVM Command Support
	u16 FUSES;                // 522: Fused Operation Support
	u8  FNA;                  // 524: Format NVM Attributes
	u8  VWC;                  // 525: Volatile Write Cache
	u16 AWUN;                 // 526: Atomic Write Unit Normal
	u16 AWUPF;                // 528: Atomic Write Unit Power Fail
	u8  NVSCC;                // 530: NVM Vendor Specific Command Configuration
	u8  reserved4;            // 531
	u16 ACWU;                 // 532: Atomic Compare & Write Unit
	u8  reserved5[2];         // 534
	u32 SGLS;                 // 536: SGL Support
	u8  reserved6[164];       // 540

	// Power State Descriptors + vendor specific (bytes 704-4095)
	u8  reserved7[1344];      // 704
	u8  psd[1024];            // 2048: Power State Descriptors (32 bytes x 32)
	u8  vendor_specific[1024];// 3072: Vendor Specific
} NVMeIdentifyController;

// LBA Format descriptor, used inside Identify Namespace
typedef struct __attribute__((packed)) {
	u16 MS;   // Metadata Size (bytes)
	u8  LBADS; // LBA Data Size, as power of 2 (e.g. 9 = 512 bytes, 12 = 4096 bytes)
	u8  RP;    // Relative Performance
} NVMeLBAFormat;

// Identify Namespace data structure (CNS = 0x00, requires NSID set), 4096 bytes total
typedef struct __attribute__((packed)) {
	u64 NSZE;               // 0:  Namespace Size (in logical blocks)
	u64 NCAP;               // 8:  Namespace Capacity
	u64 NUSE;                // 16: Namespace Utilization
	u8  NSFEAT;               // 24: Namespace Features
	u8  NLBAF;                // 25: Number of LBA Formats
	u8  FLBAS;                // 26: Formatted LBA Size (bits 3:0 = index into lbaf[])
	u8  MC;                   // 27: Metadata Capabilities
	u8  DPC;                  // 28: End-to-end Data Protection Capabilities
	u8  DPS;                  // 29: End-to-end Data Protection Type Settings
	u8  NMIC;                 // 30: Namespace Multi-path I/O and Sharing Capabilities
	u8  RESCAP;               // 31: Reservation Capabilities
	u8  FPI;                  // 32: Format Progress Indicator
	u8  DLFEAT;               // 33: Deallocate Logical Block Features
	u16 NAWUN;                 // 34: Namespace Atomic Write Unit Normal
	u16 NAWUPF;                 // 36: Namespace Atomic Write Unit Power Fail
	u16 NACWU;                  // 38: Namespace Atomic Compare & Write Unit
	u16 NABSN;                  // 40: Namespace Atomic Boundary Size Normal
	u16 NABO;                   // 42: Namespace Atomic Boundary Offset
	u16 NABSPF;                  // 44: Namespace Atomic Boundary Size Power Fail
	u16 NOIOB;                   // 46: Namespace Optimal I/O Boundary
	u8  NVMCAP[16];              // 48: NVM Capacity
	u8  reserved1[40];           // 64
	u8  NGUID[16];                // 104: Namespace GUID
	u8  EUI64[8];                 // 120: IEEE Extended Unique Identifier
	NVMeLBAFormat lbaf[16];        // 128: LBA Format Support list (indexed by FLBAS bits 3:0)
	u8  reserved2[192];            // 192
	u8  vendor_specific[3712];     // 384
} NVMeIdentifyNamespace;

static_assert(sizeof(NVMeIdentifyController) == 4096, "NVMeIdentifyController must be 4096 bytes");
static_assert(sizeof(NVMeIdentifyNamespace) == 4096, "NVMeIdentifyNamespace must be 4096 bytes");

#define __MAX_NVME_NS 16
typedef struct {
	u32 nsid;
	u32 block_size;
	u32 num_blocks;
} NVMeNamespaceInfo;

typedef struct NVMeDevice {
	volatile void *base;
	u32 doorbell_stride;
	NVMeQueue admin;
	NVMeQueue io;
	NVMeIdentifyController id_controller;
	NVMeIdentifyNamespace id_namespace;
	NVMeNamespaceInfo ns_infos[__MAX_NVME_NS];
	int ns_count;
} NVMeDevice;

#endif

