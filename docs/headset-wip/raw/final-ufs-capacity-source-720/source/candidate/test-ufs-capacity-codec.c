#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ufs_capacity_codec.h"
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1); } } while(0)
static void put_be(uint8_t *p,uint32_t value)
{ p[0]=(uint8_t)(value>>24);p[1]=(uint8_t)(value>>16);p[2]=(uint8_t)(value>>8);p[3]=(uint8_t)value; }
static void unchanged(const uint8_t *data,size_t bytes,enum uc_decode_result expected)
{
    struct uc_geometry before,out;
    memset(&before,0x67,sizeof(before));out=before;
    CHECK(uc_decode_capacity(data,bytes,&out)==expected);
    CHECK(memcmp(&out,&before,sizeof(out))==0);
}
int main(void)
{
    struct uc_command command,before;
    CHECK(!uc_make_capacity(2,NULL));CHECK(!uc_command_allowed(NULL));
    for (unsigned lun=0;lun<256;lun++) {
        memset(&command,0x66,sizeof(command));before=command;
        CHECK(uc_make_capacity(lun,&command)==(lun==2));
        if (lun!=2) CHECK(memcmp(&command,&before,sizeof(command))==0);
    }
    memset(&command,0x66,sizeof(command));before=command;
    CHECK(!uc_make_capacity(UINT32_MAX,&command));CHECK(memcmp(&command,&before,sizeof(command))==0);
    CHECK(uc_make_capacity(2,&command));CHECK(uc_command_allowed(&command));
    CHECK(command.utrd[0]==0x14000000 && command.utrd[2]==15);
    CHECK(command.upiu[0]==1 && command.upiu[1]==0x40 && command.upiu[2]==2 && command.upiu[3]==0);
    CHECK(uc_be32(command.upiu+12)==8 && command.upiu[16]==0x25 && command.prd[3]==7);
    CHECK(command.utrd[4]==0x60101000 && command.prd[0]==0x6010e000);
    for (unsigned i=17;i<32;i++) CHECK(command.upiu[i]==0);
    /* Exhaust every alternative byte value, not just a small opcode sample. */
    for (size_t i=0;i<sizeof(command);i++) {
        uint8_t *byte=(uint8_t *)&command;
        uint8_t saved=byte[i];
        for (unsigned value=0;value<256;value++) {
            byte[i]=(uint8_t)value;
            CHECK(uc_command_allowed(&command)==(value==saved));
        }
        byte[i]=saved;
    }
    CHECK(uc_command_allowed(&command));
    uint8_t data[8];put_be(data,31240191);put_be(data+4,4096);
    CHECK(data[0]==1 && data[1]==0xdc && data[2]==0xaf && data[3]==0xff);
    struct uc_geometry geometry;
    CHECK(uc_decode_capacity(data,sizeof(data),&geometry)==UC_DECODE_OK);
    CHECK(geometry.last_lba==31240191 && geometry.block_count==31240192 && geometry.logical_block_bytes==4096);
    CHECK(geometry.capacity_bytes==UINT64_C(127959826432) && uc_matches_android(&geometry));
    CHECK(!uc_matches_android(NULL));
    struct uc_geometry real=geometry;
    geometry.last_lba++;CHECK(!uc_matches_android(&geometry));geometry=real;
    geometry.block_count++;CHECK(!uc_matches_android(&geometry));geometry=real;
    geometry.capacity_bytes++;CHECK(!uc_matches_android(&geometry));geometry=real;
    geometry.logical_block_bytes=512;CHECK(!uc_matches_android(&geometry));
    for (size_t bytes=0;bytes<=32;bytes++) if (bytes!=8) unchanged(data,bytes,UC_BAD_ARGUMENT);
    unchanged(data,SIZE_MAX,UC_BAD_ARGUMENT);unchanged(NULL,8,UC_BAD_ARGUMENT);
    CHECK(uc_decode_capacity(data,8,NULL)==UC_BAD_ARGUMENT);
    put_be(data,UINT32_MAX);unchanged(data,8,UC_RC10_SATURATED);
    memset(data,0xff,sizeof(data));unchanged(data,8,UC_RC10_SATURATED);
    memset(data,0,sizeof(data));unchanged(data,8,UC_ZERO_BLOCK_BYTES);
    put_be(data,31240191);unchanged(data,8,UC_ZERO_BLOCK_BYTES);
    const uint32_t last_values[]={0,1,31240190,31240191,31240192,UINT32_MAX-1};
    const uint32_t block_values[]={1,512,520,4096,8192,UINT32_MAX};
    for (unsigned i=0;i<sizeof(last_values)/sizeof(last_values[0]);i++)
        for (unsigned j=0;j<sizeof(block_values)/sizeof(block_values[0]);j++) {
            uint32_t last=last_values[i],block=block_values[j];
            put_be(data,last);put_be(data+4,block);
            CHECK(uc_decode_capacity(data,8,&geometry)==UC_DECODE_OK);
            CHECK(geometry.last_lba==last && geometry.logical_block_bytes==block);
            CHECK(geometry.block_count==(uint64_t)last+1 && geometry.capacity_bytes==((uint64_t)last+1)*block);
            CHECK(uc_matches_android(&geometry)==(last==31240191 && block==4096));
        }
    uint8_t reference[8];put_be(reference,31240191);put_be(reference+4,4096);
    for (size_t i=0;i<sizeof(reference);i++) for (unsigned value=0;value<256;value++) {
        memcpy(data,reference,sizeof(data));data[i]=(uint8_t)value;
        enum uc_decode_result result=uc_decode_capacity(data,8,&geometry);
        if (result==UC_DECODE_OK) CHECK(uc_matches_android(&geometry)==(value==reference[i]));
        else CHECK(value!=reference[i] && result==UC_ZERO_BLOCK_BYTES);
    }
    printf("PASS %u capacity-codec checks; pure host, no IO\n",checks);
    return 0;
}
