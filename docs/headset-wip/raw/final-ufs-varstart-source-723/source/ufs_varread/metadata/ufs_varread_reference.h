/* Reference CRC metadata for the bounded variable-LBA probe.
 *
 * Pure constants. The reference PAYLOAD is never read, stored, logged or printed — only these
 * three checksums, which the operator independently verified against the private snapshot.
 *
 * Presence is its own signal. A checksum of 0 is NOT overloaded to mean "no reference".
 */
#ifndef MT6895_UFS_VARREAD_REFERENCE_H
#define MT6895_UFS_VARREAD_REFERENCE_H
#include <stdint.h>
#include <stdbool.h>

#define VR_REF_LBA1   1u
#define VR_REF_LBA2   2u
#define VR_REF_LBA3   3u
#define VR_REF_LBA14  14u
#define VR_REF_LBA15  15u

#define VR_REF_CRC_LBA1   UINT32_C(0x28a2836c)
#define VR_REF_CRC_LBA2   UINT32_C(0x0d024e0c)
#define VR_REF_CRC_LBA3   UINT32_C(0x58f10429)
#define VR_REF_CRC_LBA14  UINT32_C(0xc71c0011)
#define VR_REF_CRC_LBA15  UINT32_C(0xc71c0011)

/* True only for the LBAs this stage carries metadata for. */
static inline bool varread_reference_is_known(uint32_t lba)
{
	return lba == VR_REF_LBA1 || lba == VR_REF_LBA2 || lba == VR_REF_LBA3 ||
	       lba == VR_REF_LBA14 || lba == VR_REF_LBA15;
}

/* Returns 0 for an unknown LBA; callers MUST gate on varread_reference_is_known() first. */
static inline uint32_t varread_reference_crc(uint32_t lba)
{
	switch (lba) {
	case VR_REF_LBA1:  return VR_REF_CRC_LBA1;
	case VR_REF_LBA2:  return VR_REF_CRC_LBA2;
	case VR_REF_LBA3:  return VR_REF_CRC_LBA3;
	case VR_REF_LBA14: return VR_REF_CRC_LBA14;
	case VR_REF_LBA15: return VR_REF_CRC_LBA15;
	default:           return 0u;
	}
}

#endif
