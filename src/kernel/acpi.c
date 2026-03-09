#include "acpi.h"
#include <kmem.h>
#include <string.h>


bool rsdp_check_header(RSDP *rsdp)
{
	unsigned char sum = 0;
	for(size_t i = 0; i < sizeof(*rsdp); ++i)
		sum += ((u8 *)rsdp)[i];

	return sum == 0;
}

uintptr_t rsdp_find_table(RSDP *rsdp, char table_name[4], u32 *length_out)
{
	if(!length_out)
		return 0;

	uintptr_t found = 0;

	ACPISDTHeader *root_header = kmem_map_phy_addr(rsdp->RsdtAddress, sizeof(*root_header), PAGE_FLAG_RW);

	size_t addrs_size = (root_header->Length - sizeof(*root_header));
	u32 *addresses = kmem_map_phy_addr(rsdp->RsdtAddress + sizeof(*root_header), addrs_size, PAGE_FLAG_RW);
	for(size_t i = 0; i < addrs_size / 4 && found == 0; ++i) {
		ACPISDTHeader *header = kmem_map_phy_addr(addresses[i], sizeof(*header), PAGE_FLAG_RW);

		if(memcmp(header->Signature, table_name, 4) == 0) {
			*length_out = header->Length;
			found = addresses[i];
		}
		
		kmem_unmap_raw(header, sizeof(*header));
	}

	kmem_unmap_raw(root_header, sizeof(*root_header));
	return found;
}

