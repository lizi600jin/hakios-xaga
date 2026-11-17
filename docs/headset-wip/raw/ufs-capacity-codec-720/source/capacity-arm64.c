#include "ufs_capacity_codec.h"
bool capacity_arm64_command(uint32_t lun,struct uc_command *command)
{ return uc_make_capacity(lun,command) && uc_command_allowed(command); }
enum uc_decode_result capacity_arm64_decode(const uint8_t *data,size_t bytes,struct uc_geometry *geometry)
{ return uc_decode_capacity(data,bytes,geometry); }
bool capacity_arm64_reference(const struct uc_geometry *geometry)
{ return uc_matches_android(geometry); }
