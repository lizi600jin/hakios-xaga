/* Host-only test of the ACTUAL 721-C provider against a synthetic controller.
 * No real hardware address is dereferenced. One scenario per process (argv[1]).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <zlib.h>

typedef uintptr_t vm_offset_t;
typedef unsigned long vm_size_t;
typedef int kern_return_t;
typedef int boolean_t;
#define VM_WIMG_WCOMB 0x8u
#define PAGE_SIZE 4096u
#define PAGE_MASK (PAGE_SIZE-1u)
#define EMU_REG_BYTES 0x2300u
#define EMU_SCRATCH_PAGES 15u
#define EMU_PAGES (2u + EMU_SCRATCH_PAGES)

static unsigned checks, scenarios, inject_hits;
#define CHECK(x) do { checks++; if(!(x)){ fprintf(stderr,"line%d: %s\n",__LINE__,#x); exit(1);} } while(0)
#define SCENARIO(n) do { scenarios++; printf("  scenario: %s\n", n); } while(0)
#define INJECT(n) do { inject_hits++; } while(0)

#define UCD_WORDS 416
#define DATA_WORDS 1024

struct model {
    uint32_t table[8], ucd[UCD_WORDS], data[DATA_WORDS], answered[DATA_WORDS];
    uint32_t regs[EMU_REG_BYTES/4];
    /* fault injection */
    const char *fail_name; int fail_at, call_ordinal; uint32_t fail_region; uint32_t fail_off;
    bool dbr_never_clears, stop_at_ring, stop_at_drain, stop_on_second;
    bool partial_data; unsigned partial_after, data_reads;
    bool restore_phase, restore_fails;
    int stop_at;      /* 0 none, 1 pre-copy, 3 in copy_done */
    bool stop_while_active, restore_phase2, arm_restore_fault;
    unsigned stop_injected, restore_read_hits;
    int fail_map;                 /* 1 scratch, 2 table, 3 regs */
    bool bad_managed, bad_pmap, fail_claim, fail_finish, fail_restore_read;
    unsigned maps, reg_reads, reg_writes, word_reads, word_writes;
    /* observation */
    uint32_t rings, stalled, decoded_lba[8], decoded_n;
    bool saw_command, rung;
    uint64_t clock_us;
};
static struct model M;
static uint8_t *BASE;

static uint8_t *page(unsigned n) { return BASE + (size_t)n*PAGE_SIZE; }
static uint32_t *reg32(unsigned off) { return (uint32_t*)(void*)(page(0)+off); }
static uint32_t *tbl32(void) { return (uint32_t*)(void*)(page(1)+0x400u); }
static uint32_t *ucd32(void) { return (uint32_t*)(void*)(page(2)+0x1000u); }  /* UCD at +0x1000 */
static uint32_t *data32(void) { return (uint32_t*)(void*)(page(2)+0xE000u); }

static void protect_all(int prot) { for (unsigned i=0;i<EMU_PAGES;i++)
    if (mprotect(page(i),PAGE_SIZE,prot)) { perror("mprotect"); exit(3); } }
static void restore_all(void) { protect_all(PROT_READ|PROT_WRITE); }

void vr_host_barrier(void) {}
uint64_t vr_host_now_us(void) { return M.clock_us++; }
vm_offset_t vr_host_map(vm_offset_t pa, vm_size_t size)
{ (void)size; M.maps++;
  if (M.fail_map==1 && pa>=0x60100000ull) return 0;
  if (M.fail_map==2 && pa>=0x51266000ull && pa<0x51267000ull) return 0;
  if (M.fail_map==3 && pa>=0x112b0000ull && pa<0x112b0000ull+EMU_REG_BYTES) return 0; if (pa>=0x112b0000ull && pa<0x112b0000ull+EMU_REG_BYTES) return (vm_offset_t)(uintptr_t)page(0);
  if (pa>=0x51266000ull && pa<0x51267000ull) return (vm_offset_t)(uintptr_t)page(1);
  if (pa>=0x60100000ull && pa<0x60100000ull+(vm_offset_t)EMU_SCRATCH_PAGES*PAGE_SIZE)
      return (vm_offset_t)(uintptr_t)page(2)+(pa-0x60100000ull);
  return 0; }
boolean_t vr_host_pmap_valid(uint64_t pa) { (void)pa; return M.bad_pmap?1:0; }
/* ur_outside_vm 由 contract 提供；这里用 host 钩子制造"落在 managed 区"的情形 */

static int g_mode=-1, g_service=0, g_read=0, g_probe=0, g_capacity=0;
int vr_host_boot_arg(const char *name, void *out, size_t size)
{
    (void)size;
    if (!strcmp(name,"mtk_ufs_varread"))   { if (g_mode<0) return 0; *(uint32_t*)out=(uint32_t)g_mode; return 1; }
    if (!strcmp(name,"mtk_ufs_service"))   { *(uint32_t*)out=(uint32_t)g_service;  return 1; }
    if (!strcmp(name,"mtk_ufs_read"))      { *(uint32_t*)out=(uint32_t)g_read;     return 1; }
    if (!strcmp(name,"mtk_ufs_probe"))     { *(uint32_t*)out=(uint32_t)g_probe;    return 1; }
    if (!strcmp(name,"mtk_ufs_capacity"))  { *(uint32_t*)out=(uint32_t)g_capacity; return 1; }
    return 0;
}
#define ml_io_map(a,b)              vr_host_map((a),(b))
#define ml_io_map_unmappable(a,b,c) vr_host_map((a),(b))
#define pmap_valid_address(a)       vr_host_pmap_valid((a))
#define PE_parse_boot_argn          vr_host_boot_arg
unsigned long gPhysBase = 0x100000000ull, gPhysSize = 0x100000000ull;
static uint32_t PUBLISHED;
static bool publish_cb(void *ctx, uint32_t state) { (void)ctx; PUBLISHED=state; return true; }

/* 16 reference blocks; their bytes stay local and are never printed. */
static uint8_t ref_payload[16][4096];
static bool ref_loaded;

static uint32_t ref_crc[16];
static void load_reference(void)
{
    const char *p = getenv("VR_REFERENCE");
    if (!p) { for (int i=0;i<16;i++) ref_crc[i]=0; return; }
    FILE *f = fopen(p,"rb");
    if (!f) { fprintf(stderr,"cannot open reference\n"); exit(3); }
    for (int i=0;i<16;i++) {
        if (fread(ref_payload[i],1,4096,f)!=4096) { fprintf(stderr,"short reference\n"); exit(3); }
        ref_crc[i] = (uint32_t)crc32(0L, ref_payload[i], 4096);
    }
    fclose(f); ref_loaded = true;
}

/* The provider reads the emulated backing memory directly, so the controller must write
 * THERE — not into a private struct array. Keeping both would have let the test pass while
 * the real path read stale bytes.
 */

/* The synthetic controller answers with the reference bytes for the decoded LBA, so a
 * reference-CRC match is a real observation. The bytes are never printed.
 */
static void model_fill(uint32_t lba)
{
    if (ref_loaded && lba < 16u) {
        memcpy(data32(), ref_payload[lba], 4096);
        memcpy(M.answered, ref_payload[lba], 4096);
        return;
    }
    uint32_t seed = lba*2654435761u + 0x9e3779b9u;
    for (uint32_t i=0;i<DATA_WORDS;i++) {
        seed = seed*1103515245u+12345u;
        data32()[i] = seed;
        M.answered[i] = seed;
    }
}
static void model_complete(void);
static void model_reset(void);

/* ---- the synthetic controller -------------------------------------------------
 * HOOKS ONLY. They mutate emulated backing memory or refuse an access; they never perform
 * the read themselves. The provider's own whitelist switch, address arithmetic and volatile
 * load run in both builds, so the platform test exercises them for real.
 */
static void model_complete(void);
static void model_reset(void);

static bool vr_host_reg_hook(uint16_t off)
{
    if (off == 0x58) {
        /* The provider wrote the doorbell; the model observes MEMORY. */
        if (reg32(0x58)[0] == 1u && !M.rung) {
            M.rung = true; M.rings++; INJECT();
            M.saw_command = false;
            if (M.arm_restore_fault) M.restore_phase2 = true;   /* arm only after the ring */
        }
        if (!M.rung) { reg32(0x58)[0] = 0u; return true; }
        if (M.dbr_never_clears || M.stop_at_ring) { reg32(0x58)[0] = 1u; return true; }
    /* Inject a read fault at a chosen ordinal: the hook refuses, the provider reports false. */
    if (M.fail_name && !strcmp(M.fail_name,"read_reg") && ++M.call_ordinal==M.fail_at)
        { INJECT(); return false; }
        if (!M.saw_command) model_complete();
        reg32(0x58)[0] = 0u;
    }
    return true;      /* never replaces the volatile read */
}

static bool vr_host_ring_hook(void)
{
    if (M.fail_name && !strcmp(M.fail_name,"ring") && ++M.call_ordinal==M.fail_at)
        { INJECT(); return false; }
    return true;
}

static bool vr_host_read_word_hook(int region, uint32_t off)
{
    (void)off;
    /* The engine's restore re-reads table/UCD/data to verify the snapshot; a fault there is a
     * distinct failure from a sampling fault.
     */
    if (M.restore_phase2 && region==1) {
        if (M.fail_restore_read) {
            M.fail_restore_read=false; M.restore_read_hits++; INJECT(); return false;
        }
    }
    if (M.fail_name && !strcmp(M.fail_name,"read_word") && ++M.call_ordinal==M.fail_at)
        { INJECT(); return false; }
    if (region==2 && M.saw_command && M.partial_data) {
        if (M.data_reads>=M.partial_after) { INJECT(); M.partial_data=false; return false; }
        M.data_reads++;
    }
    return true;
}

static bool vr_host_write_hook(int region, uint32_t off)
{
    if (M.fail_name && !strcmp(M.fail_name,"write_word") && ++M.call_ordinal==M.fail_at)
        { INJECT(); return false; }
    if (M.restore_phase && region == 2 && off == 0u) { M.restore_phase = false; INJECT(); return false; }
    return true;
}

#include "ufs_variable_read_engine.h"

/* The provider is compiled in so its statics are observable. */
#include "hakios.mt6895_ufs_varread.c"

/* Real STOP at the chosen point: it calls the same us_stop the kernel would, so the
 * provider's later checks run against the genuine cached lease state.
 */
static void inject_stop_now(void)
{
    M.stop_injected++;
    INJECT();
    us_stop(&vr_lease);
}

void varread_host_stop_point(int which)
{
    if (M.stop_at != which) return;
    M.stop_at = 0;
    inject_stop_now();
}

bool varread_host_bad_managed(void) { return M.bad_managed; }
bool varread_host_claim_hook(void) { if (M.fail_claim) { INJECT(); M.fail_claim=false; return false; } return true; }
bool varread_host_finish_hook(void) { if (M.fail_finish) { INJECT(); M.fail_finish=false; return false; } return true; }
bool varread_host_restore_read_hook(void)
{
    if (M.fail_restore_read) { M.fail_restore_read=false; M.restore_read_hits++; INJECT(); return false; }
    return true;
}

static void model_reset(void)
{
    memset(&M,0,sizeof(M));
    reg32(0)[0]=0x3587071fu; reg32(8)[0]=0x200u;
    reg32(0x30)[0]=0x10fu; reg32(0x34)[0]=1u;
    reg32(0x50)[0]=(uint32_t)UR_LIST_PA; reg32(0x54)[0]=0; reg32(0x58)[0]=0; reg32(0x60)[0]=1u;
    /* inherited READ(10) baseline */
    tbl32()[0]=0x14000000u; tbl32()[2]=0; tbl32()[4]=(uint32_t)UR_UCD_PA;
    tbl32()[5]=0; tbl32()[6]=0x00800080u; tbl32()[7]=0x01000001u;
    ucd32()[0]=0x00024001u; ucd32()[3]=0x00100000u; ucd32()[4]=0x00000028u;
    ucd32()[5]=0; ucd32()[6]=1;
    /* fresh loader lease */
    struct ur_lease *r=(struct ur_lease*)(void*)page(2);
    r->magic=UR_MAGIC; r->version=UR_VERSION; r->trial=UR_TRIAL; r->state=UR_UNUSED;
}
static void model_complete(void)
{
    uint8_t upiu[32];
    for (unsigned i=0;i<8;i++) { uint32_t w=ucd32()[i];
        upiu[i*4]=(uint8_t)w; upiu[i*4+1]=(uint8_t)(w>>8); upiu[i*4+2]=(uint8_t)(w>>16); upiu[i*4+3]=(uint8_t)(w>>24); }
    M.saw_command=true;
    uint32_t lba=((uint32_t)upiu[18]<<24)|((uint32_t)upiu[19]<<16)|((uint32_t)upiu[20]<<8)|upiu[21];
    if (M.decoded_n<8) M.decoded_lba[M.decoded_n++]=lba;
    model_fill(lba);
    uint8_t resp[52]; memset(resp,0,sizeof(resp)); resp[0]=0x21; resp[2]=2;
    for (unsigned i=0;i<13;i++)
        ucd32()[512/4+i]=(uint32_t)resp[i*4]|((uint32_t)resp[i*4+1]<<8)|((uint32_t)resp[i*4+2]<<16)|((uint32_t)resp[i*4+3]<<24);
    tbl32()[2]=0;
    reg32(0x58)[0]=0;      /* the controller clears the doorbell */
    M.rung=false;          /* ready to observe the next posted store */
}

static int g_dbg_once;
static bool h_read_reg(void *c, uint16_t off, uint32_t *out)
{
    (void)c;
    if (off==0x58 && !g_dbg_once) { g_dbg_once=1;
        fprintf(stderr,"      [R] regs=%p page0=%p dbr@mem=%08x\n",
                (void*)platform.regs,(void*)page(0),reg32(0x58)[0]); }
    if (M.fail_name && !strcmp(M.fail_name,"read_reg") && ++M.call_ordinal==M.fail_at)
        { INJECT(); return false; }
    switch (off) {
    case 0: *out=reg32(0)[0]; return true;
    case 8: *out=reg32(8)[0]; return true;
    case 0x30: *out=reg32(0x30)[0]; return true;
    case 0x34: *out=reg32(0x34)[0]; return true;
    case 0x50: *out=reg32(0x50)[0]; return true;
    case 0x54: *out=reg32(0x54)[0]; return true;
    case 0x58:
        /* The provider writes the doorbell itself, so the model must observe MEMORY, not a
         * callback of its own. This is what makes the ring count real.
         */
        if (reg32(0x58)[0]==1u && !M.rung) { M.rung=true; M.rings++; INJECT(); }
        if (!M.rung) { *out=0; return true; }
        if (M.dbr_never_clears) { *out=1; return true; }
        if (M.stop_at_ring) { *out=1; return true; }
        /* Each new ring is a new command: reset the per-command observation. */
        if (!M.saw_command) model_complete();
        *out=0; return true;
    case 0x60: *out=reg32(0x60)[0]; return true;
    default: return false;
    }
}
static bool h_read_word(void *c, int region, uint32_t off, uint32_t *out)
{
    (void)c;
    uint32_t i=off/4;
    if (M.fail_name && !strcmp(M.fail_name,"read_word") && ++M.call_ordinal==M.fail_at)
        { INJECT(); return false; }
    if (region==2 && M.saw_command && M.partial_data) {
        if (M.data_reads>=M.partial_after) { INJECT(); M.partial_data=false; return false; }
        M.data_reads++;
    }
    if (region==0) { if (i>=8) return false; *out=tbl32()[i]; return true; }
    if (region==1) { if (i>=UCD_WORDS) return false; *out=ucd32()[i]; return true; }
    if (region==2) { if (i>=DATA_WORDS) return false; *out=data32()[i]; return true; }
    return false;
}
static bool h_write_word(void *c, int region, uint32_t off, uint32_t v)
{
    (void)c;
    uint32_t i=off/4;
    if (M.fail_name && !strcmp(M.fail_name,"write_word") && ++M.call_ordinal==M.fail_at)
        { INJECT(); return false; }
    if (M.restore_phase && region==2 && off==0u) { M.restore_phase=false; INJECT(); return false; }
    if (region==0) { if (i>=8) return false; tbl32()[i]=v; return true; }
    if (region==1) { if (i>=UCD_WORDS) return false; ucd32()[i]=v; return true; }
    if (region==2) { if (i>=DATA_WORDS) return false; data32()[i]=v; return true; }
    return false;
}
static bool h_claim(void *c)  { (void)c; if (M.fail_name && !strcmp(M.fail_name,"claim")) { INJECT(); return false; } return true; }
static bool h_finish(void *c, bool ok) { (void)c;
    if (!ok) return false;
    if (M.fail_name && !strcmp(M.fail_name,"finish")) { INJECT(); return false; }
    return ok; }
static bool h_stopped(void *c) { (void)c;
    if (M.stop_at_drain) return true;
    return false; }
/* Not used by the provider (it has its own ring), kept so the callback table is complete. */
static bool h_ring(void *c) { (void)c; return true; }
static void h_barrier(void *c) { (void)c; }
static uint64_t h_now(void *c) { (void)c; return M.clock_us++; }


int main(int argc, char **argv)
{
    BASE = mmap(NULL, EMU_PAGES*PAGE_SIZE, PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (BASE==MAP_FAILED) { perror("mmap"); return 3; }
    load_reference();
    const char *sc = argc>1 ? argv[1] : "full";

    if (!strcmp(sc,"full")) {
        SCENARIO("four requests 1 2 15 1, budget 4, then a second init issues nothing");
        model_reset(); g_mode=1;
        unsigned m0=M.maps, r0=M.reg_reads, w0=M.reg_writes, rd0=M.word_reads, wr0=M.word_writes;
        hakios_mt6895_ufs_varread_init();
        CHECK(M.rings==4);
        CHECK(M.decoded_n==4);
        CHECK(M.decoded_lba[0]==1 && M.decoded_lba[1]==2 &&
              M.decoded_lba[2]==15 && M.decoded_lba[3]==1);
        /* every request kept its OWN copy: compare all four, not just the last */
        for (unsigned i=0;i<VR_BUDGET;i++)
            CHECK(memcmp(vr_records[i].cached, ref_payload[vr_sequence[i]], VR_BLOCK_BYTES)==0);
        /* re-entry: prove by real counters and mappings, not just ring equality */
        { unsigned m1=M.maps, r1=M.reg_reads, w1=M.reg_writes, rd1=M.word_reads, wr1=M.word_writes;
          unsigned rings=M.rings;
          hakios_mt6895_ufs_varread_init();
          CHECK(M.rings==rings && M.maps==m1 && M.reg_reads==r1 &&
                M.reg_writes==w1 && M.word_reads==rd1 && M.word_writes==wr1); }
        (void)m0; (void)r0; (void)w0; (void)rd0; (void)wr0;
    } else if (!strcmp(sc,"stop_precopy")) {
        SCENARIO("REAL STOP in DRAINED before the copy: RESTORED stays published");
        model_reset(); g_mode=1; M.stop_at=1;
        hakios_mt6895_ufs_varread_init();
        CHECK(M.stop_injected==1);                       /* the real us_stop ran */
        CHECK(vr_records[0].copied==0);
        CHECK((vr_lease.control & US_RELEASED_BIT) != 0);
        CHECK((vr_lease.control & US_POISON_BIT) == 0);  /* not re-poisoned */
        CHECK(varread_published_state==UR_RESTORED);
        CHECK(M.rings==1);
    } else if (!strcmp(sc,"stop_incopydone")) {
        SCENARIO("REAL STOP in DRAINED at copy_done: us_copy_done fails, RESTORED kept");
        model_reset(); g_mode=1;
        M.stop_at=3;                                     /* inside copy_done window */
        hakios_mt6895_ufs_varread_init();
        CHECK(M.stop_injected==1);
        CHECK((vr_lease.control & US_RELEASED_BIT) != 0);
        CHECK((vr_lease.control & US_POISON_BIT) == 0);
        CHECK(varread_published_state==UR_RESTORED);                   /* clean release not overwritten */
    } else if (!strcmp(sc,"stop_idle")) {
        SCENARIO("REAL STOP at an IDLE boundary is a clean release");
        model_reset(); g_mode=1;
        hakios_mt6895_ufs_varread_init();                 /* completes normally, back to IDLE */
        CHECK(vr_records[3].copied==1);
        us_stop(&vr_lease);                              /* quiesce from IDLE */
        CHECK((vr_lease.control & US_RELEASED_BIT) != 0);
        CHECK((vr_lease.control & US_POISON_BIT) == 0);
        CHECK(varread_published_state==UR_RESTORED);
    } else if (!strcmp(sc,"stop_active")) {
        SCENARIO("REAL STOP while ACTIVE still poisons");
        model_reset(); g_mode=1;
        M.stop_at=1; M.stop_while_active=true;
        hakios_mt6895_ufs_varread_init();
        CHECK((vr_lease.control & (US_RELEASED_BIT|US_POISON_BIT)) != 0);
        CHECK(vr_records[0].copied==0);
    } else if (!strcmp(sc,"mapping")) {
        SCENARIO("scratch or register mapping failure stops before any request");
        model_reset(); g_mode=1; M.fail_map=1;
        hakios_mt6895_ufs_varread_init();
        CHECK(M.rings==0);
        model_reset(); g_mode=1; M.fail_map=2;
        hakios_mt6895_ufs_varread_init();
        CHECK(M.rings==0);
        model_reset(); g_mode=1; M.fail_map=3;
        hakios_mt6895_ufs_varread_init();
        CHECK(M.rings==0);
    } else if (!strcmp(sc,"managed")) {
        SCENARIO("managed-memory or pmap overlap is refused before mapping");
        model_reset(); g_mode=1; M.bad_managed=1;
        hakios_mt6895_ufs_varread_init();
        CHECK(M.rings==0 && M.maps==0);
        model_reset(); g_mode=1; M.bad_pmap=1;
        hakios_mt6895_ufs_varread_init();
        CHECK(M.rings==0 && M.maps==0);
    } else if (!strcmp(sc,"badbaseline")) {
        SCENARIO("a broken inherited baseline is refused before CLAIMED is published");
        model_reset(); g_mode=1; tbl32()[6]=0x00800081u;   /* wrong PRDT entry count */
        hakios_mt6895_ufs_varread_init();
        CHECK(M.rings==0);
        CHECK(varread_published_state==0);                  /* CLAIMED never published */
        model_reset(); g_mode=1; ucd32()[4]=0x0000002au;    /* wrong opcode */
        hakios_mt6895_ufs_varread_init();
        CHECK(M.rings==0 && varread_published_state==0);
    } else if (!strcmp(sc,"claimfail")) {
        SCENARIO("claim failure is fail-closed and issues no ring");
        model_reset(); g_mode=1; M.fail_claim=1;
        hakios_mt6895_ufs_varread_init();
        CHECK(M.rings==0);
        CHECK((vr_lease.control & US_POISON_BIT) != 0);
    } else if (!strcmp(sc,"finishfail")) {
        SCENARIO("finish failure poisons and stops the run");
        model_reset(); g_mode=1; M.fail_finish=1;
        hakios_mt6895_ufs_varread_init();
        CHECK(M.rings==1);
        CHECK((vr_lease.control & US_POISON_BIT) != 0);
        CHECK(vr_records[0].copied==0);
    } else if (!strcmp(sc,"restoresample")) {
        SCENARIO("a restore sampling read failure is caught and poisons");
        model_reset(); g_mode=1; M.fail_restore_read=1; M.arm_restore_fault=1;
        hakios_mt6895_ufs_varread_init();
        CHECK(M.restore_read_hits==1);
        CHECK((vr_lease.control & US_POISON_BIT) != 0);
        CHECK(vr_records[0].copied==0);
    } else if (!strcmp(sc,"modes")) {
        SCENARIO("every competing mode and an invalid value are refused before any IO");
        const int vals[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
        for (int k=0;k<4;k++) {
            model_reset(); g_mode=1;
            g_service=vals[k][0]; g_read=vals[k][1]; g_probe=vals[k][2]; g_capacity=vals[k][3];
            hakios_mt6895_ufs_varread_init();
            CHECK(M.rings==0 && M.maps==0);
        }
        model_reset(); g_service=g_read=g_probe=g_capacity=0; g_mode=2;
        hakios_mt6895_ufs_varread_init(); CHECK(M.rings==0);
        model_reset(); g_mode=-1; hakios_mt6895_ufs_varread_init(); CHECK(M.rings==0);
    } else if (!strcmp(sc,"guard")) {
        SCENARIO("a stale, wrong-trial or wrong-state guard is refused");
        model_reset(); g_mode=1; ((struct ur_lease*)(void*)page(2))->state=UR_CLAIMED;
        hakios_mt6895_ufs_varread_init(); CHECK(M.rings==0);
        model_reset(); g_mode=1; ((struct ur_lease*)(void*)page(2))->trial=720u;
        hakios_mt6895_ufs_varread_init(); CHECK(M.rings==0);
        model_reset(); g_mode=1; ((struct ur_lease*)(void*)page(2))->magic=0u;
        hakios_mt6895_ufs_varread_init(); CHECK(M.rings==0);
    } else if (!strcmp(sc,"quiesce")) {
        SCENARIO("clean quiesce touches only the lease page: everything else PROT_NONE");
        model_reset(); g_mode=1;
        hakios_mt6895_ufs_varread_init();
        protect_all(PROT_NONE);
        if (mprotect(page(2),PAGE_SIZE,PROT_READ|PROT_WRITE)) { perror("mprotect"); exit(3); }
        hakios_mt6895_ufs_varread_quiesce();   /* faults if it touches regs/table/UCD/data */
        restore_all();
        CHECK((vr_lease.control & US_STOP_BIT) != 0);
        CHECK(varread_published_state==UR_RESTORED);
    } else {
        fprintf(stderr,"unknown scenario %s\n",sc); return 2;
    }
    printf("PASS %s: %u checks, %u injection points\n", sc, checks, inject_hits);
    return 0;
}
