typedef long int int64_t;
typedef long unsigned int uint64_t;
typedef int64_t int_least64_t;
typedef uint64_t uint_least64_t;
typedef int64_t int_fast64_t;
typedef uint64_t uint_fast64_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef int32_t int_least32_t;
typedef uint32_t uint_least32_t;
typedef int32_t int_fast32_t;
typedef uint32_t uint_fast32_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int16_t int_least16_t;
typedef uint16_t uint_least16_t;
typedef int16_t int_fast16_t;
typedef uint16_t uint_fast16_t;
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef int8_t int_least8_t;
typedef uint8_t uint_least8_t;
typedef int8_t int_fast8_t;
typedef uint8_t uint_fast8_t;
typedef long int intptr_t;
typedef long unsigned int uintptr_t;
typedef long int intmax_t;
typedef long unsigned int uintmax_t;
typedef long int ptrdiff_t;
typedef long unsigned int size_t;
typedef unsigned int wchar_t;
typedef struct {
  long long __clang_max_align_nonce1
      __attribute__((__aligned__(__alignof__(long long))));
  long double __clang_max_align_nonce2
      __attribute__((__aligned__(__alignof__(long double))));
} max_align_t;


static const unsigned char font8x16[95][16] = {
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x24,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x12,0x16,0x7f,0x3e,0x24,0xfe,0xfe,0x68,0x48,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x08,0x18,0x3e,0x78,0x78,0x3c,0x1e,0x1e,0x7e,0x7c,0x18,0x18,0x00,0x00 },
    { 0x00,0x00,0x00,0x70,0xd0,0xd0,0x76,0x18,0x4e,0x0b,0x0b,0x0e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x1c,0x3c,0x60,0x30,0x30,0x7b,0xcf,0xcf,0xee,0x7f,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x10,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x0c,0x08,0x18,0x18,0x10,0x30,0x30,0x18,0x18,0x18,0x08,0x0c,0x00,0x00 },
    { 0x00,0x00,0x30,0x10,0x18,0x18,0x18,0x0c,0x0c,0x18,0x18,0x18,0x10,0x30,0x00,0x00 },
    { 0x00,0x00,0x00,0x58,0x7e,0x3c,0x7e,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x18,0x18,0x18,0xfe,0x7e,0x18,0x18,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3c,0x3c,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x02,0x06,0x04,0x0c,0x08,0x18,0x10,0x30,0x30,0x60,0x60,0x00,0x00,0x00 },
    { 0x00,0x00,0x18,0x3c,0x66,0x66,0x66,0x7e,0x66,0x66,0x7e,0x3c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x18,0x78,0x38,0x18,0x18,0x18,0x18,0x18,0x7e,0x7e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x38,0x7c,0x06,0x06,0x0c,0x1c,0x18,0x30,0x7e,0x7e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x38,0x7e,0x06,0x06,0x3c,0x1c,0x06,0x06,0x7e,0x7c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x0c,0x0c,0x1c,0x3c,0x2c,0x6c,0x7e,0x7e,0x0c,0x0c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x3c,0x7e,0x60,0x60,0x7c,0x6e,0x06,0x06,0x7e,0x7c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x1c,0x3e,0x60,0x60,0x7e,0x76,0x66,0x66,0x7e,0x3c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x7e,0x7e,0x06,0x0c,0x0c,0x1c,0x18,0x18,0x30,0x30,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x18,0x7e,0x66,0x66,0x3c,0x3c,0x66,0x66,0x7e,0x3c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x18,0x7c,0x66,0x66,0x66,0x7e,0x3e,0x06,0x6c,0x7c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x06,0x3e,0x70,0x70,0x3e,0x06,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x7e,0x00,0x00,0x7e,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x60,0x7c,0x0e,0x0e,0x7c,0x60,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x18,0x7e,0x06,0x06,0x0c,0x18,0x18,0x10,0x18,0x18,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x3c,0x7e,0x42,0xdf,0xd3,0xb3,0xd3,0xdf,0x40,0x72,0x1e,0x00,0x00 },
    { 0x00,0x00,0x18,0x38,0x3c,0x3c,0x3c,0x66,0x7e,0x7e,0x66,0xc3,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x78,0x7e,0x66,0x66,0x7c,0x7e,0x66,0x67,0x7e,0x7e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x0c,0x3e,0x72,0x60,0x60,0x60,0x60,0x70,0x3e,0x1e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x70,0x7c,0x6e,0x66,0x66,0x66,0x66,0x66,0x7e,0x7c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x7e,0x7e,0x60,0x60,0x7e,0x7e,0x60,0x60,0x7e,0x7e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x3e,0x7e,0x60,0x60,0x7e,0x7e,0x60,0x60,0x60,0x60,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x1c,0x3e,0x72,0x60,0x60,0x6e,0x66,0x66,0x7e,0x3e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x66,0x66,0x66,0x66,0x7e,0x7e,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x7e,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x7e,0x7e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x1c,0x3e,0x0e,0x0e,0x0e,0x0e,0x0e,0x0e,0x7c,0x7c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x42,0x66,0x6c,0x78,0x78,0x7c,0x6c,0x6e,0x66,0x67,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x20,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7e,0x7e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x66,0x66,0x6e,0x7e,0x7e,0x5a,0x5a,0x42,0x42,0x42,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x62,0x66,0x76,0x76,0x76,0x7e,0x6e,0x6e,0x6e,0x66,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x18,0x7c,0x66,0x66,0x66,0x66,0x66,0x66,0x7e,0x3c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x78,0x7e,0x66,0x66,0x66,0x7e,0x78,0x60,0x60,0x60,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x18,0x7c,0x66,0x66,0x66,0x66,0x66,0x66,0x7e,0x3c,0x0e,0x04,0x00,0x00 },
    { 0x00,0x00,0x78,0x7e,0x66,0x66,0x6e,0x7c,0x6c,0x66,0x66,0x67,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x1c,0x7e,0x60,0x60,0x78,0x3e,0x06,0x06,0x7e,0x7c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x7e,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x42,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x7e,0x3c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x42,0x66,0x66,0x66,0x66,0x2c,0x3c,0x3c,0x3c,0x38,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x81,0xc3,0xc3,0xdb,0xdb,0x7e,0x7e,0x7e,0x66,0x66,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x42,0x66,0x6e,0x3c,0x18,0x18,0x3c,0x3c,0x66,0xe7,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x42,0x66,0x66,0x3c,0x3c,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x7e,0x7e,0x06,0x0c,0x1c,0x18,0x30,0x70,0x7e,0x7f,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x1c,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x1c,0x1c,0x00,0x00 },
    { 0x00,0x00,0x40,0x60,0x20,0x30,0x10,0x18,0x18,0x0c,0x0c,0x04,0x06,0x00,0x00,0x00 },
    { 0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x38,0x38,0x00,0x00 },
    { 0x00,0x00,0x18,0x3c,0x3c,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00 },
    { 0x00,0x00,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x7e,0x06,0x3e,0x7e,0x66,0x6e,0x7e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x60,0x60,0x60,0x7e,0x76,0x66,0x66,0x66,0x7e,0x7c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x3e,0x72,0x60,0x60,0x60,0x72,0x3e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x06,0x06,0x06,0x7e,0x6e,0x66,0x66,0x66,0x7e,0x7e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x3c,0x66,0x66,0x7e,0x60,0x76,0x3e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x0e,0x1e,0x18,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x7e,0x6e,0x66,0x66,0x66,0x7e,0x3e,0x06,0x7e,0x38,0x00 },
    { 0x00,0x00,0x60,0x60,0x60,0x7e,0x76,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00 },
    { 0x00,0x18,0x18,0x18,0x00,0x78,0x18,0x18,0x18,0x18,0x7e,0x7f,0x00,0x00,0x00,0x00 },
    { 0x00,0x08,0x1c,0x08,0x00,0x3c,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0x78,0x70,0x00 },
    { 0x00,0x00,0x60,0x60,0x60,0x66,0x6c,0x78,0x7c,0x6c,0x66,0x66,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x70,0x70,0x30,0x30,0x30,0x30,0x30,0x38,0x1e,0x1e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0xfe,0xda,0xda,0xdb,0xdb,0xdb,0xdb,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x7e,0x76,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x3c,0x66,0x66,0x66,0x66,0x7e,0x3c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x7e,0x76,0x66,0x66,0x66,0x7e,0x7c,0x60,0x60,0x60,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x7e,0x6e,0x66,0x66,0x66,0x7e,0x7e,0x06,0x06,0x06,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x3e,0x38,0x30,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x3c,0x60,0x70,0x3c,0x06,0x46,0x7c,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x38,0x38,0x7e,0x38,0x38,0x38,0x38,0x1c,0x1e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x7e,0x3e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x3c,0x3c,0x3c,0x18,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0xc3,0xc3,0xdb,0x5a,0x7e,0x7e,0x66,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x66,0x3c,0x3c,0x18,0x3c,0x7e,0x66,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x3c,0x3c,0x1c,0x18,0x18,0x70,0x60,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x7e,0x0e,0x0c,0x18,0x30,0x70,0x7e,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x0e,0x1c,0x18,0x18,0x18,0x38,0x70,0x18,0x18,0x18,0x18,0x1e,0x00,0x00 },
    { 0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00 },
    { 0x00,0x00,0x70,0x38,0x18,0x18,0x18,0x1c,0x0e,0x18,0x18,0x18,0x18,0x70,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7e,0x5e,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
};
static inline uint32_t rd32(uint64_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uint64_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline uint64_t dbg_ticks(void)
{
 uint64_t v;
 __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
 return v;
}
static inline uint64_t dbg_freq(void)
{
 uint64_t f;
 __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
 return f ? f : 13000000u;
}
static inline void dbg_delay_us(uint32_t us)
{
 uint64_t end = dbg_ticks() + (dbg_freq() / 1000000u) * us + 1;
 uint32_t spin = us > (1u << 26) / 200u
                     ? (1u << 26)
                     : us * 200u;
 while (dbg_ticks() < end) {
  if (--spin == 0) {
   return;
  }
 }
}
static inline void dbg_delay_ms(uint32_t ms)
{
 uint64_t end = dbg_ticks() + dbg_freq() / 1000 * ms;
 uint32_t spin = ms > (1u << 26) / (1000u * 200u)
                     ? (1u << 26)
                     : ms * 1000u * 200u;
 while (dbg_ticks() < end) {
  if (--spin == 0) {
   return;
  }
 }
}
static inline void wdt_disable(void)
{
 wr32((0x1c007000ULL + 0x08), 0x1971u);
 wr32((0x1c007000ULL + 0x00), 0x22000000u);
}
static inline void __attribute__((noreturn)) wdt_reset(void)
{
 uint32_t t = 2;
 wr32((0x1c007000ULL + 0x04), ((t * 32768u / 512u) << 5) | 0x8u);
 wr32((0x1c007000ULL + 0x08), 0x1971u);
 wr32((0x1c007000ULL + 0x00), 0x22000000u | 0x1u);
 wr32((0x1c007000ULL + 0x14), 0x1209u);
 for (;;) {
  __asm__ volatile("wfe");
 }
}
static inline void wdt_arm(uint32_t seconds)
{
 uint32_t t = seconds > 31 ? 31 : seconds;
 wr32((0x1c007000ULL + 0x04), ((t * 32768u / 512u) << 5) | 0x8u);
 wr32((0x1c007000ULL + 0x08), 0x1971u);
 wr32((0x1c007000ULL + 0x00), 0x22000000u | 0x1u);
}
struct ramoops_hdr {
 uint32_t sig;
 uint32_t start;
 uint32_t size;
};
static inline void plog_reset(void) { }
static inline void plog_putc(char c) { (void)c; }
struct fbcon {
 uint64_t base;
 uint32_t pitch, w, h;
 uint32_t x, y, top, scale;
 uint32_t fg, bg;
};
static struct fbcon fbc;
static inline void fb_px(uint32_t x, uint32_t y, uint32_t c)
{
 *(volatile uint32_t *)(fbc.base + (uint64_t)y * fbc.pitch + (uint64_t)x * 4) = c;
}
static inline void fb_rect(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t c)
{
 if (!fbc.base) {
  return;
 }
 for (uint32_t y = y0; y < y0 + h && y < fbc.h; y++) {
  for (uint32_t x = x0; x < x0 + w && x < fbc.w; x++) {
   fb_px(x, y, c);
  }
 }
}
static inline void fbcon_init(uint64_t base, uint32_t pitch, uint32_t top, uint32_t scale)
{
 fbc.base = base;
 fbc.pitch = pitch;
 fbc.w = 1080u;
 fbc.h = 2460u;
 fbc.x = 0;
 fbc.y = fbc.top = top;
 fbc.scale = scale;
 fbc.fg = 0xffffffffu;
 fbc.bg = 0xff000000u;
}
static inline void fbcon_newline(void)
{
 uint32_t lh = 16 * fbc.scale;
 fbc.x = 0;
 fbc.y += lh;
 if (fbc.y + lh > fbc.h) {
  fbc.y = fbc.top;
 }
 fb_rect(0, fbc.y, fbc.w, lh, fbc.bg);
 if (fbc.y + 2 * lh <= fbc.h) {
  fb_rect(0, fbc.y + lh, fbc.w, 2, 0xffff3030u);
 }
}
static inline void fbcon_putc(char c)
{
 uint32_t cw = 8 * fbc.scale;
 if (!fbc.base) {
  return;
 }
 if (c == '\n') {
  fbcon_newline();
  return;
 }
 if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) {
  c = '?';
 }
 if (fbc.x + cw > fbc.w) {
  fbcon_newline();
 }
 const unsigned char *g = font8x16[(unsigned char)c - 0x20];
 for (uint32_t row = 0; row < 16 * fbc.scale; row++) {
  unsigned char bits = g[row / fbc.scale];
  for (uint32_t col = 0; col < cw; col++) {
   fb_px(fbc.x + col, fbc.y + row, (bits & (0x80 >> (col / fbc.scale))) ? fbc.fg : fbc.bg);
  }
 }
 fbc.x += cw;
}
extern char *dbg_tee;
extern uint32_t dbg_tee_cap, dbg_tee_at;
static inline void dbg_tee_putc(char c)
{
 if (dbg_tee && dbg_tee_at < dbg_tee_cap) {
  dbg_tee[dbg_tee_at++] = c;
 }
}
static inline void dbg_putc(char c)
{
 dbg_tee_putc(c);
 plog_putc(c);
 fbcon_putc(c);
}
static inline void dbg_puts(const char *s)
{
 while (*s) {
  dbg_putc(*s++);
 }
}
static inline void dbg_hex(uint64_t v, int digits)
{
 static const char hex[] = "0123456789abcdef";
 for (int i = (digits - 1) * 4; i >= 0; i -= 4) {
  dbg_putc(hex[(v >> i) & 0xf]);
 }
}
static inline void dbg_dec(uint64_t v)
{
 char buf[21];
 int n = 0;
 do {
  buf[n++] = (char)('0' + v % 10);
  v /= 10;
 } while (v);
 while (n) {
  dbg_putc(buf[--n]);
 }
}
static inline void dbg_dump(const char *name, uint64_t base, uint32_t from, uint32_t to, int quiet)
{
 uint64_t fb = fbc.base;
 if (quiet) {
  fbc.base = 0;
 }
 for (uint32_t off = from; off < to; off += 16) {
  dbg_puts(name);
  dbg_putc('+');
  dbg_hex(off, 3);
  dbg_putc(':');
  for (uint32_t i = 0; i < 16 && off + i < to; i += 4) {
   dbg_putc(' ');
   dbg_hex(rd32(base + off + i), 8);
  }
  dbg_putc('\n');
 }
 fbc.base = fb;
}
static uint64_t msdc_base = 0x11230000ULL;
struct msdc_err {
 const char *what;
 uint32_t status;
};
static struct msdc_err msdc_last;
static uint32_t msdc_int_seen;
static int msdc_fail(const char *what, uint32_t status)
{
 msdc_last.what = what;
 msdc_last.status = status;
 return 0;
}
static int msdc_wait_idle(void)
{
 for (uint32_t i = 0; i < 2000000; i++) {
  if (!(rd32(msdc_base + 0x3c) & ((1u << 0) | (1u << 1)))) {
   return 1;
  }
 }
 return msdc_fail("controller busy", rd32(msdc_base + 0x3c));
}
static void msdc_fifo_clear(void)
{
 wr32(msdc_base + 0x14, rd32(msdc_base + 0x14) | (1u << 31));
 for (uint32_t i = 0; i < 100000 && (rd32(msdc_base + 0x14) & (1u << 31)); i++) {
 }
}
static int msdc_wait_int(uint32_t want, uint32_t bad, const char *what)
{
 for (uint32_t i = 0; i < 20000000; i++) {
  uint32_t st = rd32(msdc_base + 0x0c);
  if (st) {
   wr32(msdc_base + 0x0c, st);
   msdc_int_seen |= st;
  }
  if (msdc_int_seen & bad) {
   uint32_t bits = msdc_int_seen;
   msdc_int_seen = 0;
   return msdc_fail(what, bits);
  }
  if (msdc_int_seen & want) {
   msdc_int_seen &= ~want;
   return 1;
  }
 }
 return msdc_fail(what, msdc_int_seen);
}
static void msdc_pio_mode(void)
{
 wr32(msdc_base + 0x00, rd32(msdc_base + 0x00) | (1u << 3));
 wr32(msdc_base + 0x0c, rd32(msdc_base + 0x0c));
 msdc_int_seen = 0;
 msdc_fifo_clear();
}
static int __attribute__((unused)) msdc_read(uint64_t lba, uint32_t blocks, void *dst)
{
 volatile uint32_t *out = (volatile uint32_t *)dst;
 if (blocks == 0) {
  return 1;
 }
 if (!msdc_wait_idle()) {
  return 0;
 }
 msdc_pio_mode();
 wr32(msdc_base + 0x50, blocks);
 wr32(msdc_base + 0x38, (uint32_t)lba);
 wr32(msdc_base + 0x34, ((blocks == 1 ? 17 : 18) & 0x3f) |
     (1u << 7) | (blocks == 1 ? (1u << 11) : (2u << 11)) | (((512) & 0xfff) << 16));
 if (!msdc_wait_int((1u << 8), (1u << 9) | (1u << 10), "command")) {
  return 0;
 }
 for (uint32_t left = blocks * 512 / 4; left; ) {
  uint32_t fifo = rd32(msdc_base + 0x14) & 0xff, spin = 0;
  while (fifo < 4) {
   uint32_t st = rd32(msdc_base + 0x0c);
   if (st & ((1u << 14) | (1u << 15))) {
    wr32(msdc_base + 0x0c, st);
    return msdc_fail("data", st);
   }
   if (++spin > 20000000) {
    return msdc_fail("data stalled", rd32(msdc_base + 0x14));
   }
   fifo = rd32(msdc_base + 0x14) & 0xff;
  }
  uint32_t words = fifo / 4;
  if (words > left) {
   words = left;
  }
  for (uint32_t i = 0; i < words; i++) {
   *out++ = rd32(msdc_base + 0x1c);
  }
  left -= words;
 }
 if (!msdc_wait_int((1u << 12), (1u << 14) | (1u << 15), "transfer end")) {
  return 0;
 }
 if (blocks > 1) {
  wr32(msdc_base + 0x38, 0);
  wr32(msdc_base + 0x34, ((12) & 0x3f) | (6u << 7) | (1u << 14));
  if (!msdc_wait_int((1u << 8), (1u << 9) | (1u << 10), "stop")) {
   return 0;
  }
  if (!msdc_wait_idle()) {
   return 0;
  }
 }
 return 1;
}
static void (*ufs_trace)(const char *) = 0;
static void (*ufs_trace_hex_fn)(uint64_t) = 0;
struct ufs_err {
 const char *what;
 uint32_t status;
};
static struct ufs_err ufs_last;
struct ufs_slot {
 volatile uint32_t *utrd;
 volatile uint32_t *ucd;
 uint64_t ucd_pa;
 uint32_t bytes;
};
static struct ufs_slot ufs_slots[32];
static uint32_t ufs_lbs = 4096u;
static int ufs_lbs_known;
static uint64_t ufs_base = 0x112b0000ULL;
static uint32_t ufs_nutrs;
static int ufs_reset_done;
static uint32_t ufs_hce_after_reset, ufs_hce_enabled, ufs_hcs_after_enable;
static uint32_t ufs_hcs_trace[10];
static const char *ufs_hcs_where[10];
static uint32_t ufs_ring_trace[5];
static const char *ufs_ring_where[5];
static uint32_t ufs_tail_wrote_rsr, ufs_tail_rsr_before;
static uint32_t ufs_hiber_exit_ok;
static uint32_t ufs_run_calls;
static uint32_t ufs_first_hcs, ufs_first_rsr, ufs_first_is_before, ufs_first_is_after;
static uint32_t ufs_first_dbr_before, ufs_first_dbr_after, ufs_first_ocs, ufs_first_exit;
static uint8_t ufs_first_resp[8];
static uint32_t ufs_stale_clears;
static uint32_t ufs_mtk_xoufs_before, ufs_mtk_xoufs_after;
static uint32_t ufs_mtk_refclk_before, ufs_mtk_refclk_after;
static uint32_t ufs_mtk_refclk_ack;
static uint32_t ufs_sip_ref_pre;
static uint32_t ufs_sip_ref_post;
static uint32_t ufs_sip_mtcmos_off;
static uint32_t ufs_sip_mtcmos_on;
static uint32_t ufs_hce_init;
static uint32_t ufs_hce_after_set;
static uint32_t ufs_hce_at_ring;
static uint32_t ufs_post_link_rsr;
static uint32_t ufs_post_link_dbr;
static uint32_t ufs_last_is;
static uint32_t ufs_last_dbr;
static uint32_t ufs_pdn_before, ufs_pdn_asserted, ufs_pdn_after;
static uint32_t ufs_ao_before, ufs_ao_asserted, ufs_ao_after;
static uint32_t ufs_lk_list_lo, ufs_lk_list_hi;
static uint32_t ufs_lk_cold_lo, ufs_lk_cold_hi;
static uint32_t ufs_lk_utrd[8];
static int ufs_lk_saved;
static uint32_t ufs_moved_lo, ufs_moved_hi;
static uint32_t ufs_base_tries_used;
static uint32_t ufs_base_hi_wanted;
static uint32_t ufs_hcs_after_stop, ufs_hcs_after_clr;
static uint32_t ufs_utmrl_rsr_at_stop, ufs_utmrl_rsr_after;
static int ufs_64as;
static uint32_t ufs_hce_before, ufs_hce_after;
static uint32_t ufs_uic_status;
static uint32_t ufs_uic_step;
static uint32_t ufs_uic_hcs;
static uint32_t ufs_is_before, ufs_door_before;
static uint32_t ufs_run_after_stop, ufs_door_after_clear;
static uint32_t ufs_restart_rsr;
static uint32_t ufs_rsr_at_ring, ufs_rsr_after_ring;
static uint32_t ufs_rsr_restarted;
static uint32_t ufs_hcs_at_ring;
static uint32_t ufs_dbr_at_ring;
static uint32_t ufs_exit_reason;
static uint32_t ufs_is_before_ring;
static uint32_t ufs_door_after_ring;
static uint32_t ufs_link_ran;
static uint32_t ufs_link_skipped;
static uint32_t ufs_nop_ocs, ufs_nop_resp0, ufs_nop_resp6;
static uint32_t ufs_query_ocs, ufs_query_resp, ufs_query_rsp0;
static uint32_t ufs_last_resp6, ufs_last_status7;
static uint8_t ufs_tur_resp[16];
static uint32_t ufs_tur_resp_seen;
static uint32_t ufs_fdevinit_stage, ufs_fdevinit_flag, ufs_fdevinit_tries;
static uint32_t ufs_io_stage, ufs_io_fdev;
static uint32_t ufs_hs_stage, ufs_hs_ok, ufs_hs_upmcrs, ufs_hs_res;
static uint32_t ufs_hs_gear_rx, ufs_hs_gear_tx, ufs_hs_lane_rx, ufs_hs_lane_tx;
static uint32_t ufs_run_polls[1];
static uint32_t ufs_sip_va09_ok, ufs_sip_devpwr_ok, ufs_sip_rst_lo, ufs_sip_rst_hi;
static uint32_t ufs_device_woke;
static uint32_t ufs_tur_ok;
static uint32_t ufs_list_lo, ufs_list_hi;
static int ufs_list_is_lk;
static int ufs_ready;
static int ufs_failed;
static int ufs_fail(const char *what, uint32_t status)
{
 ufs_last.what = what;
 ufs_last.status = status;
 return 0;
}
static inline uint32_t ufs_rd(uint32_t off)
{
 return *(volatile uint32_t *)(uintptr_t)(ufs_base + off);
}
static inline void ufs_wr(uint32_t off, uint32_t v)
{
 __asm__ volatile("dsb sy" ::: "memory");
 *(volatile uint32_t *)(uintptr_t)(ufs_base + off) = v;
 __asm__ volatile("dsb sy" ::: "memory");
}
struct ufs_smc_ret { uint64_t a0, a1, a2, a3; };
static struct ufs_smc_ret ufs_smc(uint64_t fn, uint64_t a1, uint64_t a2,
                                  uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
 register uint64_t x0 __asm__("x0") = fn;
 register uint64_t x1 __asm__("x1") = a1;
 register uint64_t x2 __asm__("x2") = a2;
 register uint64_t x3 __asm__("x3") = a3;
 register uint64_t x4 __asm__("x4") = a4;
 register uint64_t x5 __asm__("x5") = a5;
 register uint64_t x6 __asm__("x6") = a6;
 __asm__ volatile("smc #0"
                  : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
                  : "r"(x4), "r"(x5), "r"(x6)
                  : "memory");
 struct ufs_smc_ret r;
 r.a0 = x0; r.a1 = x1; r.a2 = x2; r.a3 = x3;
 return r;
}
static struct ufs_smc_ret ufs_mtk_ref_clk_notify(uint64_t on, uint64_t stage)
{
 return ufs_smc(0xC2000276ULL, (1ULL << 3), on, stage, 0, 0, 0);
}
static struct ufs_smc_ret ufs_mtk_mtcmos_ctrl(uint64_t on)
{
 return ufs_smc(0xC2000276ULL, (1ULL << 9), on, 0, 0, 0, 0);
}
static struct ufs_smc_ret ufs_mtk_device_reset_ctrl(uint64_t high)
{
 return ufs_smc(0xC2000276ULL, (1ULL << 1), high, 0, 0, 0, 0);
}
static struct ufs_smc_ret ufs_mtk_va09_pwr_ctrl(uint64_t on)
{
 return ufs_smc(0xC2000276ULL, (1ULL << 0), on, 0, 0, 0, 0);
}
static struct ufs_smc_ret ufs_mtk_device_pwr_ctrl(uint64_t on, uint64_t ufs_version)
{
 return ufs_smc(0xC2000276ULL, (1ULL << 7), on, ufs_version, 0, 0, 0);
}
static void ufs_put(void *to, const void *from, uint32_t bytes)
{
 volatile uint32_t *d = (volatile uint32_t *)to;
 const uint8_t *s = (const uint8_t *)from;
 for (uint32_t i = 0; i < bytes / 4; i++) {
  uint32_t w;
  __builtin_memcpy(&w, s + 4 * i, 4);
  d[i] = w;
 }
}
static void ufs_zero(void *to, uint32_t bytes)
{
 volatile uint32_t *d = (volatile uint32_t *)to;
 for (uint32_t i = 0; i < bytes / 4; i++) {
  d[i] = 0;
 }
}
static uint64_t ufs_place_pa;
static uint64_t ufs_prd_dst;
static uint32_t ufs_place_bytes;
static uint32_t ufs_canary_back;
static void ufs_cache_sync(volatile void *p, uint32_t len)
{
 uintptr_t a = (uintptr_t)p & ~(uintptr_t)63u;
 uintptr_t end = ((uintptr_t)p + len + 63u) & ~(uintptr_t)63u;
 __asm__ volatile("dsb sy" ::: "memory");
 for (; a < end; a += 64u) {
  __asm__ volatile("dc civac, %0" : : "r"(a) : "memory");
 }
 __asm__ volatile("dsb sy" ::: "memory");
}
static uint8_t ufs_lun = 2;
static uint32_t ufs_probe_ocs[6];
static uint32_t ufs_probe_resp[6];
static uint32_t ufs_probe_door[6];
static uint32_t ufs_probe_is[6];
static uint64_t ufs_probe_base[6];
static int ufs_place_which(struct ufs_slot *s, const uint8_t *upiu, uint32_t dw0, uint32_t bytes,
      int which)
{
 uint32_t prds = 0;
 ufs_place_pa = s->ucd_pa;
 ufs_place_bytes = bytes;
 s->bytes = bytes;
 *(volatile uint32_t *)((uint8_t *)s->ucd + 512 + 60) = 0xc0de1234u;
 __asm__ volatile("dsb sy" ::: "memory");
 ufs_canary_back = *(volatile uint32_t *)((uint8_t *)s->ucd + 512 + 60);
 ufs_put((void *)s->ucd + 0, upiu, 32);
 ufs_zero((void *)s->ucd + 0 + 32, 1024 - 32);
 for (uint32_t at = 0; at < bytes; at += (256u << 10), prds++) {
  volatile uint32_t *prd = (volatile uint32_t *)((uint8_t *)s->ucd + 1024 + 16 * prds);
  uint64_t pa = (ufs_prd_dst ? ufs_prd_dst : (uint64_t)(uintptr_t)((uint8_t *)(uintptr_t)(0x60100000ULL + (0x1000u + 32 * (((1024 + 16 * ((8u << 20) / (256u << 10) + 1)) + 127u) & ~127u))))) + at;
  uint32_t len = bytes - at < (256u << 10) ? bytes - at : (256u << 10);
  prd[0] = (uint32_t)pa;
  prd[1] = (uint32_t)(pa >> 32);
  prd[2] = 0;
  prd[3] = len - 1u;
 }
 for (uint32_t i = 0; i < 8; i++) {
  s->utrd[i] = 0;
 }
 s->utrd[0] = dw0;
 s->utrd[1] = 0;
 s->utrd[2] = 0x0fu;
 s->utrd[3] = 0;
 s->utrd[4] = (uint32_t)s->ucd_pa;
 s->utrd[5] = (uint32_t)(s->ucd_pa >> 32);
 s->utrd[6] = (512 / 4) << 16 | (512 / 4);
 s->utrd[7] = (1024 / 4) << 16 | prds;
 __asm__ volatile("dsb sy" ::: "memory");
 return 1;
}
static int ufs_layout = 0;
static uint32_t ufs_layout_tried[4];
static int ufs_place(struct ufs_slot *s, const uint8_t *upiu, uint32_t dw0, uint32_t bytes)
{
 return ufs_place_which(s, upiu, dw0, bytes, ufs_layout);
}
static int ufs_run(uint32_t *ocs_out)
{
 struct ufs_slot *s = &ufs_slots[0];
 uint64_t deadline = dbg_ticks() + dbg_freq() / 1000u * 10000u;
 uint32_t waited = 0;
 uint32_t is;
 uint32_t polls = (1u << 24);
 if (s->bytes != 0u) {
  uint64_t need = (uint64_t)s->bytes * 2u;
  if (need > (uint64_t)polls) {
   polls = need > 0xffffffffull ? 0xffffffffu : (uint32_t)need;
  }
 }
 ufs_run_calls++;
 ufs_ring_where[0] = "run:start"; ufs_ring_trace[0] = ufs_rd(0x30);
 if (ufs_run_calls == 1) {
  ufs_first_hcs = ufs_ring_trace[0];
  ufs_first_is_before = ufs_rd(0x20);
  ufs_first_dbr_before = ufs_rd(0x58);
 }
 ufs_cache_sync(s->utrd, 32);
 ufs_cache_sync(s->ucd, 1024 + 16u * ((8u << 20) / (256u << 10) + 1));
 if (s->bytes) {
  ufs_cache_sync(ufs_prd_dst ? (volatile void *)(uintptr_t)ufs_prd_dst : ((uint8_t *)(uintptr_t)(0x60100000ULL + (0x1000u + 32 * (((1024 + 16 * ((8u << 20) / (256u << 10) + 1)) + 127u) & ~127u)))),
          s->bytes);
 }
 ufs_ring_where[1] = "run:cache"; ufs_ring_trace[1] = ufs_rd(0x30);
 ufs_is_before_ring = ufs_rd(0x20);
 ufs_hce_at_ring = ufs_rd(0x34);
 ufs_ring_where[2] = "run:prersr"; ufs_ring_trace[2] = ufs_rd(0x30);
 ufs_rsr_at_ring = ufs_rd(0x60);
 ufs_rsr_restarted = 0;
 if ((ufs_rsr_at_ring & 1u) == 0) {
  ufs_wr(0x60, 1u);
  __asm__ volatile("dsb sy" ::: "memory");
  for (uint32_t n = 0; n < 10000u; n++) {
   if ((ufs_rd(0x60) & 1u) != 0) { break; }
  }
  ufs_rsr_restarted = 1;
 }
 ufs_rsr_after_ring = ufs_rd(0x60);
 ufs_hcs_at_ring = ufs_rd(0x30);
 ufs_dbr_at_ring = ufs_rd(0x58);
 __asm__ volatile("dsb sy" ::: "memory");
 ufs_ring_where[3] = "run:prebell"; ufs_ring_trace[3] = ufs_rd(0x30);
 if (ufs_run_calls == 1) {
  ufs_first_rsr = ufs_rsr_at_ring;
  ufs_first_dbr_after = ufs_door_after_ring;
 }
 do { if (ufs_trace) { ufs_trace("r:bell\n"); } } while (0);
 ufs_wr(0x58, 1u);
 ufs_ring_where[4] = "run:postbell"; ufs_ring_trace[4] = ufs_rd(0x30);
 ufs_door_after_ring = ufs_rd(0x58);
 if ((ufs_door_after_ring & 1u) == 0) {
  ufs_wr(0x58, 1u);
  __asm__ volatile("dsb sy" ::: "memory");
  ufs_door_after_ring = ufs_rd(0x58);
 }
 for (;;) {
  is = ufs_rd(0x20);
  if ((is & 0x1u) != 0 && (ufs_is_before_ring & 0x1u) == 0) {
   ufs_exit_reason = 1;
   break;
  }
  if ((ufs_door_after_ring & 1u) != 0 && (ufs_rd(0x58) & 1u) == 0) {
   ufs_exit_reason = 2;
   break;
  }
  if ((is & ((1u << 16) | (1u << 17))) != 0) {
   ufs_exit_reason = 3;
   return ufs_fail("controller error", is);
  }
  ufs_last_is = is;
  waited++;
  if (dbg_ticks() >= deadline) {
   ufs_last_dbr = ufs_rd(0x58);
   ufs_exit_reason = 4;
   return ufs_fail("doorbell timeout", ufs_last_dbr);
  }
  if (--polls == 0) {
   ufs_last_dbr = ufs_rd(0x58);
   ufs_exit_reason = 4;
   return ufs_fail("doorbell timeout (no timebase: counted polls)", ufs_last_dbr);
  }
 }
 ufs_run_polls[0] = waited;
 ufs_cache_sync(s->utrd, 32);
 ufs_cache_sync(s->ucd, 1024 + 16u * ((8u << 20) / (256u << 10) + 1));
 if (s->bytes) {
  ufs_cache_sync(ufs_prd_dst ? (volatile void *)(uintptr_t)ufs_prd_dst : ((uint8_t *)(uintptr_t)(0x60100000ULL + (0x1000u + 32 * (((1024 + 16 * ((8u << 20) / (256u << 10) + 1)) + 127u) & ~127u)))),
          s->bytes);
 }
 *ocs_out = s->utrd[2] & 0xffu;
 if (ufs_run_calls == 1) {
  ufs_first_is_after = ufs_last_is;
  ufs_first_exit = ufs_exit_reason;
  ufs_first_ocs = *ocs_out;
  for (uint32_t b = 0; b < 8u; b++) {
   ufs_first_resp[b] = *(volatile const uint8_t *)((uint8_t *)s->ucd + 512 + b);
  }
 }
 return 1;
}
static uint8_t ufs_response_status(struct ufs_slot *s)
{
 volatile const uint8_t *r = (volatile const uint8_t *)s->ucd + 512;
 return r[6];
}
static uint8_t ufs_scsi_status(struct ufs_slot *s)
{
 volatile const uint8_t *r = (volatile const uint8_t *)s->ucd + 512;
 return r[7];
}
static int ufs_reply_ok(struct ufs_slot *s)
{
 uint8_t resp = ufs_response_status(s);
 uint8_t st = ufs_scsi_status(s);
 ufs_last_resp6 = resp;
 ufs_last_status7 = st;
 return resp == 0 && (st & 0x3fu) == 0;
}
static uint32_t ufs_reset_ok, ufs_enable_ok;
static uint32_t ufs_reset_hcs, ufs_enable_hcs;
static uint32_t ufs_link_uecpa, ufs_link_uecpa_after;
static uint32_t ufs_link_hcs_before, ufs_link_hcs_after;
static uint32_t ufs_link_is, ufs_link_ok;
static uint32_t ufs_link_result;
static uint32_t ufs_uic_result;
static uint32_t ufs_link_already_up;
static uint32_t ufs_pre_lpm_ok, ufs_pre_lpm_res;
static uint32_t ufs_pre_txlcc_ok, ufs_pre_txlcc_res;
static uint32_t ufs_pre_stall_ok, ufs_pre_stall_res, ufs_pre_stall_val;
static uint32_t ufs_uic_status, ufs_uic_hcs_after, ufs_uic_is_after;
static int ufs_uic_cmd4(uint32_t command, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
 uint32_t loops = 200000;
 uint32_t is;
 if ((ufs_rd(0x30) & (1u << 3)) == 0) {
  return 0;
 }
 ufs_wr(0x20, (1u << 5) | (1u << 10) |
                (1u << 8) | 0x1u);
 ufs_wr(0x94, arg1);
 ufs_wr(0x98, arg2);
 ufs_wr(0x9c, arg3);
 __asm__ volatile("dsb sy" ::: "memory");
 ufs_wr(0x90, command);
 __asm__ volatile("dsb sy" ::: "memory");
 for (;;) {
  is = ufs_rd(0x20);
  if ((is & ((1u << 5) | (1u << 10) |
             (1u << 8))) != 0) {
   break;
  }
  if (--loops == 0) {
   break;
  }
 }
 ufs_uic_status = is;
 ufs_uic_hcs_after = ufs_rd(0x30);
 ufs_uic_is_after = is;
 ufs_uic_result = ufs_rd(0x98) & 0xffu;
 ufs_wr(0x20, (1u << 5) | (1u << 10) |
               (1u << 8));
 __asm__ volatile("dsb sy" ::: "memory");
 if ((is & ((1u << 5) | (1u << 10) |
            (1u << 8))) == 0) {
  return 0;
 }
 return ufs_uic_result == 0;
}
static int ufs_uic_cmd(uint32_t command, uint32_t arg1)
{
 return ufs_uic_cmd4(command, arg1, 0, 0);
}
static int ufs_dme_set(uint32_t attr, uint32_t value, uint32_t *out_result)
{
 int ok = ufs_uic_cmd4(0x02u, attr << 16, 0, value);
 if (out_result) {
  *out_result = ufs_uic_result;
 }
 return ok;
}
static int ufs_dme_get(uint32_t attr, uint32_t *out_value, uint32_t *out_result)
{
 int ok = ufs_uic_cmd4(0x01u, attr << 16, 0, 0);
 if (out_value) {
  *out_value = ufs_rd(0x9c);
 }
 if (out_result) {
  *out_result = ufs_uic_result;
 }
 return ok;
}
static int ufs_dme_peer_get(uint32_t attr, uint32_t *out_value, uint32_t *out_result)
{
 int ok = ufs_uic_cmd4(0x03u, attr << 16, 0, 0);
 if (out_value) {
  *out_value = ufs_rd(0x9c);
 }
 if (out_result) {
  *out_result = ufs_uic_result;
 }
 return ok;
}
static int ufs_pwr_mode_set(uint32_t mode, uint32_t *out_upmcrs)
{
 uint32_t hcs = 0;
 uint32_t loops = 200000u;
 ufs_wr(0x20, (1u << 10));
 ufs_wr(0x94, 0x1571u << 16);
 ufs_wr(0x98, 0);
 ufs_wr(0x9c, mode);
 __asm__ volatile("dsb sy" ::: "memory");
 ufs_wr(0x90, 0x02u);
 __asm__ volatile("dsb sy" ::: "memory");
 while (loops-- != 0u) {
  hcs = ufs_rd(0x30);
  if (((hcs >> 8u) & 0x7u) == 1u) {
   break;
  }
  if ((ufs_rd(0x20) & (1u << 10)) != 0) {
   hcs = ufs_rd(0x30);
   break;
  }
 }
 hcs = ufs_rd(0x30);
 ufs_wr(0x20, (1u << 10));
 __asm__ volatile("dsb sy" ::: "memory");
 if (out_upmcrs) {
  *out_upmcrs = (hcs >> 8u) & 0x7u;
 }
 return ((hcs >> 8u) & 0x7u) == 1u;
}
static int ufs_hs_negotiate(void)
{
 uint32_t gear_rx = 0, gear_tx = 0, lane_rx = 0, lane_tx = 0;
 uint32_t res = 0, upmcrs = 0;
 uint32_t fast;
 ufs_hs_stage = 1;
 if (!ufs_dme_get(0x1587u, &gear_rx, &res) || gear_rx == 0u) {
  ufs_hs_stage = 2;
  ufs_hs_gear_rx = gear_rx; ufs_hs_res = res;
  return 0;
 }
 ufs_dme_peer_get(0x1587u, &gear_tx, &res);
 if (gear_tx == 0u) {
  ufs_hs_stage = 3;
  ufs_hs_gear_rx = gear_rx; ufs_hs_res = res;
  return 0;
 }
 ufs_dme_get(0x1581u, &lane_rx, &res);
 ufs_dme_get(0x1561u, &lane_tx, &res);
 if (lane_rx == 0u || lane_tx == 0u) {
  ufs_hs_stage = 4;
  ufs_hs_gear_rx = gear_rx; ufs_hs_gear_tx = gear_tx;
  ufs_hs_lane_rx = lane_rx; ufs_hs_lane_tx = lane_tx; ufs_hs_res = res;
  return 0;
 }
 ufs_hs_gear_rx = gear_rx; ufs_hs_gear_tx = gear_tx;
 ufs_hs_lane_rx = lane_rx; ufs_hs_lane_tx = lane_tx;
 ufs_hs_stage = 5;
 fast = 1u;
 ufs_dme_set(0x1583u, gear_rx, &res);
 ufs_dme_set(0x1580u, lane_rx, &res);
 ufs_dme_set(0x1584u, fast, &res);
 ufs_dme_set(0x1568u, gear_tx, &res);
 ufs_dme_set(0x1560u, lane_tx, &res);
 ufs_dme_set(0x1569u, fast, &res);
 ufs_dme_set(0x156Au, 1u, &res);
 ufs_dme_set(0x15B0u, 0u, &res);
 ufs_dme_set(0x15B0u + 1u, 0u, &res);
 ufs_dme_set(0x15B0u + 2u, 0u, &res);
 ufs_dme_set(0x15B0u + 3u, 0u, &res);
 ufs_dme_set(0x15B0u + 4u, 0u, &res);
 ufs_dme_set(0x15B0u + 5u, 0u, &res);
 ufs_dme_set(0xD041u, 8191u, &res);
 ufs_dme_set(0xD042u, 65535u, &res);
 ufs_dme_set(0xD043u, 32767u, &res);
 ufs_hs_stage = 6;
 ufs_hs_ok = (uint32_t)ufs_pwr_mode_set((gear_rx << 4) | gear_tx, &upmcrs);
 ufs_hs_upmcrs = upmcrs;
 ufs_hs_res = res;
 ufs_hs_stage = ufs_hs_ok ? 7u : 8u;
 return (int)ufs_hs_ok;
}
static int ufs_link_startup(void)
{
 uint32_t hcs = ufs_rd(0x30);
 uint32_t loops = 200000;
 uint32_t is;
 ufs_link_ok = 0;
 ufs_link_is = 0xffffffffu;
 if ((hcs & (1u << 3)) == 0) {
  return 0;
 }
 ufs_wr(0x20, (1u << 5) | (1u << 10) |
               0x1u );
 ufs_wr(0x94, 0);
 ufs_wr(0x98, 0);
 ufs_wr(0x9c, 0);
 __asm__ volatile("dsb sy" ::: "memory");
 ufs_wr(0x90, 0x16u);
 __asm__ volatile("dsb sy" ::: "memory");
 for (;;) {
  is = ufs_rd(0x20);
  if ((is & ((1u << 5) | (1u << 10) |
             (1u << 8))) != 0) {
   break;
  }
  if (--loops == 0) {
   break;
  }
 }
 ufs_link_is = is;
 ufs_link_result = ufs_rd(0x98) & 0xffu;
 ufs_link_ok = (is & ((1u << 5) | (1u << 10) |
                      (1u << 8))) != 0 && ufs_link_result == 0;
 ufs_wr(0x20, (1u << 5) | (1u << 10) |
               (1u << 8));
 __asm__ volatile("dsb sy" ::: "memory");
 return ufs_link_ok;
}
static int ufs_uic_hibernate_exit(void)
{
 uint32_t hcs = ufs_rd(0x30);
 uint32_t deadline_loops = 20000;
 uint32_t is;
 if ((hcs & (1u << 3)) == 0) {
  ufs_uic_status = 0xffffffffu;
  return 0;
 }
 ufs_uic_step = 1;
 ufs_wr(0x20, (1u << 5) | (1u << 10));
 ufs_wr(0x94, 0);
 ufs_wr(0x98, 0);
 ufs_wr(0x9c, 0);
 __asm__ volatile("dsb sy" ::: "memory");
 ufs_uic_step = 2;
 ufs_wr(0x90, 0x18u);
 __asm__ volatile("dsb sy" ::: "memory");
 ufs_uic_step = 3;
 for (;;) {
  is = ufs_rd(0x20);
  if ((is & ((1u << 5) | (1u << 10))) != 0) {
   break;
  }
  if (--deadline_loops == 0) {
   break;
  }
 }
 ufs_uic_step = 4;
 ufs_uic_status = is & ((1u << 5) | (1u << 10));
 ufs_wr(0x20, (1u << 5) | (1u << 10));
 ufs_uic_hcs = hcs;
 return 1;
}
static void ufs_mtk_host_reset(void)
{
 ufs_pdn_before = ufs_rd(0x112bc000ULL + 0x44);
 ufs_ao_before = ufs_rd(0x112b8000ULL + 0x44);
 ufs_wr(0x112bc000ULL + 0x44u, 0x2u);
 ufs_wr(0x112bc000ULL + 0x44u, 0x1u);
 ufs_wr(0x112b8000ULL + 0x44u, 0x1u);
 __asm__ volatile("dsb sy" ::: "memory");
 for (volatile uint32_t n = 0; n < 20000u; n++) { }
 ufs_wr(0x112b8000ULL + 0x48u, 0x1u);
 ufs_wr(0x112bc000ULL + 0x48u, 0x1u);
 ufs_wr(0x112bc000ULL + 0x48u, 0x2u);
 __asm__ volatile("dsb sy" ::: "memory");
 for (volatile uint32_t n = 0; n < 20000u * 10u; n++) { }
 ufs_pdn_after = ufs_rd(0x112bc000ULL + 0x44);
 ufs_ao_after = ufs_rd(0x112b8000ULL + 0x44);
}
static void ufs_mtk_host_enable(void)
{
 uint32_t polls;
 ufs_hce_after_reset = ufs_rd(0x34);
 ufs_wr(0x34, 1u);
 __asm__ volatile("dsb sy" ::: "memory");
 for (polls = 0; polls < 200000u; polls++) {
  if ((ufs_rd(0x34) & 1u) != 0) {
   break;
  }
 }
 ufs_hce_enabled = ufs_rd(0x34);
 for (polls = 0; polls < 200000u; polls++) {
  if ((ufs_rd(0x30) & (1u << 1)) != 0) {
   break;
  }
 }
 ufs_hcs_after_enable = ufs_rd(0x30);
 ufs_reset_ok = ufs_uic_cmd(0x14u, 0);
 ufs_reset_hcs = ufs_rd(0x30);
 ufs_enable_ok = ufs_uic_cmd(0x12u, 0);
 ufs_enable_hcs = ufs_rd(0x30);
 ufs_link_startup();
 ufs_link_uecpa_after = ufs_rd(0x38);
 ufs_link_hcs_after = ufs_rd(0x30);
}
static void __attribute__((unused)) ufs_device_wake(void);
static int ufs_init(void)
{
 ufs_last.what = 0;
 ufs_last.status = 0;
 uint32_t cap, hcs, nutrs;
 if (ufs_ready) {
  return 1;
 }
 if (ufs_failed) {
  return 0;
 }
 cap = ufs_rd(0x00);
 hcs = ufs_rd(0x30);
 nutrs = (((cap) & 0x1fu) + 1u);
 ufs_last.status = hcs;
 if ((hcs & (1u << 0)) == 0 || (hcs & (1u << 1)) == 0) {
  ufs_last.what = "controller not ready";
  ufs_failed = 1;
  return 0;
 }
 if (nutrs < 1) {
  ufs_failed = 1;
  return 0;
 }
 ufs_nutrs = nutrs;
 ufs_link_uecpa = ufs_rd(0x38);
 ufs_link_hcs_before = ufs_rd(0x30);
 ufs_link_uecpa_after = ufs_rd(0x38);
 ufs_link_hcs_after = ufs_rd(0x30);
 ufs_hcs_where[0] = "entries"; ufs_hcs_trace[0] = ufs_rd(0x30);
 ufs_lk_list_lo = ufs_rd(0x50);
 ufs_lk_list_hi = ufs_rd(0x54);
 ufs_lk_cold_lo = ufs_lk_list_lo;
 ufs_lk_cold_hi = ufs_lk_list_hi;
 if (!ufs_lk_saved && ufs_lk_list_lo) {
  const volatile uint32_t *lk = (const volatile uint32_t *)(uintptr_t)ufs_lk_list_lo;
  for (uint32_t i = 0; i < 8; i++) {
   ufs_lk_utrd[i] = lk[i];
  }
  ufs_lk_saved = 1;
 }
 for (uint32_t i = 0; i < 32; i++) {
  ufs_slots[i].utrd = (volatile uint32_t *)((uint8_t *)((volatile uint32_t *)(uintptr_t)0x60100000ULL) + i * 32);
  ufs_slots[i].ucd = (volatile uint32_t *)(((uint8_t *)(uintptr_t)(0x60100000ULL + 0x1000u)) + i * (((1024 + 16 * ((8u << 20) / (256u << 10) + 1)) + 127u) & ~127u));
  ufs_slots[i].ucd_pa = (uint64_t)(uintptr_t)(((uint8_t *)(uintptr_t)(0x60100000ULL + 0x1000u)) + i * (((1024 + 16 * ((8u << 20) / (256u << 10) + 1)) + 127u) & ~127u));
 }
 ufs_hce_after = ufs_rd(0x34);
ufs_hcs_where[1] = "pre-branch"; ufs_hcs_trace[1] = ufs_rd(0x30);
 {
  struct ufs_smc_ret mr = ufs_mtk_mtcmos_ctrl(0);
  ufs_sip_mtcmos_off = (uint32_t)(mr.a0 & 0xffffffffu);
  mr = ufs_mtk_mtcmos_ctrl(1);
  ufs_sip_mtcmos_on = (uint32_t)(mr.a0 & 0xffffffffu);
 }
 __asm__ volatile("dsb sy" ::: "memory");
 ufs_mtk_xoufs_before = ufs_rd(0x140);
 ufs_wr(0x140, ufs_mtk_xoufs_before | (1u << 7));
 __asm__ volatile("dsb sy" ::: "memory");
 ufs_mtk_xoufs_after = ufs_rd(0x140);
 {
  struct ufs_smc_ret rr = ufs_mtk_ref_clk_notify(1, 0ULL);
  ufs_sip_ref_pre = (uint32_t)(rr.a0 & 0xffffffffu);
 }
 ufs_mtk_refclk_before = ufs_rd(0x144);
 ufs_wr(0x144, (1u << 0));
 for (uint32_t n = 0; n < 20000u; n++) {
  uint32_t v = ufs_rd(0x144);
  if ((v & (1u << 1)) != 0) { ufs_mtk_refclk_ack = 1; break; }
 }
 ufs_mtk_refclk_after = ufs_rd(0x144);
 {
  struct ufs_smc_ret rr = ufs_mtk_ref_clk_notify(1, 1ULL);
  ufs_sip_ref_post = (uint32_t)(rr.a0 & 0xffffffffu);
 }
 ufs_device_wake();
 ufs_hce_init = ufs_rd(0x34);
 if ((ufs_hce_init & 1u) == 0) {
  ufs_wr(0x34, ufs_hce_init | 1u);
  __asm__ volatile("dsb sy" ::: "memory");
  for (volatile uint32_t d = 0; d < 200000u; d++) { __asm__ volatile("nop"); }
  for (uint32_t n = 0; n < (1u << 16); n++) {
   if ((ufs_rd(0x34) & 1u) != 0) { break; }
  }
 }
 ufs_hce_after_set = ufs_rd(0x34);
 ufs_link_already_up = ((ufs_hce_init & 1u) != 0) && !ufs_device_woke;
 ufs_hcs_where[6] = "pre-link"; ufs_hcs_trace[6] = ufs_rd(0x30);
 if (!ufs_link_already_up) {
 ufs_link_ran = 1;
 ufs_link_uecpa = ufs_rd(0x38);
 ufs_link_hcs_before = ufs_rd(0x30);
 ufs_link_uecpa = ufs_rd(0x38);
 ufs_reset_ok = ufs_uic_cmd(0x14u, 0);
 ufs_reset_hcs = ufs_rd(0x30);
 ufs_enable_ok = ufs_uic_cmd(0x12u, 0);
 ufs_enable_hcs = ufs_rd(0x30);
 ufs_link_startup();
 }
 ufs_hs_negotiate();
 ufs_link_uecpa_after = ufs_rd(0x38);
 ufs_link_hcs_after = ufs_rd(0x30);
 ufs_hcs_where[7] = "post-link"; ufs_hcs_trace[7] = ufs_rd(0x30);
 ufs_link_skipped = ufs_link_ran ? 0u : 1u;
 ufs_lk_list_lo = ufs_rd(0x50);
 ufs_lk_list_hi = ufs_rd(0x54);
 ufs_hcs_where[2] = "after clr"; ufs_hcs_trace[2] = ufs_rd(0x30);
 for (uint32_t n = 0; n < 10000u; n++) {
  if ((ufs_rd(0x58) & 1u) == 0) { break; }
 }
 ufs_list_lo = ufs_lk_list_lo;
 ufs_list_hi = ufs_lk_list_hi;
 ufs_list_is_lk = 1;
 ufs_moved_lo = ufs_list_lo;
 ufs_moved_hi = ufs_list_hi;
 for (uint32_t i = 0; i < 32; i++) {
  uint64_t base = ((uint64_t)ufs_list_hi << 32) | ufs_list_lo;
  ufs_slots[i].utrd = (volatile uint32_t *)(uintptr_t)(base + i * 32);
 }
 ufs_hcs_where[3] = "after is"; ufs_hcs_trace[3] = ufs_rd(0x30);
 ufs_is_before = ufs_rd(0x20);
 ufs_door_before = ufs_rd(0x58);
 ufs_door_after_clear = ufs_rd(0x58);
 ufs_run_after_stop = ufs_rd(0x60);
 ufs_hcs_where[4] = "after rsr"; ufs_hcs_trace[4] = ufs_rd(0x30);
 ufs_hcs_where[5] = "branch end"; ufs_hcs_trace[5] = ufs_rd(0x30);
 ufs_post_link_rsr = ufs_rd(0x60);
 ufs_post_link_dbr = ufs_rd(0x58);
 ufs_hcs_where[8] = "post-rsr"; ufs_hcs_trace[8] = ufs_rd(0x30);
 ufs_zero((void *)((volatile uint32_t *)(uintptr_t)0x60100000ULL), 32 * 32);
 __asm__ volatile("dsb sy" ::: "memory");
 ufs_is_before = ufs_rd(0x20);
 ufs_door_before = ufs_rd(0x58);
 ufs_door_after_clear = ufs_rd(0x58);
 ufs_list_lo = ufs_moved_lo;
 ufs_list_hi = ufs_moved_hi;
 const int ufs_list_inherited = ufs_list_is_lk;
 ufs_list_is_lk = 0;
 for (uint32_t i = 0; i < 32; i++) {
  uint64_t base = ((uint64_t)ufs_list_hi << 32) | ufs_list_lo;
  ufs_slots[i].utrd = (volatile uint32_t *)(uintptr_t)(base + i * 32);
 }
 ufs_tail_rsr_before = ufs_rd(0x60);
 ufs_tail_wrote_rsr = 0;
 if (!ufs_list_inherited || (ufs_tail_rsr_before & 1u) == 0) {
  ufs_wr(0x60, 1);
  ufs_tail_wrote_rsr = 1;
 }
 if ((ufs_rd(0x60) & 1u) == 0) {
  ufs_failed = 1;
  return ufs_fail("list did not start", ufs_rd(0x60));
 }
 ufs_hcs_where[9] = "tail-rsr"; ufs_hcs_trace[9] = ufs_rd(0x30);
 ufs_hiber_exit_ok = ufs_uic_hibernate_exit();
 ufs_ready = 1;
 return 1;
}
static int __attribute__((unused)) ufs_tur(uint8_t lun)
{
 uint8_t upiu[32];
 uint32_t ocs;
 if (!ufs_init()) {
  return 0;
 }
 ufs_zero(upiu, sizeof(upiu));
 upiu[0] = 0x01;
 upiu[2] = lun;
 upiu[3] = 0u;
 upiu[16] = 0x00;
 if (!ufs_place(&ufs_slots[0], upiu, (1u << 28) | (0u << 25), 0)) {
  return 0;
 }
 if (!ufs_run(&ocs)) {
  ufs_failed = 1;
  return 0;
 }
 for (uint32_t b = 0; b < 16u; b++) {
  ufs_tur_resp[b] = *(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + 512 + b);
 }
 ufs_tur_resp_seen = 1;
 if (ocs != 0x0u) {
  ufs_failed = 1;
  return ufs_fail("TEST UNIT READY", ocs);
 }
 if (!ufs_reply_ok(&ufs_slots[0])) {
  ufs_failed = 1;
  return ufs_fail("TEST UNIT READY status", ufs_last_status7);
 }
 return 1;
}
static void __attribute__((unused)) ufs_device_wake(void)
{
 ufs_device_woke = 1;
 struct ufs_smc_ret r = ufs_mtk_va09_pwr_ctrl(1);
 ufs_sip_va09_ok = (uint32_t)r.a0;
 for (volatile uint32_t n = 0; n < 20000u; n++) { }
 r = ufs_mtk_device_pwr_ctrl(1, 0);
 ufs_sip_devpwr_ok = (uint32_t)r.a0;
 r = ufs_mtk_device_reset_ctrl(0);
 ufs_sip_rst_lo = (uint32_t)r.a0;
 for (volatile uint32_t n = 0; n < 2000u; n++) { }
 r = ufs_mtk_device_reset_ctrl(1);
 ufs_sip_rst_hi = (uint32_t)r.a0;
 for (volatile uint32_t n = 0; n < 200000u; n++) { }
}
static int __attribute__((unused)) ufs_nop(void)
{
 uint8_t upiu[32];
 uint32_t ocs;
 if (!ufs_init()) {
  return 0;
 }
 ufs_zero(upiu, sizeof(upiu));
 upiu[0] = 0x00;
 upiu[3] = 0u;
 if (!ufs_place(&ufs_slots[0], upiu, (1u << 28) | (0u << 25) | (1u << 24), 0)) {
  return 0;
 }
 if (!ufs_run(&ocs)) {
  ufs_nop_ocs = 0xffffffffu;
  return 0;
 }
 ufs_nop_ocs = ocs;
 ufs_nop_resp0 = *(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + 512);
 ufs_nop_resp6 = *(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + 512 + 6);
 if (ocs != 0x0u) {
  return 0;
 }
 return 1;
}
static int __attribute__((unused)) ufs_query_flag(uint8_t opcode, uint8_t idn, uint32_t *value)
{
 uint8_t upiu[32];
 uint32_t ocs;
 volatile const uint8_t *r;
 if (!ufs_init()) {
  return 0;
 }
 ufs_zero(upiu, sizeof(upiu));
 upiu[0] = 0x16;
 upiu[2] = 0u;
 upiu[3] = 0u;
 upiu[5] = (opcode == 0x05) ? 0x01
        : 0x81;
 upiu[12] = opcode;
 upiu[13] = idn;
 upiu[14] = 0;
 upiu[15] = 0;
 if (!ufs_place(&ufs_slots[0], upiu, (1u << 28) | (0u << 25) | (1u << 24), 0)) {
  return 0;
 }
 if (!ufs_run(&ocs)) {
  ufs_query_ocs = 0xffffffffu;
  return 0;
 }
 ufs_query_ocs = ocs;
 r = (volatile const uint8_t *)ufs_slots[0].ucd + 512;
 ufs_query_resp = r[6];
 ufs_query_rsp0 = r[0];
 if (value) {
  *value = ((uint32_t)r[20] << 24) | ((uint32_t)r[21] << 16) |
    ((uint32_t)r[22] << 8) | (uint32_t)r[23];
 }
 if (ocs != 0x0u) {
  return 0;
 }
 return r[6] == 0;
}
static int __attribute__((unused)) ufs_fdevice_init(void)
{
 uint32_t tries;
 if (!ufs_query_flag(0x06, 0x01, 0)) {
  ufs_fdevinit_stage = 1;
  return 0;
 }
 ufs_fdevinit_stage = 2;
 for (tries = 0; tries < 64; tries++) {
  uint32_t flag = 0;
  if (!ufs_query_flag(0x05, 0x01, &flag)) {
   ufs_fdevinit_stage = 3;
   return 0;
  }
  ufs_fdevinit_flag = flag;
  if (flag == 0) {
   ufs_fdevinit_stage = 4;
   ufs_fdevinit_tries = tries + 1u;
   return 1;
  }
  for (volatile uint32_t n = 0; n < 20000u; n++) { }
 }
 ufs_fdevinit_tries = tries;
 ufs_fdevinit_stage = 5;
 return 0;
}
static uint32_t ufs_lun_status[8];
static uint32_t ufs_lun_ocs[8];
static const char *ufs_lun_why[8];
static uint32_t ufs_last_utrd[8];
static uint8_t ufs_last_resp[32];
static uint32_t ufs_place_probe[4];
static uint32_t ufs_place_probe_addr;
static uint8_t ufs_cap_raw[16];
static uint8_t ufs_cap_resp[16];
static uint8_t ufs_cap_cmd[16];
static uint8_t ufs_cap_at128[16];
static uint8_t ufs_cap_at1024[16];
static uint32_t ufs_cap_utrd0, ufs_cap_utrd7;
static uint32_t ufs_cap_stage;
static uint32_t ufs_cap_last, ufs_cap_lbs;
static int ufs_lun_found;
static uint32_t ufs_pw_ocs0[8];
static uint32_t ufs_pw_ocs2[8];
static uint32_t ufs_pw_resp[8];
static uint32_t ufs_pw_at[8];
static int ufs_pw_best = -1;
static int __attribute__((unused)) ufs_probe_addr_word(void)
{
 ufs_pw_best = -1;
 for (int w = 1; w < 8; w++) {
  uint8_t upiu[32];
  uint32_t ocs;
  ufs_failed = 0;
  if (!ufs_init()) {
   return 0;
  }
  ufs_zero((void *)ufs_slots[0].utrd, 32);
  ufs_zero(upiu, sizeof(upiu));
  upiu[0] = 0x01;
  upiu[2] = ufs_lun;
  upiu[3] = 0u;
  upiu[16] = 0x00;
  ufs_put((void *)ufs_slots[0].ucd + 0, upiu, 32);
  ufs_zero((void *)ufs_slots[0].ucd + 512, 64);
  ufs_slots[0].utrd[0] = (1u << 28) | (0u << 25);
  ufs_slots[0].utrd[w] = (uint32_t)ufs_slots[0].ucd_pa;
  if (w < 7) {
   ufs_slots[0].utrd[w + 1] = (uint32_t)(ufs_slots[0].ucd_pa >> 32);
  }
  ufs_slots[0].utrd[5] = ((512 / 4) << 16) | (512 / 4);
  ufs_slots[0].utrd[6] = (0u << 16) | (1024 / 4);
  __asm__ volatile("dsb sy" ::: "memory");
  ufs_pw_ocs0[w] = 0;
  ufs_pw_ocs2[w] = 0;
  ufs_pw_resp[w] = 0;
  ufs_pw_at[w] = 0xffffu;
  if (!ufs_run(&ocs)) {
   ufs_pw_ocs0[w] = 0xffffffffu;
   continue;
  }
  ufs_pw_ocs0[w] = ufs_slots[0].utrd[0] >> 24;
  ufs_pw_ocs2[w] = ufs_slots[0].utrd[2];
  ufs_pw_resp[w] =
   *(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + 512);
  {
   volatile const uint8_t *u = (volatile const uint8_t *)ufs_slots[0].ucd;
   for (uint32_t at = 0; at + 1 < 512u; at += 4) {
    if (u[at] == 0x21) { ufs_pw_at[w] = at; break; }
   }
  }
  if (ufs_pw_resp[w] == 0x21 && ufs_pw_best < 0) {
   ufs_pw_best = w;
  }
 }
 return ufs_pw_best > 0;
}
static int __attribute__((unused)) ufs_pick_layout(void)
{
 for (int which = 0; which < 4; which++) {
  uint8_t upiu[32];
  uint32_t ocs;
  ufs_layout = which;
  ufs_failed = 0;
  if (!ufs_init()) {
   return 0;
  }
  ufs_zero(upiu, sizeof(upiu));
  upiu[0] = 0x01;
  upiu[2] = ufs_lun;
  upiu[3] = 0u;
  upiu[16] = 0x00;
  if (!ufs_place(&ufs_slots[0], upiu, (1u << 28) | (0u << 25), 0)) {
   return 0;
  }
  if (!ufs_run(&ocs)) {
   ufs_layout_tried[which] = 0xffffffffu;
   continue;
  }
  ufs_layout_tried[which] =
   *(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + 512);
  if (ufs_layout_tried[which] == 0x21) {
   ufs_layout = which;
   return 1;
  }
 }
 return 0;
}
static uint32_t ufs_ab_ocs[4];
static uint32_t ufs_ab_dw2[4];
static uint32_t ufs_err_phy, ufs_err_dl, ufs_err_net, ufs_err_tr, ufs_err_dme;
static uint32_t ufs_fail_is, ufs_fail_hcs, ufs_fail_cnr, ufs_fail_resp0;
static void ufs_capture_failure(void)
{
 ufs_err_phy = ufs_rd(0x38);
 ufs_err_dl = ufs_rd(0x3c);
 ufs_err_net = ufs_rd(0x40);
 ufs_err_tr = ufs_rd(0x44);
 ufs_err_dme = ufs_rd(0x48);
 ufs_fail_is = ufs_rd(0x20);
 ufs_fail_hcs = ufs_rd(0x30);
 ufs_fail_cnr = ufs_rd(0x64);
 ufs_fail_resp0 = *(volatile const uint32_t *)((uint8_t *)ufs_slots[0].ucd + 512);
}
static void ufs_probe_variants(void)
{
 for (int v = 0; v < 4; v++) {
  uint8_t upiu[32];
  uint32_t ocs = 0xffffffffu;
  uint32_t ct = (v < 2) ? 0x00000100u : (1u << 28);
  uint32_t rl = (v == 1 || v == 2) ? 128u : 32u;
  ufs_failed = 0;
  ufs_ab_ocs[v] = 0xffffffffu;
  ufs_ab_dw2[v] = 0xffffffffu;
  if (!ufs_init()) {
   continue;
  }
  ufs_zero(upiu, sizeof(upiu));
  upiu[0] = 0x01;
  upiu[2] = ufs_lun;
  upiu[3] = 0u;
  upiu[16] = 0x00;
  if (!ufs_place(&ufs_slots[0], upiu, ct | (0u << 25), 0)) {
   continue;
  }
  ufs_slots[0].utrd[6] = (512 / 4) << 16 | rl;
  ufs_cache_sync(ufs_slots[0].utrd, 32);
  if (!ufs_run(&ocs)) {
   ufs_ab_dw2[v] = ufs_slots[0].utrd[2];
   if (v == 0) { ufs_capture_failure(); }
   continue;
  }
  ufs_ab_ocs[v] = ocs;
  ufs_ab_dw2[v] = ufs_slots[0].utrd[2];
  if (v == 0) { ufs_capture_failure(); }
 }
}
static int __attribute__((unused)) ufs_pick_lun(void)
{
 if (ufs_lun_found) {
  return 1;
 }
 for (uint32_t l = 0; l < 8; l++) {
  ufs_lun_status[l] = 0xffu;
  for (uint32_t t = 0; t < 4; t++) {
   uint8_t upiu[32];
   uint32_t ocs;
   do { if (ufs_trace) { ufs_trace("lun "); } } while (0);
   do { if (ufs_trace_hex_fn) { ufs_trace_hex_fn(l); } } while (0);
   do { if (ufs_trace) { ufs_trace("\n"); } } while (0);
   ufs_failed = 0;
   if (!ufs_init()) {
    return 0;
   }
   ufs_zero(upiu, sizeof(upiu));
   upiu[0] = 0x01;
   upiu[2] = (uint8_t)l;
   upiu[3] = 0u;
   upiu[16] = 0x00;
   if (!ufs_place(&ufs_slots[0], upiu, (1u << 28) | (0u << 25), 0)) {
    return 0;
   }
   ufs_place_probe[0] = ufs_slots[0].utrd[0];
   ufs_place_probe[1] = ufs_slots[0].utrd[4];
   ufs_place_probe[2] = ufs_slots[0].utrd[7];
   ufs_place_probe[3] = *(volatile uint32_t *)((uint8_t *)ufs_slots[0].ucd + 0);
   ufs_place_probe_addr = ufs_rd(0x50);
   {
    int ran = ufs_run(&ocs);
    for (uint32_t i = 0; i < 8; i++) { ufs_last_utrd[i] = ufs_slots[0].utrd[i]; }
    for (uint32_t i = 0; i < 16; i++) {
     ufs_last_resp[i] = *(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + 512 + i);
    }
    if (!ran) {
     ufs_lun_status[l] = 0xfeu;
     ufs_lun_ocs[l] = 0u;
     ufs_lun_why[l] = ufs_last.what;
     continue;
    }
   }
   if (0) {
    ufs_lun_ocs[l] = 0u;
    ufs_lun_why[l] = ufs_last.what;
    continue;
   }
   if (ocs != 0x0u) {
    ufs_lun_status[l] = 0xfdu;
    ufs_lun_ocs[l] = ocs;
    ufs_lun_why[l] = "ocs";
    continue;
   }
   ufs_lun_ocs[l] = 0u;
   if (ufs_reply_ok(&ufs_slots[0])) {
    ufs_lun_why[l] = "ok";
    ufs_lun_status[l] = 0u;
   } else {
    ufs_lun_why[l] = "scsi";
    ufs_lun_status[l] = (ufs_last_resp6 << 8) | ufs_last_status7;
   }
   break;
  }
  if (ufs_lun_status[l] == 0) {
   ufs_lun = (uint8_t)l;
   ufs_lun_found = 1;
   return 1;
  }
 }
 return 0;
}
static int ufs_cmd_once(uint8_t lun, uint32_t lba, uint32_t blocks, uint32_t flags, uint32_t dw0)
{
 uint8_t upiu[32];
 uint32_t ocs, bytes = blocks * ufs_lbs;
 if (!ufs_init()) {
  return 0;
 }
 ufs_zero(upiu, sizeof(upiu));
 upiu[0] = 0x01;
 upiu[1] = (uint8_t)flags;
 upiu[2] = lun;
 upiu[3] = 0u;
 upiu[12] = (uint8_t)(bytes >> 24);
 upiu[13] = (uint8_t)(bytes >> 16);
 upiu[14] = (uint8_t)(bytes >> 8);
 upiu[15] = (uint8_t)bytes;
 upiu[16] = (uint8_t)((dw0 & (1u << 25)) ? 0x2a : 0x28);
 upiu[18] = (uint8_t)(lba >> 24);
 upiu[19] = (uint8_t)(lba >> 16);
 upiu[20] = (uint8_t)(lba >> 8);
 upiu[21] = (uint8_t)lba;
 upiu[22] = 0;
 upiu[23] = (uint8_t)(blocks >> 8);
 upiu[24] = (uint8_t)blocks;
 upiu[25] = 0;
 if (!ufs_place(&ufs_slots[0], upiu, dw0, bytes)) {
  return 0;
 }
 if (!ufs_run(&ocs)) {
  ufs_failed = 1;
  return 0;
 }
 if (ocs != 0x0u) {
  ufs_failed = 1;
  return ufs_fail((dw0 & (1u << 25)) ? "WRITE(10)" : "READ(10)", ocs);
 }
 if (!ufs_reply_ok(&ufs_slots[0])) {
  ufs_failed = 1;
  return ufs_fail((dw0 & (1u << 25)) ? "WRITE(10) status" : "READ(10) status", ufs_last_status7);
 }
 return 1;
}
static int ufs_capacity(uint8_t lun, uint32_t *lbs_out)
{
 uint8_t upiu[32];
 uint32_t ocs, last, lbs;
 ufs_cap_stage = 1;
 if (!ufs_init()) {
  ufs_cap_stage = 2;
  return 0;
 }
 ufs_cap_stage = 3;
 ufs_zero(upiu, sizeof(upiu));
 upiu[0] = 0x01;
 upiu[1] = 0x40;
 upiu[2] = lun;
 upiu[3] = 0u;
 upiu[12] = 0;
 upiu[13] = 0;
 upiu[14] = 0;
 upiu[15] = 8;
 upiu[16] = 0x25;
 ufs_cap_stage = 4;
 if (!ufs_place(&ufs_slots[0], upiu, (1u << 28) | (2u << 25), 8)) {
  ufs_cap_stage = 5;
  return 0;
 }
 ufs_cap_stage = 6;
 if (!ufs_run(&ocs)) {
  ufs_cap_stage = 7;
  ufs_failed = 1;
  return 0;
 }
 ufs_cap_stage = 8;
 if (ocs != 0x0u) {
  ufs_cap_stage = 9;
  ufs_failed = 1;
  return ufs_fail("READ CAPACITY(10)", ocs);
 }
 ufs_cap_stage = 10;
 if (ufs_response_status(&ufs_slots[0]) != 0) {
  ufs_failed = 1;
  return ufs_fail("READ CAPACITY(10) status", ufs_response_status(&ufs_slots[0]));
 }
 {
  volatile const uint8_t *d = (volatile const uint8_t *)((uint8_t *)(uintptr_t)(0x60100000ULL + (0x1000u + 32 * (((1024 + 16 * ((8u << 20) / (256u << 10) + 1)) + 127u) & ~127u))));
  last = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
  lbs = ((uint32_t)d[4] << 24) | ((uint32_t)d[5] << 16) | ((uint32_t)d[6] << 8) | d[7];
 }
 {
  volatile const uint8_t *d = (volatile const uint8_t *)((uint8_t *)(uintptr_t)(0x60100000ULL + (0x1000u + 32 * (((1024 + 16 * ((8u << 20) / (256u << 10) + 1)) + 127u) & ~127u))));
  volatile const uint8_t *r = (volatile const uint8_t *)ufs_slots[0].ucd + 512;
  for (uint32_t i = 0; i < 16; i++) { ufs_cap_raw[i] = d[i]; }
  for (uint32_t i = 0; i < 16; i++) { ufs_cap_resp[i] = r[i]; }
  {
   volatile const uint8_t *c = (volatile const uint8_t *)ufs_slots[0].ucd;
   for (uint32_t i = 0; i < 16; i++) { ufs_cap_cmd[i] = c[i]; }
   for (uint32_t i = 0; i < 16; i++) { ufs_cap_at128[i] = c[128 + i]; }
   for (uint32_t i = 0; i < 16; i++) { ufs_cap_at1024[i] = c[1024 + i]; }
  }
  ufs_cap_utrd0 = ufs_slots[0].utrd[0];
  ufs_cap_utrd7 = ufs_slots[0].utrd[7];
  ufs_cap_last = last;
  ufs_cap_lbs = lbs;
 }
 if (lbs != 512u && lbs != 4096u) {
  return ufs_fail("logical block size is neither 512 nor 4096", lbs);
 }
 ufs_cap_stage |= 0x1000u;
 dbg_puts("ufs: unit "); dbg_hex(lun, 2); dbg_puts(": "); dbg_dec((uint64_t)last + 1u);
 dbg_puts(" blocks of "); dbg_dec(lbs); dbg_puts(" bytes\n");
 *lbs_out = lbs;
 return 1;
}
static int ufs_io_ready(void)
{
 static int state;
 if (state == 1) {
  return 1;
 }
 if (state == 2) {
  return 0;
 }
 if (!ufs_init()) {
  state = 2;
  ufs_io_stage = 1;
  return 0;
 }
 ufs_io_stage = 2;
 ufs_io_fdev = (uint32_t)ufs_fdevice_init();
 if (!ufs_io_fdev) {
  ufs_io_stage = 3;
 }
 ufs_pick_lun();
 ufs_io_stage = 4;
 state = 1;
 return 1;
}
static int __attribute__((unused)) ufs_read(uint64_t lba, uint32_t blocks, void *dst)
{
 uint8_t *d = (uint8_t *)dst;
 uint64_t byte_at, bytes;
 uint32_t lbs, per;
 if (ufs_failed) {
  return 0;
 }
 if (!ufs_io_ready()) {
  return 0;
 }
 ufs_lbs = 4096u;
 ufs_lbs_known = 1;
 lbs = ufs_lbs;
 per = (8u << 20) / lbs;
 byte_at = lba * 512ull;
 bytes = (uint64_t)blocks * 512ull;
 while (bytes) {
  uint64_t blk = byte_at / lbs;
  uint32_t off = (uint32_t)(byte_at % lbs);
  uint32_t want = bytes < (8u << 20) ? (uint32_t)bytes : (8u << 20);
  uint32_t nblk = (off + want + lbs - 1u) / lbs;
  uint32_t avail, take;
  if (off == 0 && (((uint64_t)(uintptr_t)d & 63ull) == 0)) {
   if (nblk > per) {
    nblk = per;
   }
   if (blk > 0xffffffffull) {
    ufs_failed = 1;
    ufs_prd_dst = 0;
    return ufs_fail("lba out of READ(10) range", (uint32_t)blk);
   }
   ufs_prd_dst = (uint64_t)(uintptr_t)d;
   if (!ufs_cmd_once(ufs_lun, (uint32_t)blk, nblk, 0x40, (1u << 28) | (2u << 25))) {
    ufs_prd_dst = 0;
    return 0;
   }
   ufs_prd_dst = 0;
   avail = nblk * lbs;
   take = want < avail ? want : avail;
   d += take;
   bytes -= take;
   byte_at += take;
   continue;
  }
  ufs_prd_dst = 0;
  if (nblk > per) {
   nblk = per;
  }
  if (blk > 0xffffffffull) {
   ufs_failed = 1;
   return ufs_fail("lba out of READ(10) range", (uint32_t)blk);
  }
  if (!ufs_cmd_once(ufs_lun, (uint32_t)blk, nblk, 0x40, (1u << 28) | (2u << 25))) {
   return 0;
  }
  avail = nblk * lbs - off;
  take = want < avail ? want : avail;
  ufs_put(d, (const void *)(((uint8_t *)(uintptr_t)(0x60100000ULL + (0x1000u + 32 * (((1024 + 16 * ((8u << 20) / (256u << 10) + 1)) + 127u) & ~127u)))) + off), take);
  d += take;
  bytes -= take;
  byte_at += take;
 }
 return 1;
}
static int __attribute__((unused)) ufs_write(uint64_t lba, uint32_t blocks, const void *src)
{
 const uint8_t *s8 = (const uint8_t *)src;
 uint64_t byte_at, bytes;
 uint32_t lbs, per;
 if (ufs_failed) {
  return 0;
 }
 if (!ufs_io_ready()) {
  return 0;
 }
 ufs_lbs = 4096u;
 ufs_lbs_known = 1;
 lbs = ufs_lbs;
 per = (8u << 20) / lbs;
 byte_at = lba * 512ull;
 bytes = (uint64_t)blocks * 512ull;
 while (bytes) {
  uint64_t blk = byte_at / lbs;
  uint32_t off = (uint32_t)(byte_at % lbs);
  uint32_t want = bytes < (8u << 20) ? (uint32_t)bytes : (8u << 20);
  uint32_t nblk, avail, take;
  if (off == 0 && (((uint64_t)(uintptr_t)s8 & 63ull) == 0)) {
   nblk = (want + lbs - 1u) / lbs;
   if (nblk > per) {
    nblk = per;
   }
   if (blk > 0xffffffffull) {
    ufs_failed = 1;
    ufs_prd_dst = 0;
    return ufs_fail("lba out of WRITE(10) range", (uint32_t)blk);
   }
   ufs_prd_dst = (uint64_t)(uintptr_t)s8;
   if (!ufs_cmd_once(ufs_lun, (uint32_t)blk, nblk, 0x20,
       (1u << 28) | (1u << 25))) {
    ufs_prd_dst = 0;
    return 0;
   }
   ufs_prd_dst = 0;
   avail = nblk * lbs;
   take = want < avail ? want : avail;
   s8 += take;
   bytes -= take;
   byte_at += take;
   continue;
  }
  nblk = (off + want + lbs - 1u) / lbs;
  if (nblk > per) {
   nblk = per;
  }
  ufs_zero(((uint8_t *)(uintptr_t)(0x60100000ULL + (0x1000u + 32 * (((1024 + 16 * ((8u << 20) / (256u << 10) + 1)) + 127u) & ~127u)))), nblk * lbs);
  ufs_put(((uint8_t *)(uintptr_t)(0x60100000ULL + (0x1000u + 32 * (((1024 + 16 * ((8u << 20) / (256u << 10) + 1)) + 127u) & ~127u)))) + off, s8, want);
  if (!ufs_cmd_once(ufs_lun, (uint32_t)blk, nblk, 0x20,
      (1u << 28) | (1u << 25))) {
   return 0;
  }
  avail = nblk * lbs - off;
  take = want < avail ? want : avail;
  s8 += take;
  bytes -= take;
  byte_at += take;
 }
 return 1;
}
static void __attribute__((unused)) ufs_report(void)
{
 uint32_t cap = ufs_rd(0x00);
 ufs_last.what = "not tried";
 ufs_last.status = 0;
 ufs_last.what = "controller";
 ufs_last.status = ufs_rd(0x30);
 ufs_nutrs = (((cap) & 0x1fu) + 1u);
}
static void spi_pins_to_spi2(void)
{
 uint32_t mode = rd32(0x10005000ULL + 0x330);
 for (int pin = 25; pin <= 28; pin++) {
  uint32_t shift = (uint32_t)(pin % 8) * 4;
  mode = (mode & ~(0xfu << shift)) | ((uint32_t)2 << shift);
 }
 wr32(0x10005000ULL + 0x330, mode);
 __asm__ volatile("dsb sy");
}
static void spi_setup(void)
{
 wr32(0x10000000ULL + 0x68, (3u << 24) | (1u << 31));
 __asm__ volatile("dsb sy");
 wr32(0x11012000ULL + 0x18, (1u << 2));
 wr32(0x11012000ULL + 0x18, 0);
 wr32(0x11012000ULL + 0x28, ((2 - 1) << 0) |
     ((2 - 1) << 16));
 wr32(0x11012000ULL + 0x00, ((2 - 1) << 0) |
     ((2 - 1) << 16));
 wr32(0x11012000ULL + 0x24, 0);
}
static inline void pin_set(uint32_t pin, int high)
{
 wr32(0x10005000ULL + (high ? 0x104 : 0x108), 1u << pin);
}
static inline int pin_get(uint32_t pin)
{
 return (rd32(0x10005000ULL + 0x200) >> pin) & 1u;
}
static inline void pin_settle(void)
{
 (void)rd32(0x10005000ULL + 0x200);
 (void)rd32(0x10005000ULL + 0x200);
}
static void spi_pins_to_gpio(void)
{
 uint32_t mode = rd32(0x10005000ULL + 0x330);
 for (int pin = 25; pin <= 28; pin++) {
  mode &= ~(0xfu << ((uint32_t)(pin % 8) * 4));
 }
 wr32(0x10005000ULL + 0x330, mode);
 wr32(0x10005000ULL + 0x004, (1u << 26) | (1u << 27) | (1u << 25));
 wr32(0x10005000ULL + 0x008, 1u << 28);
 pin_set(25, 1);
 pin_set(27, 1);
 __asm__ volatile("dsb sy");
 pin_settle();
}
static void spi_bitbang_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len, uint32_t byte_gap_us)
{
 int idle = 1;
 pin_set(27, idle);
 pin_set(25, 0);
 pin_settle();
 for (uint32_t i = 0; i < len; i++) {
  uint8_t out_byte = tx ? tx[i] : 0xff, in_byte = 0;
  for (int bit = 7; bit >= 0; bit--) {
   pin_set(26, (out_byte >> bit) & 1);
   pin_set(27, !idle);
   pin_settle();
   pin_set(27, idle);
   pin_settle();
   in_byte = (uint8_t)((in_byte << 1) | pin_get(28));
  }
  if (rx) {
   rx[i] = in_byte;
  }
  if (byte_gap_us) {
   dbg_delay_us(byte_gap_us);
  }
 }
 pin_set(25, 1);
 pin_settle();
}
__attribute__((unused)) static void touch_reset(void)
{
 wr32(0x10005000ULL + 0x108, 1u);
 __asm__ volatile("dsb sy");
 dbg_delay_ms(20);
 wr32(0x10005000ULL + 0x104, 1u);
 __asm__ volatile("dsb sy");
 dbg_delay_ms(200);
}
static int spi_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
 uint32_t words = (len + 3) / 4;
 if (len == 0 || len > 32) {
  return 0;
 }
 spi_setup();
 wr32(0x11012000ULL + 0x18, (1u << 8) | (1u << 9) | (1u << 12) | (1u << 13));
 wr32(0x11012000ULL + 0x04, ((len - 1) << 16) |
     (0 << 8) | ((2 - 1) << 0));
 for (uint32_t w = 0; w < words; w++) {
  uint32_t v = 0;
  for (uint32_t b = 0; b < 4; b++) {
   uint32_t i = w * 4 + b;
   v |= (uint32_t)(tx && i < len ? tx[i] : 0xff) << (8 * b);
  }
  wr32(0x11012000ULL + 0x10, v);
 }
 wr32(0x11012000ULL + 0x18, rd32(0x11012000ULL + 0x18) | (1u << 0));
 for (uint32_t spin = 0; ; spin++) {
  if (rd32(0x11012000ULL + 0x1c) & (1u << 0)) {
   break;
  }
  if (spin > 2000000) {
   return 0;
  }
 }
 for (uint32_t w = 0; w < words; w++) {
  uint32_t v = rd32(0x11012000ULL + 0x14);
  for (uint32_t b = 0; b < 4; b++) {
   uint32_t i = w * 4 + b;
   if (rx && i < len) {
    rx[i] = (uint8_t)(v >> (8 * b));
   }
  }
 }
 return 1;
}
static const uint8_t touch_fw[1] = { 0x00 };
static int tcm_spi_stalls;
static int tcm_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
 if (rx) {
  spi_pins_to_gpio();
  spi_bitbang_xfer(tx, rx, len, 20);
  return 1;
 }
 spi_pins_to_spi2();
 spi_setup();
 if (spi_xfer(tx, rx, len)) {
  return 1;
 }
 tcm_spi_stalls++;
 spi_pins_to_gpio();
 spi_bitbang_xfer(tx, rx, len, 0);
 return 1;
}
static int tcm_irq(void)
{
 return pin_get(1) == 0;
}
static int tcm_wait_irq(uint32_t ms)
{
 for (uint32_t i = 0; i < ms; i++) {
  if (tcm_irq()) {
   return 1;
  }
  dbg_delay_ms(1);
 }
 return 0;
}
static uint32_t tcm_stall_chunk, tcm_stall_size;
static int tcm_write(uint8_t cmd, const uint8_t *payload, uint32_t len, uint32_t unit,
    uint32_t *steps)
{
 static uint8_t chunk[32];
 uint32_t space = 32 - 1, remaining = len + 2, sent = 0;
 int first = 1;
 if (unit > 1) {
  space -= space % unit;
 }
 if (space == 0) {
  return 0;
 }
 while (remaining > 0) {
  uint32_t xfer = remaining > space ? space : remaining, n = 0;
  if (first) {
   chunk[n++] = cmd;
   chunk[n++] = (uint8_t)len;
   chunk[n++] = (uint8_t)(len >> 8);
   for (uint32_t i = 2; i < xfer; i++) {
    chunk[n++] = payload[sent++];
   }
   first = 0;
  } else {
   chunk[n++] = 0x01;
   for (uint32_t i = 0; i < xfer; i++) {
    chunk[n++] = payload[sent++];
   }
  }
  if (!tcm_xfer(chunk, ((void*)0), n)) {
   if (steps) {
    *steps = sent;
   }
   return 0;
  }
  remaining -= xfer;
 }
 if (steps) {
  *steps = len;
 }
 return 1;
}
static int tcm_read(uint8_t *code, uint8_t *buf, uint32_t max, uint32_t *got)
{
 static uint8_t tmp[1024];
 uint8_t head[4];
 uint32_t space = 1024 - 2, len, left, at = 0;
 if (!tcm_xfer(((void*)0), head, 4)) {
  return 0;
 }
 if (head[0] != 0xa5) {
  *code = head[0];
  *got = 0;
  return -1;
 }
 *code = head[1];
 len = (uint32_t)head[2] | ((uint32_t)head[3] << 8);
 *got = len;
 left = len + 1;
 if (len == 0) {
  left = 1;
 }
 while (left > 0) {
  uint32_t xfer = left > space ? space : left;
  if (!tcm_xfer(((void*)0), tmp, xfer + 2)) {
   return 0;
  }
  if (tmp[0] != 0xa5 || tmp[1] != 0x03) {
   return -2;
  }
  for (uint32_t i = 0; i < xfer; i++) {
   if (at + i < max) {
    buf[at + i] = tmp[2 + i];
   }
  }
  at += xfer;
  left -= xfer;
 }
 return 1;
}
__attribute__((unused)) static int tcm_read_within(uint8_t *code, uint8_t *buf, uint32_t max, uint32_t *got, uint32_t ms)
{
 for (uint32_t waited = 0; waited <= ms; waited += 120) {
  int r;
  tcm_wait_irq(100);
  r = tcm_read(code, buf, max, got);
  if (r == 1) {
   return 1;
  }
  dbg_delay_ms(20);
 }
 return 0;
}
static int tcm_read_patiently(uint8_t *code, uint8_t *buf, uint32_t max, uint32_t *got)
{
 for (int tries = 0; tries < 6; tries++) {
  int r;
  tcm_wait_irq(100);
  r = tcm_read(code, buf, max, got);
  if (r == 1) {
   return 1;
  }
  dbg_delay_ms(20);
 }
 return 0;
}
static void tcm_load_firmware(uint8_t *message)
{
 uint32_t len = 14 + 0u;
 message[0] = 0x45;
 message[1] = (uint8_t)len;
 message[2] = (uint8_t)(len >> 8);
 for (uint32_t i = 0; i < 14; i++) {
  message[3 + i] = 0;
 }
 message[3] = (uint8_t)(0u >> 16);
 spi_pins_to_gpio();
 spi_bitbang_xfer(message, ((void*)0), 3 + len, 0);
}
static void tcm_run_firmware(void)
{
 dbg_delay_ms(100);
 tcm_write(0x42, 0, 0, 0, 0);
}
static void tcm_reset(void)
{
 pin_set(0, 0);
 __asm__ volatile("dsb sy");
 dbg_delay_ms(20);
 pin_set(0, 1);
 __asm__ volatile("dsb sy");
 dbg_delay_ms(200);
}

struct identity_prop { const char *name; unsigned offset, length; };
static const struct identity_prop identity_root[] = {
 { "target-type", 0, 4 },
 { "manufacturer", 4, 11 },
 { "target-sub-type", 15, 6 },
 { "compatible", 21, 26 },
 { "model", 47, 11 },
 { "device_type", 58, 8 },
};
static const unsigned char identity_root_data[] = {
 0x44, 0x32, 0x32, 0x00, 0x41, 0x70, 0x70, 0x6c, 0x65, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x00, 0x44, 0x32, 0x32, 0x41, 0x50,
 0x00, 0x44, 0x32, 0x32, 0x41, 0x50, 0x00, 0x69, 0x50, 0x68, 0x6f, 0x6e, 0x65, 0x31, 0x30, 0x2c, 0x33, 0x00, 0x41, 0x70,
 0x70, 0x6c, 0x65, 0x41, 0x52, 0x4d, 0x00, 0x69, 0x50, 0x68, 0x6f, 0x6e, 0x65, 0x31, 0x30, 0x2c, 0x33, 0x00, 0x62, 0x6f,
 0x6f, 0x74, 0x72, 0x6f, 0x6d, 0x00,
};
static const struct identity_prop identity_product[] = {
 { "sandman-support", 0, 4 },
 { "display-mirroring", 4, 4 },
 { "car-integration", 8, 4 },
 { "assistant", 12, 4 },
 { "lockdown-certtype", 16, 4 },
 { "ui-pip", 20, 4 },
 { "wifi-chipset", 24, 5 },
 { "product-name", 29, 9 },
 { "hearingaid-audio-equalization", 38, 4 },
 { "RF-exposure-separation-distance", 42, 4 },
 { "mobiledevice-min-ver", 46, 5 },
 { "product-id", 51, 20 },
 { "gps-capable", 71, 4 },
 { "product-description", 75, 9 },
 { "allow-32bit-apps", 84, 4 },
 { "low-power-wallet-mode", 88, 4 },
 { "builtin-mics", 92, 4 },
 { "has-boot-chime", 96, 4 },
 { "supports-burnin-mitigation", 100, 4 },
 { "compatible-device-fallback", 104, 10 },
 { "device-perf-memory-class", 114, 4 },
 { "artwork-scale-factor", 118, 4 },
 { "baseband-chipset", 122, 6 },
 { "hearingaid-low-energy-audio", 128, 4 },
 { "ui-background-quality", 132, 4 },
 { "supports-avatars", 136, 0 },
 { "public-key-accelerator", 136, 4 },
 { "disable-globe-map", 140, 0 },
 { "artwork-device-idiom", 140, 6 },
 { "device-color-policy", 146, 4 },
 { "graphics-featureset-class", 150, 7 },
 { "siri-gesture", 157, 4 },
 { "supports-maps-optical-heading", 161, 0 },
 { "compatible-app-variant", 161, 2 },
 { "artwork-dynamic-displaymode", 163, 2 },
 { "bluetooth-le", 165, 4 },
 { "watch-companion", 169, 4 },
 { "has-applelpm", 173, 4 },
 { "iap2-protocol-supported", 177, 4 },
 { "ephemeral-data-mode", 181, 4 },
 { "activation-protocol-version", 185, 4 },
 { "volume-up-button-location", 189, 20 },
 { "ptp-large-files", 209, 4 },
 { "udid-version", 213, 4 },
 { "offline-dictation", 217, 4 },
 { "location-reminders", 221, 4 },
 { "ui-weather-quality", 225, 4 },
 { "graphics-featureset-fallbacks", 229, 38 },
 { "dictation", 267, 4 },
 { "itunes-min-ver", 271, 4 },
 { "artwork-display-gamut", 275, 3 },
 { "front-cam-offset-from-center", 278, 16 },
 { "ui-reachability", 294, 4 },
 { "volume-down-button-location", 298, 20 },
 { "personal-hotspot", 318, 4 },
 { "single-stage-boot", 322, 4 },
 { "mobiledevice-beta-min-ver", 326, 11 },
 { "oled-display", 337, 4 },
 { "thin-bezel", 341, 4 },
 { "artwork-device-subtype", 345, 4 },
 { "side-button-location", 349, 20 },
};
static const unsigned char identity_product_data[] = {
 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
 0x01, 0x00, 0x00, 0x00, 0x34, 0x33, 0x35, 0x37, 0x00, 0x69, 0x50, 0x68, 0x6f, 0x6e, 0x65, 0x20, 0x58, 0x00, 0x01, 0x00,
 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x31, 0x34, 0x30, 0x30, 0x00, 0xfd, 0x07, 0x9b, 0x29, 0x81, 0x44, 0xa7, 0xde, 0xb4,
 0xd7, 0xc8, 0x7f, 0xef, 0x3e, 0xb4, 0x74, 0xd8, 0xf0, 0xe7, 0xeb, 0x01, 0x00, 0x00, 0x00, 0x69, 0x50, 0x68, 0x6f, 0x6e,
 0x65, 0x20, 0x58, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x01, 0x00, 0x00, 0x00, 0x69, 0x50, 0x68, 0x6f, 0x6e, 0x65, 0x39, 0x2c, 0x32, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00,
 0x00, 0x00, 0x6d, 0x61, 0x76, 0x31, 0x37, 0x00, 0x01, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
 0x70, 0x68, 0x6f, 0x6e, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0x50, 0x50, 0x4c, 0x45, 0x34, 0x00, 0x01, 0x00, 0x00,
 0x00, 0x30, 0x00, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x78, 0xb0, 0x09, 0x00, 0xff, 0x96, 0x14, 0x00, 0x30, 0xe0, 0x03,
 0x00, 0xca, 0x9d, 0x01, 0x00, 0x10, 0x27, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
 0x00, 0x01, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x41, 0x50, 0x50, 0x4c, 0x45, 0x33, 0x3a, 0x41, 0x50, 0x50, 0x4c,
 0x45, 0x33, 0x76, 0x31, 0x3a, 0x41, 0x50, 0x50, 0x4c, 0x45, 0x32, 0x3a, 0x41, 0x50, 0x50, 0x4c, 0x45, 0x31, 0x3a, 0x47,
 0x4c, 0x45, 0x53, 0x32, 0x2c, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x07, 0x0c, 0x00, 0x50, 0x33, 0x00, 0xd0, 0x01,
 0x01, 0x00, 0x9e, 0x1d, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x78, 0xb0,
 0x09, 0x00, 0xff, 0x96, 0x14, 0x00, 0x48, 0xd0, 0x05, 0x00, 0xca, 0x9d, 0x01, 0x00, 0x10, 0x27, 0x00, 0x00, 0x01, 0x00,
 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x31, 0x31, 0x39, 0x30, 0x2e, 0x31, 0x30, 0x30, 0x2e, 0x32, 0x00, 0x01, 0x00, 0x00,
 0x00, 0x01, 0x00, 0x00, 0x00, 0x84, 0x09, 0x00, 0x00, 0x75, 0xf3, 0x00, 0x00, 0x2a, 0x0f, 0x02, 0x00, 0x00, 0x71, 0x00,
 0x00, 0xea, 0x3f, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00,
};
static const struct identity_prop identity_haptics[] = {
 { "closed-loop", 0, 4 },
 { "supports-3rd-party-haptics", 4, 4 },
};
static const unsigned char identity_haptics_data[] = {
 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};
static const struct identity_prop identity_camera[] = {
 { "aggregate-cam-video-zoom", 0, 4 },
 { "rear-max-video-fps-4k", 4, 4 },
 { "rear-max-video-zoom", 8, 4 },
 { "rear-max-burst-length", 12, 4 },
 { "camera-hdr-version", 16, 4 },
 { "front-max-burst-length", 20, 4 },
 { "auto-focus", 24, 4 },
 { "front-auto-hdr", 28, 4 },
 { "front-burst-image-duration", 32, 4 },
 { "rear-max-video-fps-720p", 36, 4 },
 { "rear-slowmo", 40, 4 },
 { "flash", 44, 4 },
 { "pipelined-stillimage-capability", 48, 4 },
 { "rear-max-slomo-video-fps-1080p", 52, 4 },
 { "front-hdr", 56, 4 },
 { "front-flash-capability", 60, 4 },
 { "pearl-camera", 64, 4 },
 { "video-cap", 68, 4 },
 { "front-hdr-on", 72, 4 },
 { "rear-auto-hdr", 76, 4 },
 { "front-burst", 80, 4 },
 { "p3-color-space-video-recording", 84, 4 },
 { "rear-burst-image-duration", 88, 4 },
 { "rear-max-slomo-video-fps-720p", 92, 4 },
 { "video-stills", 96, 4 },
 { "rear-max-video-frame_rate", 100, 4 },
 { "rear-hdr-on", 104, 4 },
 { "rear-max-video-fps-1080p", 108, 4 },
 { "aggregate-camera", 112, 4 },
 { "rear-stage-light-portrait", 116, 0 },
 { "aggregate-cam-photo-zoom", 116, 4 },
 { "rear-hdr", 120, 4 },
 { "rear-burst", 124, 4 },
 { "rear-cam-telephoto-capability", 128, 4 },
 { "post-effects", 132, 4 },
 { "stage-light-portrait-preview", 136, 4 },
 { "live-effects", 140, 4 },
 { "front-max-video-zoom", 144, 4 },
 { "front-max-video-fps-720p", 148, 4 },
 { "panorama", 152, 4 },
 { "front-max-video-fps-1080p", 156, 4 },
 { "front-stage-light-portrait", 160, 0 },
 { "live-photo-capture", 160, 4 },
};
static const unsigned char identity_camera_data[] = {
 0x58, 0x02, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x58, 0x02, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
 0x58, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00,
 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
 0x3c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00,
 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00,
 0x01, 0x00, 0x00, 0x00,
};
_Static_assert(4 >= 1 && 4 <= 7, "one core at least, and one left for the watcher");
static const uint32_t core_order[8] = { 0, 4, 5, 7, 1, 2, 3, 6 };
char *dbg_tee;
uint32_t dbg_tee_cap, dbg_tee_at;
struct snap_hdr {
 uint32_t magic;
 uint32_t version;
 uint64_t len;
 uint64_t ticks;
 uint64_t reserve[4];
};
struct boot_video {
 uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};
struct boot_args {
 uint16_t revision;
 uint16_t version;
 uint64_t virtBase;
 uint64_t physBase;
 uint64_t memSize;
 uint64_t topOfKernelData;
 struct boot_video video;
 uint32_t machineType;
 uint64_t deviceTreeP;
 uint32_t deviceTreeLength;
 char commandLine[608];
 uint64_t bootFlags;
 uint64_t memSizeActual;
};
_Static_assert(__builtin_offsetof(struct boot_args, virtBase) == 8, "boot_args");
_Static_assert(__builtin_offsetof(struct boot_args, physBase) == 16, "boot_args");
_Static_assert(__builtin_offsetof(struct boot_args, video) == 40, "boot_args");
_Static_assert(__builtin_offsetof(struct boot_args, deviceTreeP) == 96, "boot_args");
_Static_assert(__builtin_offsetof(struct boot_args, commandLine) == 108, "boot_args");
_Static_assert(__builtin_offsetof(struct boot_args, bootFlags) == 720, "boot_args");
struct mach_header_64 {
 uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved;
};
struct load_command {
 uint32_t cmd, cmdsize;
};
struct segment_command_64 {
 uint32_t cmd, cmdsize;
 char segname[16];
 uint64_t vmaddr, vmsize, fileoff, filesize;
 uint32_t maxprot, initprot, nsects, flags;
};
struct symtab_command {
 uint32_t cmd, cmdsize, symoff, nsyms, stroff, strsize;
};
struct nlist_64 {
 uint32_t n_strx;
 uint8_t n_type, n_sect;
 uint16_t n_desc;
 uint64_t n_value;
};
struct payload_header {
 uint32_t magic;
 uint32_t size;
};
struct payload_entry {
 char name[16];
 uint32_t size;
 uint32_t reserved;
};
struct payload_info {
 const uint8_t *kernel;
 uint32_t kernel_size;
 const uint8_t *ramdisk;
 uint32_t ramdisk_size;
 const char *kernel_version;
};
struct xnu_syms {
 uint64_t stub_slot, vc_colors, log, log_len, log_ring_at, log_wrapped, panic_str, panic_args, iso_font,
     msgbufp, cpu_ttep, live, uart_tx_ready, uart_tx_data, uart_rx_ready, uart_putc, uart_putc_options, log_putc,
     nprocs, tasks_count, initproc, gic_cpu_init,
     gic_unhandled_count, gic_last_unhandled, gic_active_iar, host_keep, host_keep_len, host_keep_dropped,
     hk_keep, hk_keep_len, hk_keep_dropped;
};
static struct xnu_syms xsyms;
static const uint8_t *xkernel;
uint64_t xnu_fp[4];
static volatile uint64_t xnu_back __attribute__((aligned(64)));
struct dt_writer {
 uint8_t *base, *cur;
 uint32_t *counts[16];
 int depth;
};
extern char _start[];
extern const uint64_t mtk_runtime_start;
extern uint64_t mtk_here(void);
extern const uint64_t mtk_runtime_linked;
extern char chain_stub[], chain_stub_end[];
extern char xnu_return_stub[];
extern void jump_to_kernel(uint64_t entry_pa, uint64_t boot_args_pa);
extern void psci_system_reset(void);
extern int64_t psci_cpu_on(uint64_t mpidr, uint64_t entry, uint64_t context);
extern void watcher_entry(void);
extern void mmu_on(uint64_t l1_table);
extern void dcache_clean_range(uint64_t va, uint64_t len);
extern uint64_t irq_wait(uint64_t until_ticks);
uint64_t irq_count, irq_intid;
static uint64_t lk_dtb;
void *memset(void *dst, int c, size_t n)
{
 uint8_t *d = dst;
 if (c == 0 && !((uintptr_t)d & 7)) {
  for (; n >= 8; n -= 8, d += 8) {
   *(volatile uint64_t *)d = 0;
  }
 }
 while (n--) {
  *(volatile uint8_t *)d++ = (uint8_t)c;
 }
 return dst;
}
void *memcpy(void *dst, const void *src, size_t n)
{
 uint8_t *d = dst;
 const uint8_t *s = src;
 if (!(((uintptr_t)d | (uintptr_t)s) & 7)) {
  for (; n >= 8; n -= 8, d += 8, s += 8) {
   *(volatile uint64_t *)d = *(const volatile uint64_t *)s;
  }
 }
 while (n--) {
  *(volatile uint8_t *)d++ = *(const volatile uint8_t *)s++;
 }
 return dst;
}
size_t strlen(const char *s)
{
 size_t len = 0;
 while (*s++) {
  len++;
 }
 return len;
}
static int memcmp_n(const volatile void *a, const void *b, size_t n)
{
 const volatile uint8_t *x = a;
 const uint8_t *y = b;
 for (size_t i = 0; i < n; i++) {
  if (x[i] != y[i]) {
   return 1;
  }
 }
 return 0;
}
int strcmp(const char *s1, const char *s2)
{
 while (*s1 && *s1 == *s2) {
  s1++;
  s2++;
 }
 return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}
static void uart_putc(char c)
{
 (void)c;
}
static void out_putc(char c)
{
 if (c == '\n') {
  uart_putc('\r');
 }
 uart_putc(c);
 dbg_putc(c);
}
static void out(const char *s)
{
 while (*s) {
  out_putc(*s++);
 }
}
static void out_hex(uint64_t v)
{
 static const char hex[] = "0123456789abcdef";
 int i = 60;
 out("0x");
 while (i > 0 && !((v >> i) & 0xf)) {
  i -= 4;
 }
 for (; i >= 0; i -= 4) {
  out_putc(hex[(v >> i) & 0xf]);
 }
}
static void out_dec(uint64_t v)
{
 char buf[21];
 int n = 0;
 do {
  buf[n++] = (char)('0' + v % 10);
  v /= 10;
 } while (v);
 while (n) {
  out_putc(buf[--n]);
 }
}
static uint32_t
dt_be32(const uint8_t *p)
{
 return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static int
dt_streq(const char *a, const char *b)
{
 while (*a && *a == *b) {
  a++;
  b++;
 }
 return *a == *b;
}
static int
dt_contains(const char *hay, uint32_t len, const char *needle)
{
 for (uint32_t i = 0; i < len; i++) {
  uint32_t j = 0;
  while (needle[j] && i + j < len && hay[i + j] == needle[j]) {
   j++;
  }
  if (!needle[j]) {
   return 1;
  }
 }
 return 0;
}
static int
dt_wanted(const char *text, uint32_t len)
{
 static const char *words[] = { "consys", "conninfra", "connsys", "wifi", "wlan", "gps",
     "sensor", "accel", "gsensor", "gyro", "alsps", "magnet", "scp",
     "btif" };
 for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
  if (dt_contains(text, len, words[i])) {
   return 1;
  }
 }
 return 0;
}
static uint32_t
dt_len(const char *s)
{
 uint32_t n = 0;
 while (s[n]) {
  n++;
 }
 return n;
}
static void
dt_words(const char *what, const uint8_t *value, uint32_t len)
{
 out("    ");
 out(what);
 out(":");
 for (uint32_t i = 0; i + 4 <= len && i < 4 * 48; i += 4) {
  out(" ");
  out_hex(dt_be32(value + i));
 }
 out("\n");
}
static void
connsys_dt_report(uint64_t dtb)
{
 const uint8_t *fdt = (const uint8_t *)(uintptr_t)dtb;
 const uint8_t *structs, *p, *end;
 const char *strings;
 struct {
  const char *name;
  const uint8_t *compatible, *reg, *interrupts, *size, *ranges;
  uint32_t compatible_len, reg_len, interrupts_len, size_len, ranges_len;
  uint32_t address_cells, size_cells;
 } level[24];
 int depth = -1;
 unsigned shown = 0;
 if (dtb == 0 || dt_be32(fdt) != 0xd00dfeedu) {
  out("connsys: no flattened device tree from LK\n");
  return;
 }
 structs = fdt + dt_be32(fdt + 8);
 strings = (const char *)(fdt + dt_be32(fdt + 12));
 end = structs + dt_be32(fdt + 36);
 out("connsys: Android's device tree, the nodes that say consys/conninfra/wifi/gps:\n");
 for (p = structs; p < end;) {
  uint32_t token = dt_be32(p);
  p += 4;
  if (token == 1) {
   const char *name = (const char *)p;
   p += (dt_len(name) + 1 + 3) & ~3u;
   if (++depth >= 24) {
    out("connsys: the tree is deeper than this walk keeps; stopped\n");
    return;
   }
   level[depth].name = name;
   level[depth].compatible = level[depth].reg = level[depth].interrupts = ((void*)0);
   level[depth].size = level[depth].ranges = ((void*)0);
   level[depth].compatible_len = level[depth].reg_len = level[depth].interrupts_len = 0;
   level[depth].size_len = level[depth].ranges_len = 0;
   level[depth].address_cells = 2;
   level[depth].size_cells = 1;
  } else if (token == 3) {
   uint32_t len = dt_be32(p), name_offset = dt_be32(p + 4);
   const char *prop = strings + name_offset;
   const uint8_t *value = p + 8;
   p += 8 + ((len + 3) & ~3u);
   if (depth < 0) {
    continue;
   }
   if (dt_streq(prop, "compatible")) {
    level[depth].compatible = value;
    level[depth].compatible_len = len;
   } else if (dt_streq(prop, "reg")) {
    level[depth].reg = value;
    level[depth].reg_len = len;
   } else if (dt_streq(prop, "interrupts")) {
    level[depth].interrupts = value;
    level[depth].interrupts_len = len;
   } else if (dt_streq(prop, "size")) {
    level[depth].size = value;
    level[depth].size_len = len;
   } else if (dt_streq(prop, "alloc-ranges")) {
    level[depth].ranges = value;
    level[depth].ranges_len = len;
   } else if (dt_streq(prop, "#address-cells") && len == 4) {
    level[depth].address_cells = dt_be32(value);
   } else if (dt_streq(prop, "#size-cells") && len == 4) {
    level[depth].size_cells = dt_be32(value);
   }
  } else if (token == 2) {
   if (depth < 0) {
    break;
   }
   const char *name = level[depth].name;
   int wanted = dt_wanted(name, dt_len(name)) ||
       (level[depth].compatible && dt_wanted((const char *)level[depth].compatible, level[depth].compatible_len));
   if (wanted && shown < 48) {
    shown++;
    out("  ");
    out(depth > 0 ? level[depth - 1].name : "");
    out("/");
    out(name);
    if (depth > 0) {
     out(" (parent cells: address ");
     out_dec(level[depth - 1].address_cells);
     out(", size ");
     out_dec(level[depth - 1].size_cells);
     out(")");
    }
    out("\n");
    if (level[depth].compatible) {
     out("    compatible:");
     for (uint32_t i = 0; i < level[depth].compatible_len;) {
      const char *s = (const char *)level[depth].compatible + i;
      out(" ");
      out(s);
      i += dt_len(s) + 1;
     }
     out("\n");
    }
    if (level[depth].reg) {
     dt_words("reg", level[depth].reg, level[depth].reg_len);
    }
    if (level[depth].interrupts) {
     dt_words("interrupts", level[depth].interrupts, level[depth].interrupts_len);
    }
    if (level[depth].size) {
     dt_words("size", level[depth].size, level[depth].size_len);
    }
    if (level[depth].ranges) {
     dt_words("alloc-ranges", level[depth].ranges, level[depth].ranges_len);
    }
   }
   depth--;
  } else if (token == 4) {
   continue;
  } else {
   break;
  }
 }
 out("connsys: ");
 out_dec(shown);
 out(" nodes\n");
}
static void marker_set(uint32_t v)
{
 for (int i = 0; i < 3; i++) {
  wr32(0x90000000ULL + 64 * i, v);
 }
}
static void ufs_step(uint32_t n)
{
 wr32((0x90000000ULL + 512ULL), n);
 dcache_clean_range((0x90000000ULL + 512ULL), 64);
}
static uint32_t ufs_step_get(void)
{
 return rd32((0x90000000ULL + 512ULL));
}
static uint32_t marker_get(void)
{
 uint32_t a = rd32(0x90000000ULL), b = rd32(0x90000000ULL + 64), c = rd32(0x90000000ULL + 128);
 return (a == b || a == c) ? a : (b == c ? b : 0);
}
static uint64_t fb_base;
static uint32_t fb_pitch;
static void display_find(void)
{
 fb_base = 0xfe14e000ULL;
 fb_pitch = 4352u;
 return;
 {
 uint32_t addr = rd32(0x14002000ULL + 0xf40);
 uint32_t pitch = rd32(0x14002000ULL + 0x044) & 0xffff;
 if (addr >= 0xfe14e000ULL && addr < 0xfe14e000ULL + 0x1eb1000ULL &&
     pitch >= 1080u * 4 && pitch <= 8192u) {
  fb_base = addr;
  fb_pitch = pitch;
 }
 }
}
static void display_alpha_off(void)
{
 return;
 {
 uint32_t con = rd32(0x14002000ULL + 0x030);
 wr32(0x14002000ULL + 0x030, con & ~(1u << 8));
 out("OVL0 L0_CON ");
 out_hex(con);
 out(" -> ");
 out_hex(rd32(0x14002000ULL + 0x030));
 uint32_t src = rd32(0x14002000ULL + 0x02c);
 wr32(0x14002000ULL + 0x02c, src & 1u);
 wr32(0x14001020ULL, 1);
 wr32(0x14001024ULL, 1);
 out(", SRC_CON ");
 out_hex(src);
 out(" -> ");
 out_hex(rd32(0x14002000ULL + 0x02c));
 out("\n");
 }
}
static int payload_header_ok(const struct payload_header *ph)
{
 const uint8_t *e = (const uint8_t *)(ph + 1);
 const uint8_t *eend;
 if (ph->magic != 0x50554e58u || ph->size < sizeof(struct payload_entry) ||
     ph->size > 0x4000000ULL) {
  return 0;
 }
 eend = e + ph->size;
 if (ph->size >= sizeof(struct payload_entry)) {
  const struct payload_entry *en = (const struct payload_entry *)e;
  if (strcmp(en->name, "kernel") == 0 && en->size > 4) {
   const uint8_t *d = e + sizeof(*en);
   if (((const struct mach_header_64 *)d)->magic == 0xfeedfacfu) {
    return 1;
   }
  }
 }
 (void)eend;
 return 0;
}
static const struct payload_header *payload_find(void)
{
 uint64_t base = mtk_here();
 const volatile uint64_t *hdr = (const volatile uint64_t *)base;
 uint64_t off = hdr[6];
 if (off != 0 && (off & 15) == 0 && off < 0x4000000ULL) {
  const struct payload_header *ph = (const void *)(base + off);
  if (payload_header_ok(ph)) {
   return ph;
  }
 }
 for (uint64_t at = 0x1000;
      at + sizeof(struct payload_header) < 0x4000000ULL;
      at += 16) {
  const volatile uint32_t *w = (const volatile uint32_t *)(base + at);
  if (*w == 0x50554e58u) {
   const struct payload_header *ph = (const void *)(base + at);
   if (payload_header_ok(ph)) {
    return ph;
   }
  }
 }
 return ((void*)0);
}
static int parse_payload(struct payload_info *pi)
{
 const struct payload_header *ph = payload_find();
 memset(pi, 0, sizeof(*pi));
 if (!ph) {
  return 0;
 }
 const uint8_t *p = (const uint8_t *)(ph + 1), *pend = p + ph->size;
 while (p + sizeof(struct payload_entry) <= pend) {
  const struct payload_entry *e = (const struct payload_entry *)p;
  if (e->name[0] == 0) {
   break;
  }
  if (strcmp(e->name, "kernel") == 0) {
   pi->kernel = p + sizeof(*e);
   pi->kernel_size = e->size;
  } else if (strcmp(e->name, "ramdisk") == 0) {
   pi->ramdisk = p + sizeof(*e);
   pi->ramdisk_size = e->size;
  } else if (strcmp(e->name, "kernel.ver") == 0 && e->size > 1) {
   pi->kernel_version = (const char *)(p + sizeof(*e));
  }
  p += sizeof(*e) + ((e->size + 15) & ~15u);
 }
 return pi->kernel != ((void*)0) && ((const struct mach_header_64 *)pi->kernel)->magic == 0xfeedfacfu;
}
static void find_syms(const uint8_t *kernel, struct xnu_syms *s)
{
 static const char *const names[] = {
  "_loader_stub_slot", "_vc_colors", "_loader_log", "_loader_log_len", "_loader_log_ring_at",
  "_loader_log_wrapped", "_debugger_panic_str", "_debugger_panic_args", "_iso_font",
  "_msgbufp", "_cpu_ttep", "_loader_live",
  "_vmapple_uart_transmit_ready", "_vmapple_uart_transmit_data",
  "_vmapple_uart_receive_ready", "_uart_putc", "_uart_putc_options", "_loader_log_putc",
  "_nprocs", "_tasks_count", "_initproc", "_gic_v3_irq_cpu_init",
  "_gic_unhandled_count", "_gic_last_unhandled", "_gic_active_iar",
  "_redwood_host_keep", "_redwood_host_keep_len", "_redwood_host_keep_dropped",
  "_hakios_host_keep", "_hakios_host_keep_len", "_hakios_host_keep_dropped",
 };
 _Static_assert(sizeof(struct xnu_syms) == sizeof(names), "every field needs its name, in order");
 uint64_t *slots = (uint64_t *)s;
 const struct mach_header_64 *mh = (const struct mach_header_64 *)kernel;
 const struct load_command *lc = (const struct load_command *)(mh + 1);
 const struct symtab_command *st = ((void*)0);
 memset(s, 0, sizeof(*s));
 for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const void *)((const uint8_t *)lc + lc->cmdsize)) {
  if (lc->cmd == 0x2) {
   st = (const void *)lc;
  }
 }
 if (!st) {
  return;
 }
 const struct nlist_64 *sym = (const struct nlist_64 *)(kernel + st->symoff);
 const char *strs = (const char *)(kernel + st->stroff);
 for (uint32_t i = 0; i < st->nsyms; i++) {
  const char *n = strs + sym[i].n_strx;
  if (n[0] != '_' || (n[1] != 'l' && n[1] != 'v' && n[1] != 'd' && n[1] != 'i' && n[1] != 'm' && n[1] != 'c' && n[1] != 'g' && n[1] != 'n' && n[1] != 't' && n[1] != 'r' && n[1] != 'h')) {
   continue;
  }
  for (unsigned k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
   if (!slots[k] && strcmp(n, names[k]) == 0) {
    slots[k] = sym[i].n_value;
   }
  }
 }
}
static uint64_t kva_to_pa(uint64_t va)
{
 return va + (0x100000000ULL - 0xfffffff004000000ULL);
}
static uint8_t gpt_entries[128 * 128] __attribute__((aligned(16)));
static uint8_t sector_buf[512] __attribute__((aligned(16)));
static int board_read(uint64_t lba, uint32_t blocks, void *dst)
{
 return ufs_read(lba, blocks, dst);
}
static const char *board_storage_name(void)
{
 return "UFS";
}
static const char *board_storage_what(void)
{
 return ufs_last.what ? ufs_last.what : "no error recorded";
}
static uint32_t board_storage_status(void)
{
 return ufs_last.status;
}
static int board_read_bytes(uint64_t off, uint64_t bytes, void *dst)
{
 static uint8_t bounce[512] __attribute__((aligned(16)));
 uint8_t *d = (uint8_t *)dst;
 if (off & 511ull) {
  return 0;
 }
 while (bytes) {
  uint64_t cap = (uint64_t)(8u << 20);
  uint64_t want = bytes > cap ? cap : bytes;
  uint32_t n = (uint32_t)((want + 511ull) & ~511ull);
  if (bytes >= (uint64_t)n) {
   if (!board_read(off / 512ull, n / 512u, d)) {
    return 0;
   }
   d += n;
  } else {
   if (!board_read(off / 512ull, n / 512u, bounce)) {
    return 0;
   }
   memcpy(d, bounce, (size_t)bytes);
  }
  off += n;
  bytes = bytes > (uint64_t)n ? bytes - (uint64_t)n : 0;
 }
 return 1;
}
static uint32_t gpt_block_size;
static uint32_t gpt_detect(void)
{
 static const uint32_t try[2] = { 512, 4096 };
 if (gpt_block_size) {
  return gpt_block_size;
 }
 for (int i = 0; i < 2; i++) {
  if (board_read_bytes(try[i], 512, sector_buf) &&
      !memcmp_n(sector_buf, "EFI PART", 8)) {
   gpt_block_size = try[i];
   out("gpt: ");
   out_dec(gpt_block_size);
   out("-byte blocks\n");
   return gpt_block_size;
  }
 }
 return 0;
}
static int gpt_find(const char *name, uint64_t *lba, uint64_t *blocks)
{
 if (!gpt_detect()) {
  return 0;
 }
 if (!board_read_bytes((uint64_t)gpt_block_size, 512, sector_buf)) {
  return 0;
 }
 if (memcmp_n(sector_buf, "EFI PART", 8)) {
  return 0;
 }
 uint64_t ent_lba = *(volatile uint64_t *)(sector_buf + 72);
 uint32_t count = *(volatile uint32_t *)(sector_buf + 80);
 uint32_t size = *(volatile uint32_t *)(sector_buf + 84);
 if (count > 128 || size != 128) {
  return 0;
 }
 if (!board_read_bytes(ent_lba * gpt_block_size, (uint64_t)count * size, gpt_entries)) {
  return 0;
 }
 for (uint32_t i = 0; i < count; i++) {
  const uint8_t *e = gpt_entries + i * size;
  int same = 1;
  for (int k = 0; same && name[k - (k ? 0 : 0)] != 0; k++) {
   same = e[56 + 2 * k] == (uint8_t)name[k] && e[57 + 2 * k] == 0;
  }
  if (same && e[56 + 2 * (int)strlen(name)] == 0) {
   uint64_t first = *(const volatile uint64_t *)(e + 32);
   uint64_t last = *(const volatile uint64_t *)(e + 40);
   if (first == 0 || last < first) {
    return 0;
   }
   *lba = first;
   *blocks = last - first + 1;
   return 1;
  }
 }
 return 0;
}
static uint32_t sysimg_found, sysimg_bytes, sysimg_secs;
static uint64_t load_system_image(void)
{
 uint64_t lba, blocks, size = 0, done = 0, t0 = dbg_ticks(), f = dbg_freq();
 if (!gpt_find("xnusys", &lba, &blocks)) {
  out("no " "xnusys" " partition on the ");
  out(board_storage_name());
  out(" (or the GPT could not be read: ");
  out(board_storage_what());
  out(")\n");
  return 0;
 }
 out("xnusys" " at block ");
 out_hex(lba);
 out(", ");
 out_dec(blocks * (uint64_t)gpt_block_size / 1024ull / 1024ull);
 out(" MB\n");
 uint64_t base = lba * (uint64_t)gpt_block_size;
 uint64_t span = blocks * (uint64_t)gpt_block_size;
 for (uint64_t at = 0;;) {
  if (!board_read_bytes(base + at, 512, sector_buf)) {
   out("cannot read it: ");
   out(board_storage_what());
   out("\n");
   return 0;
  }
  if (*(volatile uint32_t *)(sector_buf + 32) != 0x4253584eu) {
   break;
  }
  uint32_t bs = *(volatile uint32_t *)(sector_buf + 36);
  uint64_t count = *(volatile uint64_t *)(sector_buf + 40);
  if (bs < 512 || bs > 65536 || count == 0 || count > span / bs) {
   out("the APFS container at ");
   out_dec(at / 1024 / 1024);
   out(" MB says ");
   out_dec(count);
   out(" blocks of ");
   out_dec(bs);
   out(": that cannot be\n");
   return 0;
  }
  at += count * bs;
  size = at;
  if (at >= span) {
   break;
  }
 }
 if (size == 0) {
  out("no APFS container there (write userland/system.apfs to it)\n");
  return 0;
 }
 out("APFS: ");
 out_dec(size / 1024 / 1024);
 out(" MB\n");
 if (0 && size > (uint64_t)0 * 1024 * 1024) {
  size = (uint64_t)0 * 1024 * 1024;
  out("(only the first ");
  out_dec(0);
  out(" MB of it, as built)\n");
 }
 out("hs ");
 out_dec(ufs_hs_stage); out(" ok "); out_dec(ufs_hs_ok);
 out(" upm "); out_dec(ufs_hs_upmcrs);
 out(" rx "); out_dec(ufs_hs_gear_rx); out("/"); out_dec(ufs_hs_lane_rx);
 out(" tx "); out_dec(ufs_hs_gear_tx); out("/"); out_dec(ufs_hs_lane_tx);
 out("\n");
 out("reading ");
 out_dec(size / 1024 / 1024);
 out(" MB into ");
 out_hex((0x100000000ULL + 0x08000000ULL));
 out(": ");
 uint32_t last_shown = 0;
 uint32_t chunks = 0;
 while (done < size) {
  uint64_t chunk = size - done;
  if (chunk > (uint64_t)(8u << 20)) {
   chunk = (8u << 20);
  }
  if (!board_read_bytes(base + done, chunk, (void *)((0x100000000ULL + 0x08000000ULL) + done))) {
   out("\nfailed at ");
   out_dec(done / 1024 / 1024);
   out(" MB: ");
   out(board_storage_what());
   out(" status ");
   out_hex(board_storage_status());
   out("\n");
   return 0;
  }
  done += chunk;
  chunks++;
  uint32_t pct = (uint32_t)(done * 100 / size);
  if (pct >= last_shown + 2 || chunks <= 6u) {
   if (pct >= last_shown + 2) {
    last_shown = pct - pct % 2;
   }
   out_dec(pct);
   out("% ");
   out_dec(ufs_run_polls[0]);
   out("p ");
  }
 }
 uint64_t ms = (dbg_ticks() - t0) * 1000 / f;
 out("done in ");
 out_dec(ms / 1000);
 out(" s (");
 out_dec(size / 1024 / (ms ? ms : 1) * 1000 / 1024);
 out(" MB/s)\n");
 sysimg_found = 1;
 sysimg_bytes = (uint32_t)size;
 sysimg_secs = (uint32_t)(ms / 1000u);
 return size;
}
static void dt_prop(struct dt_writer *w, const char *name, const void *data, uint32_t len)
{
 memset(w->cur, 0, 32);
 memcpy(w->cur, name, strlen(name));
 *(volatile uint32_t *)(w->cur + 32) = len;
 memset(w->cur + 36, 0, (len + 3) & ~3u);
 memcpy(w->cur + 36, data, len);
 w->cur += 36 + ((len + 3) & ~3u);
 w->counts[w->depth - 1][0]++;
}
static void dt_prop_str(struct dt_writer *w, const char *name, const char *value)
{
 dt_prop(w, name, value, (uint32_t)strlen(value) + 1);
}
static void dt_prop_u32(struct dt_writer *w, const char *name, uint32_t value)
{
 dt_prop(w, name, &value, sizeof(value));
}
static void dt_prop_u64(struct dt_writer *w, const char *name, uint64_t value)
{
 dt_prop(w, name, &value, sizeof(value));
}
static void dt_prop_padded(struct dt_writer *w, const char *name, const char *value, uint32_t size)
{
 uint8_t field[64];
 memset(field, 0, sizeof(field));
 memcpy(field, value, strlen(value));
 dt_prop(w, name, field, size);
}
static void dt_begin(struct dt_writer *w, const char *name)
{
 if (w->depth > 0) {
  w->counts[w->depth - 1][1]++;
 }
 w->counts[w->depth++] = (uint32_t *)w->cur;
 memset(w->cur, 0, 8);
 w->cur += 8;
 dt_prop_str(w, "name", name);
}
static void dt_end(struct dt_writer *w)
{
 w->depth--;
}
static void dt_identity(struct dt_writer *w, const struct identity_prop *props, uint32_t count, const unsigned char *data)
{
 for (uint32_t i = 0; i < count; i++) {
  if (strcmp(props[i].name, "artwork-display-gamut") == 0) {
   dt_prop(w, props[i].name, "sRGB", 5);
   continue;
  }
  if (strcmp(props[i].name, "artwork-device-subtype") == 0) {
   static const uint32_t subtype = 2436;
   dt_prop(w, props[i].name, &subtype, sizeof(subtype));
   continue;
  }
  dt_prop(w, props[i].name, data + props[i].offset, props[i].length);
 }
}
static uint8_t nvram_image[0x2000u];
static void nvram_chrp_header(uint8_t *h, uint8_t sig, uint32_t bytes, const char *name)
{
 uint16_t blocks = (uint16_t)(bytes / 16), sum;
 memset(h, 0, 16);
 h[0] = sig;
 h[2] = (uint8_t)blocks;
 h[3] = (uint8_t)(blocks >> 8);
 memcpy(h + 4, name, strlen(name));
 sum = h[0];
 for (int i = 2; i < 16; i++) {
  sum += h[i];
 }
 while (sum > 0xff) {
  sum = (sum & 0xff) + (sum >> 8);
 }
 h[1] = (uint8_t)sum;
}
static void build_nvram_image(uint8_t *image)
{
 uint32_t lo = 1, hi = 0, generation = 1;
 memset(image, 0, 0x2000u);
 nvram_chrp_header(image, 0x5a, 0x20, "nvram");
 nvram_chrp_header(image + 0x20, 0x70, 0x2000u - 0x20, "common");
 memcpy(image + 20, &generation, 4);
 for (uint32_t i = 20; i < 0x2000u; i++) {
  lo = (lo + image[i]) % 65521;
  hi = (hi + lo) % 65521;
 }
 uint32_t adler = (hi << 16) | lo;
 memcpy(image + 16, &adler, 4);
}
static uint32_t build_device_tree(uint64_t dt_pa, uint64_t rd_pa, uint64_t rd_size,
    uint64_t sys_pa, uint64_t sys_size)
{
 struct dt_writer w = { .base = (uint8_t *)dt_pa, .cur = (uint8_t *)dt_pa, .depth = 0 };
 uint64_t cntfrq = dbg_freq();
 uint8_t seed[64];
 for (unsigned i = 0; i < sizeof(seed); i++) {
  seed[i] = (uint8_t)(dbg_ticks() >> (i & 7)) ^ (uint8_t)(i * 37);
 }
 dt_begin(&w, "device-tree");
 dt_identity(&w, identity_root, sizeof(identity_root) / sizeof(identity_root[0]), identity_root_data);
 dt_prop_str(&w, "platform-name", "t8015");
 dt_prop_padded(&w, "serial-number", "HAKIOS0000000000", 32);
 dt_prop_padded(&w, "mlb-serial-number", "HAKIOS0000000000" "MLB", 32);
 dt_prop_padded(&w, "model-number", "MQAC2", 32);
 dt_prop_padded(&w, "region-info", "LL/A", 32);
 dt_prop_padded(&w, "regulatory-model-number", "A1865", 32);
 dt_prop_u32(&w, "#address-cells", 2);
 dt_prop_u32(&w, "#size-cells", 2);
 dt_begin(&w, "chosen");
 dt_prop_str(&w, "darwin-board", "xaga");
 dt_prop(&w, "random-seed", seed, sizeof(seed));
 dt_prop_u64(&w, "dram-base", 0x100000000ULL);
 dt_prop_u64(&w, "dram-size", 0x100000000ULL);
 dt_prop_u32(&w, "debug-enabled", 1);
 dt_prop_u32(&w, "display-scale", 2);
 dt_prop_u32(&w, "display-rotation", 0);
 dt_prop_str(&w, "psci-method", "smc");
 static const uint8_t wifi_address[6] = { 0x02, 0x52, 0x44, 0x57, 0x00, 0x01 };
 static const uint8_t bluetooth_address[6] = { 0x02, 0x52, 0x44, 0x57, 0x00, 0x02 };
 static const uint8_t manifest_hash[48] = { 0x72, 0x65, 0x64, 0x77, 0x6f, 0x6f, 0x64 };
 static const uint8_t no_behavior[16];
 dt_prop_u64(&w, "unique-chip-id", 0x000052445730001ULL);
 dt_prop_u32(&w, "chip-id", 0x8015);
 dt_prop_u32(&w, "board-id", 0x06);
 dt_prop(&w, "mac-address-wifi0", wifi_address, sizeof(wifi_address));
 dt_prop(&w, "mac-address-bluetooth0", bluetooth_address, sizeof(bluetooth_address));
 dt_prop(&w, "boot-manifest-hash", manifest_hash, sizeof(manifest_hash));
 dt_prop(&w, "software-behavior", no_behavior, sizeof(no_behavior));
 dt_prop_str(&w, "firmware-version", "iBoot-8422.100.650");
 dt_prop_u32(&w, "certificate-production-status", 1);
 dt_prop_u32(&w, "certificate-security-mode", 1);
 dt_prop_u32(&w, "effective-production-status-ap", 1);
 dt_prop_u32(&w, "effective-security-mode-ap", 1);
 dt_prop_u32(&w, "secure-boot", 1);
 dt_prop_u32(&w, "production-cert", 1);
 dt_prop_u32(&w, "development-cert", 0);
 dt_prop_u32(&w, "embedded-panic-log-size", 0x80000u);
 build_nvram_image(nvram_image);
 dt_prop_u32(&w, "nvram-bank-size", 0x2000u);
 dt_prop_u32(&w, "nvram-bank-count", 1);
 dt_prop_u32(&w, "nvram-current-bank", 0);
 dt_prop(&w, "nvram-proxy-data", nvram_image, sizeof(nvram_image));
 dt_begin(&w, "memory-map");
 if (sys_size != 0) {
  uint64_t sys_reg[2] = { sys_pa, sys_size };
  dt_prop(&w, "RAMDisk", sys_reg, sizeof(sys_reg));
  if (rd_size != 0) {
   uint64_t rd_reg[2] = { rd_pa, rd_size };
   dt_prop(&w, "RAMDisk1", rd_reg, sizeof(rd_reg));
  }
 } else if (rd_size != 0) {
  uint64_t rd_reg[2] = { rd_pa, rd_size };
  dt_prop(&w, 0 ? "RAMDisk1" : "RAMDisk", rd_reg, sizeof(rd_reg));
 }
 dt_end(&w);
 dt_end(&w);
 dt_begin(&w, "defaults");
 dt_prop_u32(&w, "kern.vm_compressor", 2);
 dt_end(&w);
 dt_begin(&w, "product");
 dt_identity(&w, identity_product, sizeof(identity_product) / sizeof(identity_product[0]), identity_product_data);
 dt_begin(&w, "haptics");
 dt_identity(&w, identity_haptics, sizeof(identity_haptics) / sizeof(identity_haptics[0]), identity_haptics_data);
 dt_end(&w);
 dt_end(&w);
 dt_begin(&w, "options");
 dt_end(&w);
 uint64_t pram_reg[2] = { 0x80100000ULL, 0x80000u };
 memset((void *)0x80100000ULL, 0, 0x80000u);
 dt_begin(&w, "pram");
 dt_prop(&w, "reg", pram_reg, sizeof(pram_reg));
 dt_end(&w);
 dt_begin(&w, "cpus");
 for (int i = 0; i < 4; i++) {
  char name[5] = { 'c', 'p', 'u', (char)('0' + i), 0 };
  int core = core_order[i];
  dt_begin(&w, name);
  dt_prop_str(&w, "device_type", "cpu");
  dt_prop_str(&w, "state", i == 0 ? "running" : "waiting");
  dt_prop_u32(&w, "reg", ((uint32_t)((core) << 8)));
  dt_prop_u32(&w, "cpu-id", (uint32_t)i);
  dt_prop_u64(&w, "timebase-frequency", cntfrq);
  dt_prop_u64(&w, "fixed-frequency", cntfrq);
  dt_prop_u64(&w, "clock-frequency", 1800000000ULL);
  dt_prop_u64(&w, "bus-frequency", 100000000ULL);
  dt_prop_u64(&w, "memory-frequency", 100000000ULL);
  dt_prop_u64(&w, "peripheral-frequency", 24000000ULL);
  if (((core) < 4)) {
   dt_prop_u32(&w, "small-core", 1);
  }
  dt_end(&w);
 }
 dt_end(&w);
 dt_begin(&w, "arm-io");
 dt_prop_str(&w, "device_type", "xaga");
 uint64_t ranges[3] = { 0, 0x0c000000ULL, 0x14000000ULL };
 dt_prop(&w, "ranges", ranges, sizeof(ranges));
 dt_prop_u32(&w, "#address-cells", 2);
 dt_prop_u32(&w, "#size-cells", 2);
 dt_begin(&w, "gic");
 dt_prop_str(&w, "device_type", "interrupt-controller");
 dt_prop_str(&w, "compatible", "arm,gic-v3");
 uint64_t gic_reg[4] = { 0x0c000000ULL - 0x0c000000ULL, 0x40000ULL,
     0x0c040000ULL - 0x0c000000ULL, 0x200000ULL };
 dt_prop(&w, "reg", gic_reg, sizeof(gic_reg));
 dt_end(&w);
 dt_begin(&w, "uart0");
 dt_prop_str(&w, "device_type", "uart");
 dt_prop_str(&w, "compatible", "mediatek,mt6577-uart");
 uint64_t uart_reg[2] = { 0x11001000ULL - 0x0c000000ULL, 0x1000 };
 dt_prop(&w, "reg", uart_reg, sizeof(uart_reg));
 dt_prop_u32(&w, "boot-console", 1);
 dt_end(&w);
 dt_end(&w);
 dt_end(&w);
 return (uint32_t)(w.cur - w.base);
}
static void __attribute__((noreturn)) reset_board(void)
{
 psci_system_reset();
 wdt_reset();
}
static void __attribute__((noreturn)) chain_android(void)
{
 const volatile uint64_t *hdr = (const volatile uint64_t *)_start;
 const volatile uint32_t *img = (const volatile uint32_t *)((uint64_t)_start + 0x40000u);
 uint64_t image_size = hdr[5];
 marker_set(0);
 if (image_size == 0 || img[14] != 0x644d5241u) {
  out("no Android kernel in this image: reset\n");
  dbg_delay_ms(3000);
  reset_board();
 }
 if (!((img[0] == 0x91005a4du && (img[1] & 0xfc000000u) == 0x14000000u) ||
       (img[0] & 0xfc000000u) == 0x14000000u)) {
  out("the Android kernel at ");
  out_hex((uint64_t)(uintptr_t)img);
  out(" does not start with a branch: not jumping\n");
  for (;;) {
   dbg_delay_ms(1000);
  }
 }
 out("starting Android\n");
 volatile uint32_t *dst = (volatile uint32_t *)0x60000000ULL;
 const volatile uint32_t *src = (const volatile uint32_t *)chain_stub;
 for (uint64_t i = 0; i < (uint64_t)(chain_stub_end - chain_stub) / 4; i++) {
  dst[i] = src[i];
 }
 __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
 void (*stub)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) = (void *)0x60000000ULL;
 stub(lk_dtb, 0x40080000ULL, (uint64_t)img, image_size, 0x40080000ULL);
 __builtin_unreachable();
}
void loader_fault(uint64_t vec, uint64_t esr, uint64_t elr, uint64_t far)
{
 fbc.fg = 0xffff4040u;
 out("\n*** loader fault: vector ");
 out_hex(vec);
 out(" ESR ");
 out_hex(esr);
 out(" ELR ");
 out_hex(elr);
 out(" FAR ");
 out_hex(far);
 out("\n");
 dbg_delay_ms(5000);
 out("halting; the watchdog will restart the board\n");
 wdt_arm(8);
 for (;;) {
  __asm__ volatile ("wfe");
 }
}
struct span {
 const volatile char *p;
 uint64_t n;
};
static void save_log(const struct span *sp, int nsp)
{
 uint64_t total = 0, room, head, tail, skip_to, at = 0;
 for (int i = 0; i < nsp; i++) {
  total += sp[i].n;
 }
 room = (0x40000u - sizeof(struct ramoops_hdr)) - ((volatile struct ramoops_hdr *)0x4811f000ULL)->size;
 room = room > 1024 ? room - 1024 : 0;
 head = total <= room ? total : room / 4;
 tail = total <= room ? 0 : room - head;
 skip_to = total - tail;
 uint64_t fb = fbc.base;
 fbc.base = 0;
 for (int i = 0; i < nsp; i++) {
  for (uint64_t k = 0; k < sp[i].n; k++, at++) {
   if (at < head || at >= skip_to) {
    char c = sp[i].p[k];
    plog_putc(c ? c : ' ');
   } else if (at == head) {
    dbg_puts("\n\n[... ");
    dbg_dec(skip_to - head);
    dbg_puts(" bytes of the XNU log left out here ...]\n\n");
   }
  }
 }
 plog_putc('\n');
 fbc.base = fb;
}
static void save_host_keep(const struct xnu_syms *s)
{
 static const char head[] = "=== kept for the host by the kernel: ";
 volatile struct ramoops_hdr *h = (volatile struct ramoops_hdr *)0x48160000ULL;
 volatile char *d = (volatile char *)(0x48160000ULL + sizeof(struct ramoops_hdr));
 uint32_t cap = 0x10000u - sizeof(struct ramoops_hdr), at = 0, len, dropped;
 char num[12];
 int n;
 uint64_t keep_va = s->host_keep ? s->host_keep : s->hk_keep;
 uint64_t len_va = s->host_keep ? s->host_keep_len : s->hk_keep_len;
 uint64_t dropped_va = s->host_keep ? s->host_keep_dropped : s->hk_keep_dropped;
 if (!keep_va || !len_va) {
  return;
 }
 len = rd32(kva_to_pa(len_va));
 dropped = dropped_va ? rd32(kva_to_pa(dropped_va)) : 0;
 for (const char *c = head; *c; c++) {
  d[at++] = *c;
 }
 for (int part = 0; part < 2; part++) {
  uint32_t v = part ? dropped : len;
  n = 0;
  do {
   num[n++] = (char)('0' + v % 10);
   v /= 10;
  } while (v);
  while (n) {
   d[at++] = num[--n];
  }
  const char *tail = part ? " dropped) ===\n" : " bytes (";
  for (const char *c = tail; *c; c++) {
   d[at++] = *c;
  }
 }
 const volatile char *keep = (const volatile char *)kva_to_pa(keep_va);
 for (uint32_t i = 0; i < len && at < cap; i++) {
  d[at++] = keep[i];
 }
 h->sig = 0x43474244u;
 h->start = at;
 h->size = at;
 dcache_clean_range(0x48160000ULL, 0x10000u);
 out("host_keep: ");
 out_dec(len);
 out(" bytes into pmsg, ");
 out_dec(dropped);
 out(" lines dropped\n");
}
static void show_log_tail(uint32_t lines)
{
 uint32_t size = ((volatile struct ramoops_hdr *)0x4811f000ULL)->size, start = ((volatile struct ramoops_hdr *)0x4811f000ULL)->start, n = 0;
 uint32_t top = 2460u - (lines + 1) * 16;
 uint64_t i;
 if (!fbc.base || size == 0 || size != start) {
  return;
 }
 for (i = size; i > 0 && n <= lines; i--) {
  n += ((volatile char *)(0x4811f000ULL + sizeof(struct ramoops_hdr)))[i - 1] == '\n';
 }
 fb_rect(0, top - 4, 1080u, 2460u - top + 4, 0xff000000u);
 fb_rect(0, top - 4, 1080u, 2, 0xffff3030u);
 fbc.top = fbc.y = top;
 fbc.x = 0;
 fbc.scale = 1;
 for (; i < size; i++) {
  fbcon_putc(((volatile char *)(0x4811f000ULL + sizeof(struct ramoops_hdr)))[i]);
 }
}
static int report_park = 1;
static int logbook_kernel_saved;
struct logbook_hdr {
 uint32_t magic;
 uint32_t version;
 uint64_t len;
 uint64_t reason;
 uint64_t code;
 uint64_t elr;
 uint64_t far;
 uint64_t ticks;
 uint64_t where;
 uint64_t sum;
};
static uint64_t logbook_offset(void)
{
 uint64_t lba, blocks, size;
 if (!gpt_find("xnusys", &lba, &blocks)) {
  return 0;
 }
 size = blocks * (uint64_t)gpt_block_size;
 if (size < (uint64_t)(8u << 20) + 65536ull) {
  return 0;
 }
 return lba * (uint64_t)gpt_block_size + size - (uint64_t)(8u << 20);
}
static int save_log_spans_to_flash(const struct span *sp, int nsp, uint64_t reason,
      uint64_t code, uint64_t elr, uint64_t far)
{
 static union {
  struct logbook_hdr h;
  uint8_t block[4096];
 } record __attribute__((aligned(64)));
 static uint8_t last[4096] __attribute__((aligned(64)));
 struct logbook_hdr *h = &record.h;
 uint64_t at = logbook_offset();
 uint64_t total = 0, len, skip, written = 0, sum = 0;
 uint32_t used = 0;
 if (at == 0 || nsp <= 0) {
  return 0;
 }
 for (int i = 0; i < nsp; i++) {
  if ((!sp[i].p && sp[i].n) || sp[i].n > ~0ull - total) {
   return 0;
  }
  total += sp[i].n;
 }
 len = total > (uint64_t)(4u << 20) ? (uint64_t)(4u << 20) : total;
 if (len == 0) {
  return 0;
 }
 skip = total - len;
 for (int i = 0; i < nsp; i++) {
  const volatile uint8_t *p = (const volatile uint8_t *)sp[i].p;
  uint64_t n = sp[i].n;
  if (skip >= n) {
   skip -= n;
   continue;
  }
  p += skip;
  n -= skip;
  skip = 0;
  while (n) {
   if (used == 0 && n >= 4096u) {
    uint64_t whole = n & ~4095ull;
    for (uint64_t k = 0; k < whole; k++) {
     sum += p[k];
    }
    if (!ufs_write((at + 4096ull + written) / 512ull,
      (uint32_t)(whole / 512ull), (const void *)p)) {
     goto body_failed;
    }
    written += whole;
    p += whole;
    n -= whole;
   } else {
    uint32_t take = n < 4096u - used ? (uint32_t)n : 4096u - used;
    for (uint32_t k = 0; k < take; k++) {
     uint8_t v = p[k];
     last[used++] = v;
     sum += v;
    }
    p += take;
    n -= take;
    if (used == 4096u) {
     if (!ufs_write((at + 4096ull + written) / 512ull, 8u, last)) {
      goto body_failed;
     }
     written += 4096u;
     used = 0;
    }
   }
  }
 }
 if (used) {
  for (uint32_t i = used; i < 4096u; i++) {
   last[i] = 0;
  }
  if (!ufs_write((at + 4096ull + written) / 512ull, 8u, last)) {
   goto body_failed;
  }
 }
 for (uint32_t i = 0; i < sizeof(record.block); i++) {
  record.block[i] = 0;
 }
 h->magic = 0x474f4c48u;
 h->version = 1;
 h->len = len;
 h->reason = reason;
 h->code = code;
 h->elr = elr;
 h->far = far;
 h->ticks = dbg_ticks();
 h->where = at + 4096ull;
 h->sum = sum;
 if (!ufs_write(at / 512ull, 8u, record.block)) {
  out("logbook: header write failed: ");
  out(board_storage_what());
  out("\n");
  return 0;
 }
 out("logbook: ");
 out_dec(len / 1024u);
 out(" KB at byte ");
 out_hex(at);
 out(" of " "xnusys" "\n");
 return 1;
body_failed:
 out("logbook: body write failed: ");
 out(board_storage_what());
 out("\n");
 return 0;
}
static int save_log_to_flash(uint64_t pa, uint64_t len, uint64_t reason, uint64_t code,
        uint64_t elr, uint64_t far)
{
 const struct span sp = { (const volatile char *)(uintptr_t)pa, len };
 return save_log_spans_to_flash(&sp, 1, reason, code, elr, far);
}
static uint32_t cpu_mhz(void);
static void logbook_boot_note(void)
{
 static char buf[768];
 uint32_t n = 0;
 const char *text = "mtkloader: about to enter XNU\nboot args: " "rd=md0" " -v " "cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=0 mali_trial=715 headset=0 mtk_ufs_probe=1" " loader_return_after=" "150" "\n";
 const uint64_t pll[4] = { 0x20c, 0x21c, 0x22c, 0x23c };
 const char *names[4] = { "LL(A55)", "BL(A78)", "B(A78)", "DSU" };
 while (*text && n < sizeof(buf) - 1u) {
  buf[n++] = *text++;
 }
 {
  uint32_t mhz = cpu_mhz();
  text = "clock: this core measures ";
  while (*text && n < sizeof(buf) - 1u) {
   buf[n++] = *text++;
  }
  {
   char digits[12];
   uint32_t d = 0, v = mhz;
   if (v == 0) {
    digits[d++] = '0';
   }
   while (v != 0) {
    digits[d++] = (char)('0' + (v % 10u));
    v /= 10u;
   }
   while (d != 0) {
    buf[n++] = digits[--d];
   }
  }
  text = " MHz\n";
  while (*text && n < sizeof(buf) - 1u) {
   buf[n++] = *text++;
  }
 }
 for (uint32_t i = 0; i < 4; i++) {
  uint32_t con = rd32(0x1000c000ULL + pll[i]);
  uint32_t post = (con >> 24) & 7u;
  uint32_t pcw = con & 0x3fffffu;
  uint32_t div = rd32(0x0c530000ull + 0xa2a0u + i * 4u) & 0x1fu;
  uint64_t mhz = post ? (((26ull * pcw) >> 14) >> post) : 0;
  text = "clock: ";
  while (*text && n < sizeof(buf) - 1u) {
   buf[n++] = *text++;
  }
  for (const char *w = names[i]; *w && n < sizeof(buf) - 1u; w++) {
   buf[n++] = *w;
  }
  {
   const char *w = " con ";
   while (*w && n < sizeof(buf) - 1u) {
    buf[n++] = *w++;
   }
  }
  {
   const char *hex = "0123456789abcdef";
   for (int sh = 28; sh >= 0; sh -= 4) {
    buf[n++] = hex[(con >> sh) & 0xfu];
   }
  }
  {
   char digits[12];
   uint32_t d = 0, v = (uint32_t)mhz;
   text = " pll ";
   while (*text && n < sizeof(buf) - 1u) {
    buf[n++] = *text++;
   }
   if (v == 0) {
    digits[d++] = '0';
   }
   while (v != 0) {
    digits[d++] = (char)('0' + (v % 10u));
    v /= 10u;
   }
   while (d != 0) {
    buf[n++] = digits[--d];
   }
   text = " MHz div ";
   while (*text && n < sizeof(buf) - 1u) {
    buf[n++] = *text++;
   }
   d = 0;
   v = div;
   if (v == 0) {
    digits[d++] = '0';
   }
   while (v != 0) {
    digits[d++] = (char)('0' + (v % 10u));
    v /= 10u;
   }
   while (d != 0) {
    buf[n++] = digits[--d];
   }
   buf[n++] = '\n';
  }
 }
 buf[n] = 0;
 (void)save_log_to_flash((uint64_t)(uintptr_t)buf, n, 0xfeedfaceull, 0, 0, 0);
}
static void report_page_clear(void)
{
 fb_rect(0, 0, 1080u, 2460u, 0xff000000u);
}
static void report_kernel_log(uint64_t pa, uint64_t len)
{
 const volatile char *p = (const volatile char *)pa;
 uint32_t scale = 3;
 uint32_t cols = 1080u / (8u * scale);
 uint32_t rows = 2460u / (16u * scale);
 uint32_t lh = 16u * scale, cw = 8u * scale;
 uint32_t col = 0, row = 0, page = 1;
 uint64_t from;
 if (!pa || len == 0) {
  return;
 }
 return;
 if (len > (uint64_t)6 * 1024u) {
  from = len - (uint64_t)6 * 1024u;
 } else {
  from = 0;
 }
 report_page_clear();
 fbc.top = 0;
 fbc.x = 0;
 fbc.y = 0;
 fbc.scale = scale;
 fbc.fg = 0xffffffffu;
 fbc.bg = 0xff000000u;
 for (uint64_t i = from; i < len; i++) {
  char c = p[i];
  if (c == '\r') {
   continue;
  }
  if (c == '\n') {
   col = 0;
   row++;
  } else {
   if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) {
    c = '?';
   }
   fbc.x = col * cw;
   fbc.y = row * lh;
   fbcon_putc(c);
   if (++col >= cols) {
    col = 0;
    row++;
   }
  }
  if (row >= rows) {
   dbg_delay_ms(20000u);
   report_page_clear();
   fbc.scale = scale;
   fbc.x = 0;
   fbc.y = 0;
   row = 0;
   col = 0;
   page++;
  }
 }
 (void)page;
 if (report_park) {
  for (;;) {
   dbg_delay_ms(1000);
  }
 }
 dbg_delay_ms(20000u);
}
static int snapshot_take(void)
{
 struct snap_hdr *h = (struct snap_hdr *)0x90000100ULL;
 uint64_t len = h->len;
 char *text = (char *)(0x90000100ULL + sizeof(struct snap_hdr));
 if (h->magic != 0x50414e53u || len == 0 || len > 0x00100000u - sizeof(struct snap_hdr)) {
  return 0;
 }
 dcache_clean_range(0x90000100ULL, (uint64_t)len + sizeof(struct snap_hdr) + 64u);
 if (len > (uint64_t)(4u << 20)) {
  text += len - (uint64_t)(4u << 20);
  len = (uint64_t)(4u << 20);
 }
 out("\n=== the watcher left a record in DRAM (");
 out_dec(len);
 out(" bytes) ===\n");
 if (!save_log_to_flash((uint64_t)(uintptr_t)text, len, 2u , h->ticks, 0, 0)) {
  report_kernel_log((uint64_t)(uintptr_t)text, len);
  return 1;
 }
 logbook_kernel_saved = 1;
 h->magic = 0;
 dcache_clean_range(0x90000100ULL, 64);
 report_kernel_log((uint64_t)(uintptr_t)text, len);
 return 1;
}
static uint64_t xnu_va_to_pa(uint64_t va)
{
 uint64_t tcr = xnu_fp[2], ttbr1 = xnu_fp[3];
 uint32_t t1sz = (tcr >> 16) & 0x3f, tg1 = (tcr >> 30) & 3;
 uint32_t g = tg1 == 2 ? 12 : tg1 == 1 ? 14 : 0;
 if (!g || t1sz < 16) {
  return 0;
 }
 uint32_t vabits = 64 - t1sz, per = g - 3;
 uint32_t levels = (vabits - g + per - 1) / per;
 uint64_t table = ttbr1 & 0x0000fffffffffffeULL & ~((1ULL << g) - 1);
 for (uint32_t l = 0; l < levels; l++) {
  uint32_t shift = g + per * (levels - 1 - l);
  uint64_t idx = (va >> shift) & ((1ULL << per) - 1);
  if (l == 0) {
   idx &= (1ULL << (vabits - shift)) - 1;
  }
  uint64_t d = *(volatile uint64_t *)(table + idx * 8);
  if (!(d & 1)) {
   return 0;
  }
  uint64_t oa = d & 0x0000ffffffffffffULL & ~((1ULL << g) - 1);
  if (l == levels - 1 || !(d & 2)) {
   uint64_t span = 1ULL << shift;
   return (oa & ~(span - 1)) | (va & (span - 1));
  }
  table = oa;
 }
 return 0;
}
static uint64_t xnu_rd64(uint64_t va)
{
 uint64_t pa = xnu_va_to_pa(va);
 return pa && !(pa & 7) ? *(volatile uint64_t *)pa : 0;
}
static char xnu_rd8(uint64_t va)
{
 uint64_t pa = xnu_va_to_pa(va);
 return pa ? *(volatile char *)pa : 0;
}
static void out_xnu_str(uint64_t va, int max)
{
 char c;
 for (int i = 0; i < max && (c = xnu_rd8(va + i)) != 0; i++) {
  out_putc(c);
 }
}
struct xnu_va_list {
 uint64_t stack, gr_top, vr_top;
 int32_t gr_offs, vr_offs;
};
static uint64_t va_next(struct xnu_va_list *ap)
{
 uint64_t v;
 if (ap->gr_offs < 0) {
  v = xnu_rd64(ap->gr_top + (int64_t)ap->gr_offs);
  ap->gr_offs += 8;
 } else {
  v = xnu_rd64(ap->stack);
  ap->stack += 8;
 }
 return v;
}
static void show_panic_string(const struct xnu_syms *x)
{
 uint64_t fmt = x->panic_str ? xnu_rd64(x->panic_str) : 0;
 uint64_t argp = x->panic_args ? xnu_rd64(x->panic_args) : 0;
 struct xnu_va_list ap = { 0 };
 if (!fmt) {
  out("(no panic string)\n");
  return;
 }
 if (argp) {
  ap.stack = xnu_rd64(argp);
  ap.gr_top = xnu_rd64(argp + 8);
  ap.vr_top = xnu_rd64(argp + 16);
  uint64_t offs = xnu_rd64(argp + 24);
  ap.gr_offs = (int32_t)offs;
  ap.vr_offs = (int32_t)(offs >> 32);
 }
 out("panic: ");
 for (int i = 0; i < 1024; i++) {
  char c = xnu_rd8(fmt + i);
  if (!c) {
   break;
  }
  if (c != '%') {
   out_putc(c);
   continue;
  }
  do {
   c = xnu_rd8(fmt + ++i);
  } while (c && ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == ' ' || c == '#' || c == '.' ||
      c == 'l' || c == 'h' || c == 'z' || c == 'j' || c == 't' || c == 'q' || c == '*'));
  if (!argp && c != '%') {
   out("<?>");
   continue;
  }
  switch (c) {
  case '%':
   out_putc('%');
   break;
  case 's':
   out_xnu_str(va_next(&ap), 200);
   break;
  case 'c':
   out_putc((char)va_next(&ap));
   break;
  case 'd':
  case 'i':
  case 'u': {
   uint64_t v = va_next(&ap);
   out_dec(v);
   out("(");
   out_hex(v);
   out(")");
   break;
  }
  case 0:
   i = 1024;
   break;
  default:
   out_hex(va_next(&ap));
   break;
  }
 }
 out("\n");
}
static void out_sym(uint64_t addr)
{
 const struct mach_header_64 *mh = (const struct mach_header_64 *)xkernel;
 const struct load_command *lc;
 const struct symtab_command *st = ((void*)0);
 uint64_t best = 0;
 const char *best_name = ((void*)0);
 out_hex(addr);
 if (!xkernel) {
  return;
 }
 lc = (const struct load_command *)(mh + 1);
 for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const void *)((const uint8_t *)lc + lc->cmdsize)) {
  if (lc->cmd == 0x2) {
   st = (const void *)lc;
  }
 }
 if (!st) {
  return;
 }
 const struct nlist_64 *sym = (const struct nlist_64 *)(xkernel + st->symoff);
 const char *strs = (const char *)(xkernel + st->stroff);
 for (uint32_t i = 0; i < st->nsyms; i++) {
  if ((sym[i].n_type & 0x0e) == 0x0e && sym[i].n_value <= addr && sym[i].n_value > best) {
   best = sym[i].n_value;
   best_name = strs + sym[i].n_strx;
  }
 }
 if (best_name && addr - best < 0x100000) {
  out(" ");
  out(best_name);
  out("+");
  out_hex(addr - best);
 }
}
static void show_backtrace(void)
{
 uint64_t fp = xnu_fp[0];
 out("TCR_EL1 ");
 out_hex(xnu_fp[2]);
 out(" TTBR1_EL1 ");
 out_hex(xnu_fp[3]);
 out(" fp ");
 out_hex(fp);
 out(" -> pa ");
 out_hex(xnu_va_to_pa(fp));
 out(" sym debugger_panic_str -> pa ");
 out_hex(xnu_va_to_pa(xsyms.panic_str));
 out(" (slide: ");
 out_hex(kva_to_pa(xsyms.panic_str));
 out(")\nbacktrace:\n  lr ");
 out_sym(xnu_fp[1]);
 out("\n");
 for (int i = 0; i < 24 && fp; i++) {
  uint64_t lr = xnu_rd64(fp + 8);
  if (!lr) {
   break;
  }
  out("  ");
  out_sym(lr);
  out("\n");
  fp = xnu_rd64(fp);
 }
}
static void read_screen(void)
{
 static uint32_t font_hash[256];
 uint64_t font = xsyms.iso_font ? kva_to_pa(xsyms.iso_font) : 0;
 const uint32_t cols = 1080u / 8, rows = 2460u / 16;
 uint32_t lit_cells = 0, known = 0, last_row = 0;
 static char line[1080u / 8 + 1];
 if (!font || !fb_base) {
  return;
 }
 for (int ch = 0; ch < 256; ch++) {
  uint32_t h = 0;
  for (int r = 0; r < 16; r++) {
   h = h * 0x01000193u ^ *(volatile uint8_t *)(font + ch * 16 + r);
  }
  font_hash[ch] = h;
 }
 for (uint32_t row = 0; row < rows; row++) {
  for (uint32_t col = 0; col < cols; col++) {
   uint8_t bits[16];
   uint32_t h = 0, any = 0;
   for (uint32_t r = 0; r < 16; r++) {
    const volatile uint32_t *px = (const volatile uint32_t *)(fb_base + (uint64_t)(row * 16 + r) * fb_pitch) + col * 8;
    uint8_t b = 0;
    for (uint32_t x = 0; x < 8; x++) {
     uint32_t p = px[x];
     uint32_t lum = ((p >> 16) & 0xff) + ((p >> 8) & 0xff) + (p & 0xff);
     if (lum > 3 * 0x60) {
      b |= (uint8_t)(1u << x);
     }
    }
    bits[r] = b;
    any |= b;
   }
   char out_c = ' ';
   if (any) {
    lit_cells++;
    for (int inv = 0; inv < 2 && out_c == ' '; inv++) {
     h = 0;
     for (int r = 0; r < 16; r++) {
      h = h * 0x01000193u ^ (uint8_t)(inv ? ~bits[r] : bits[r]);
     }
     for (int ch = 0x21; ch < 0x7f; ch++) {
      if (font_hash[ch] == h) {
       out_c = (char)ch;
       known++;
       break;
      }
     }
    }
    if (out_c == ' ') {
     out_c = '#';
    }
   }
   line[col] = out_c;
  }
  line[cols] = 0;
  int n = (int)cols;
  while (n > 0 && line[n - 1] == ' ') {
   n--;
  }
  line[n] = 0;
  if (n) {
   last_row = row + 1;
  }
  dbg_puts(line);
  dbg_putc('\n');
 }
 dbg_puts("[screen: ");
 dbg_dec(lit_cells);
 dbg_puts(" cells lit, ");
 dbg_dec(known);
 dbg_puts(" read as characters, last text row ");
 dbg_dec(last_row);
 dbg_puts("]\n");
}
void xnu_returned(uint64_t log_pa, uint64_t len, uint64_t reason, uint64_t code, uint64_t elr, uint64_t far)
{
 struct span sp = { (const volatile char *)log_pa, len };
 wdt_disable();
 xnu_back = 1;
 marker_set(0x4e44584eu);
 uint64_t screen = fbc.base;
 fbc.base = 0;
 dbg_puts("\n=== what the screen showed when XNU came back ===\n");
 read_screen();
 fbc.base = screen;
 fbc.fg = 0xffffff40u;
 out("\n=== XNU returned to mtkloader: reason ");
 out_dec(reason);
 out(reason == 0 ? " (end)" : reason == 1 ? " (early exception)" : reason == 2 ? " (checkpoint)" : "");
 out(" code ");
 out_hex(code);
 if (reason == 1) {
  out(" ELR ");
  out_hex(elr);
  out(" FAR ");
  out_hex(far);
 }
 out("\n");
 if (xsyms.tasks_count) {
  out("tasks ");
  out_dec(rd32(kva_to_pa(xsyms.tasks_count)));
  if (xsyms.nprocs) {
   out(" processes ");
   out_dec(rd32(kva_to_pa(xsyms.nprocs)));
  }
  if (xsyms.initproc) {
   out(" initproc ");
   out_hex(*(volatile uint64_t *)kva_to_pa(xsyms.initproc));
  }
  out("\n");
 }
 if (reason != 1) {
  show_panic_string(&xsyms);
  show_backtrace();
 }
 out("=== XNU log (");
 out_dec(len);
 out(" bytes) ===\n");
 if (len > 4 * 1024 * 1024) {
  sp.n = 0;
 }
 save_log(&sp, 1);
 dbg_puts("=== end of XNU log ===\n");
 save_host_keep(&xsyms);
 if (log_pa && len) {
  logbook_kernel_saved = save_log_to_flash(log_pa, len, reason, code, elr, far);
  (void)0;
 }
 show_log_tail(40);
 dbg_delay_ms(20000);
 out("starting Android, so that the log above can be pulled\n");
 chain_android();
}
static void rescue_state(void)
{
 if (xsyms.cpu_ttep) {
  xnu_fp[3] = rd32(kva_to_pa(xsyms.cpu_ttep)) | (uint64_t)rd32(kva_to_pa(xsyms.cpu_ttep) + 4) << 32;
  xnu_fp[2] = 0x22a51a251aULL;
 }
 out("cpu_ttep ");
 out_hex(xnu_fp[3]);
 if (xsyms.live) {
  out(" heartbeat ");
  out_dec(*(volatile uint64_t *)kva_to_pa(xsyms.live + 8));
 }
 out("\n");
 show_panic_string(&xsyms);
 if (!xsyms.msgbufp || !xnu_fp[3]) {
  return;
 }
 uint64_t mb = xnu_rd64(xsyms.msgbufp);
 uint64_t w0 = xnu_rd64(mb), w1 = xnu_rd64(mb + 8), bufc = xnu_rd64(mb + 16);
 int32_t size = (int32_t)(w0 >> 32), bufx = (int32_t)w1;
 out("msgbuf ");
 out_hex(mb);
 out(" size ");
 out_dec((uint32_t)size);
 out(" bufx ");
 out_dec((uint32_t)bufx);
 out("\n--- kernel message buffer (from DRAM) ---\n");
 if (size <= 0 || size > 1024 * 1024 || bufx < 0 || bufx > size) {
  return;
 }
 uint64_t fbsave = fbc.base;
 fbc.base = 0;
 for (int pass = 0; pass < 2; pass++) {
  int from = pass == 0 ? bufx : 0, to = pass == 0 ? size : bufx;
  for (int i = from; i < to; i++) {
   char c = xnu_rd8(bufc + i);
   if (c) {
    dbg_putc(c);
   }
  }
 }
 fbc.base = fbsave;
 dbg_puts("\n--- end of message buffer ---\n");
}
static void rescue_log(void)
{
 struct payload_info pi;
 struct xnu_syms s;
 out("\n=== mtkloader: XNU did not return before the reset ===\n");
 if (!parse_payload(&pi)) {
  out("no XNU payload\n");
  return;
 }
 find_syms(pi.kernel, &s);
 if (!s.log || !s.log_len || !s.log_ring_at || !s.log_wrapped) {
  out("XNU log symbols not found\n");
  return;
 }
 const uint64_t head_sz = 256 * 1024, ring_sz = 1024 * 1024;
 const volatile char *log = (const volatile char *)kva_to_pa(s.log);
 uint32_t len = rd32(kva_to_pa(s.log_len));
 uint32_t ring_at = rd32(kva_to_pa(s.log_ring_at));
 uint8_t wrapped = *(const volatile uint8_t *)kva_to_pa(s.log_wrapped);
 struct span sp[3];
 int n = 0;
 out("loader_log_len ");
 out_dec(len);
 out(" ring_at ");
 out_dec(ring_at);
 out(" wrapped ");
 out_dec(wrapped);
 out("\n");
 if (ring_at >= ring_sz || wrapped > 1u || (wrapped && len < head_sz)) {
  out("XNU log ring metadata is invalid; not saving it\n");
  return;
 }
 if (!wrapped) {
  if (len > head_sz + ring_sz) {
   len = head_sz + ring_sz;
  }
  sp[n++] = (struct span){ log, len };
 } else {
  sp[n++] = (struct span){ log, head_sz };
  sp[n++] = (struct span){ log + head_sz + ring_at, ring_sz - ring_at };
  sp[n++] = (struct span){ log + head_sz, ring_at };
 }
 logbook_kernel_saved = save_log_spans_to_flash(sp, n, 0x52534355u, 0, 0, 0);
 xsyms = s;
 xkernel = pi.kernel;
 rescue_state();
 dbg_puts("=== XNU log (from DRAM; a few bits may have flipped in the reset) ===\n");
 save_log(sp, n);
 dbg_puts("=== end of XNU log ===\n");
 save_host_keep(&s);
 show_log_tail(40);
}
static void gic_fix_eoimode(void)
{
 uint64_t ctlr;
 __asm__ volatile("mrs %0, ICC_CTLR_EL1" : "=r"(ctlr));
 out("ICC_CTLR_EL1 ");
 out_hex(ctlr);
 out(ctlr & 2 ? " (EOImode set: clearing)" : " (EOImode clear)");
 if (ctlr & 2) {
  __asm__ volatile("msr ICC_CTLR_EL1, %0; isb" : : "r"(ctlr & ~2ULL));
  __asm__ volatile("mrs %0, ICC_CTLR_EL1" : "=r"(ctlr));
  out(" -> ");
  out_hex(ctlr);
 }
 out("\n");
}
static uint64_t watcher_l1[512] __attribute__((aligned(4096)));
static volatile uint64_t watcher_started __attribute__((aligned(64)));
static void plog_flush(void)
{
 dcache_clean_range(0x4811f000ULL, 0x40000u);
 dcache_clean_range(0x90000000ULL, 192);
}
static void snap_begin(void)
{
 struct snap_hdr *h = (struct snap_hdr *)0x90000100ULL;
 h->magic = 0;
 h->version = 1;
 h->len = 0;
 h->ticks = 0;
 dbg_tee = (char *)(0x90000100ULL + sizeof(struct snap_hdr));
 dbg_tee_cap = 0x00100000u - (uint32_t)sizeof(struct snap_hdr) - 1u;
 dbg_tee_at = 0;
}
static void snap_end(void)
{
 struct snap_hdr *h = (struct snap_hdr *)0x90000100ULL;
 h->len = dbg_tee_at;
 h->ticks = dbg_ticks();
 h->magic = 0x50414e53u;
 dbg_tee = 0;
 dcache_clean_range(0x90000100ULL, (uint64_t)dbg_tee_at + sizeof(struct snap_hdr) + 64u);
}
void watcher_main(void)
{
 for (int i = 0; i < 512; i++) {
  uint64_t pa = (uint64_t)i << 30;
  if (i == 0) {
   watcher_l1[i] = pa | (1u << 10) | (0u << 2) | 1;
  } else if (i < 8) {
   watcher_l1[i] = pa | (1u << 10) | (3u << 8) | (1u << 2) | 1;
  } else {
   watcher_l1[i] = 0;
  }
 }
 mmu_on((uint64_t)watcher_l1);
 watcher_started = 1;
 dcache_clean_range((uint64_t)&watcher_started, 8);
 snap_begin();
 fbc.base = 0xfe14e000ULL;
 fbc.pitch = 4352u;
 fbc.w = 1080u;
 fbc.h = 2460u;
 fbc.scale = 2;
 fbc.fg = 0xffffffffu;
 fbc.bg = 0xff000000u;
 fb_base = 0xfe14e000ULL;
 fb_pitch = 4352u;
 uint64_t end = dbg_ticks() + dbg_freq() * 0;
 uint32_t naps = 0 * 10u;
 fb_rect(0, 0, 1080u, 2460u, 0xff00a000u);
 dcache_clean_range(fb_base, (uint64_t)2460u * fb_pitch);
 while (dbg_ticks() < end && naps != 0) {
  dcache_clean_range((uint64_t)&xnu_back, 8);
  if (xnu_back) {
   for (;;) {
    __asm__ volatile("wfe");
   }
  }
  naps--;
  {
   uint32_t done_naps = 0 * 10u - naps;
   uint32_t w = done_naps * 4u;
   if (w > 1080u) {
    w = 1080u;
   }
   if (w != 0u) {
    fb_rect(0, 0, w, 48u, 0xffff0000u);
    fb_rect(0, 48u, w > 8u ? 8u : w, 48u, 0xffffffffu);
    dcache_clean_range(fb_base, 96ull * fb_pitch);
   }
  }
  dbg_delay_ms(100);
 }
 dcache_clean_range((uint64_t)&xnu_back, 8);
 if (xnu_back) {
  for (;;) {
   __asm__ volatile("wfe");
  }
 }
 fb_rect(0, 0, 1080u, 2460u, 0xff00c000u);
 dcache_clean_range(fb_base, (uint64_t)2460u * fb_pitch);
 snap_end();
 marker_set(0x4e52584eu);
 for (;;) {
  __asm__ volatile("wfe");
 }
 marker_set(0x4e44584eu);
 dbg_puts("\n=== watcher: XNU still running after " );
 dbg_dec(0);
 dbg_puts(" s, snapshot ===\n");
 if (xsyms.cpu_ttep) {
  xnu_fp[3] = *(volatile uint64_t *)kva_to_pa(xsyms.cpu_ttep);
  xnu_fp[2] = 0x22a51a251aULL;
 }
 dbg_puts("cpu_ttep ");
 dbg_hex(xnu_fp[3], 10);
 if (xsyms.live) {
  dbg_puts(" heartbeat ");
  dbg_dec(*(volatile uint64_t *)kva_to_pa(xsyms.live + 8));
 }
 if (xsyms.log_len) {
  dbg_puts(" loader_log_len ");
  dbg_dec(rd32(kva_to_pa(xsyms.log_len)));
 }
 {
  const uint64_t win = 0x0e010000ULL;
  uint64_t fb_saved = fbc.base, pmu = 0;
  out("\ncpu_dbgapb components:");
  for (uint64_t off = 0; off < 0x100000; off += 0x10000) {
   uint64_t b = win + off;
   uint32_t cidr1 = rd32(b + 0xff4), devarch = rd32(b + 0xfbc), devtype = rd32(b + 0xfcc);
   out("\n  +");
   out_hex(off);
   out(" CIDR1 ");
   out_hex(cidr1);
   out(" DEVARCH ");
   out_hex(devarch);
   out(" DEVTYPE ");
   out_hex(devtype);
   if ((devarch & 0xffff) == 0x2a16) {
    pmu = b;
   }
  }
  out("\n");
  if (pmu) {
   wr32(pmu + 0xfb0, 0xc5acce55u);
   out("cpu0 PC samples (PMPCSR):\n");
   for (int i = 0; i < 24; i++) {
    uint32_t lo = rd32(pmu + 0x200);
    uint32_t hi = rd32(pmu + 0x204);
    uint64_t pc = ((uint64_t)hi << 32 | lo) & 0x00ffffffffffffffULL;
    if (pc >> 47) {
     pc |= 0xff00000000000000ULL;
    }
    out("  ");
    out_sym(pc);
    out("\n");
    dbg_delay_ms(15);
   }
  }
  fbc.base = fb_saved;
 }
 {
  const uint64_t gicd = 0x0c000000ULL, gicr = 0x0c040000ULL, sgi = gicr + 0x10000;
  dbg_puts("GICD_CTLR ");
  dbg_hex(rd32(gicd + 0x000), 8);
  dbg_puts(" TYPER ");
  dbg_hex(rd32(gicd + 0x004), 8);
  dbg_puts("\nGICR_CTLR ");
  dbg_hex(rd32(gicr + 0x000), 8);
  dbg_puts(" WAKER ");
  dbg_hex(rd32(gicr + 0x014), 8);
  dbg_puts(" TYPER ");
  dbg_hex(rd32(gicr + 0x008), 8);
  dbg_putc(' ');
  dbg_hex(rd32(gicr + 0x00c), 8);
  dbg_puts("\nGICR_SGI: IGROUPR0 ");
  dbg_hex(rd32(sgi + 0x080), 8);
  dbg_puts(" IGRPMODR0 ");
  dbg_hex(rd32(sgi + 0xd00), 8);
  dbg_puts(" ISENABLER0 ");
  dbg_hex(rd32(sgi + 0x100), 8);
  dbg_puts("\n          ISPENDR0 ");
  dbg_hex(rd32(sgi + 0x200), 8);
  dbg_puts(" ISACTIVER0 ");
  dbg_hex(rd32(sgi + 0x300), 8);
  dbg_puts(" IPRIORITY27 ");
  dbg_hex(*(volatile uint8_t *)(sgi + 0x400 + 27), 2);
  dbg_puts(" ICFGR1 ");
  dbg_hex(rd32(sgi + 0xc04), 8);
  dbg_puts("\n");
 }
 if (xsyms.gic_unhandled_count) {
  dbg_puts("kernel: gic_unhandled_count ");
  dbg_dec(*(volatile uint64_t *)kva_to_pa(xsyms.gic_unhandled_count));
  dbg_puts(" last_unhandled ");
  dbg_dec(rd32(kva_to_pa(xsyms.gic_last_unhandled)));
  dbg_puts(" active_iar[0] ");
  dbg_hex(*(volatile uint64_t *)kva_to_pa(xsyms.gic_active_iar), 16);
  dbg_puts("\n");
 }
 dbg_puts("\n=== what the screen shows ===\n");
 read_screen();
 show_panic_string(&xsyms);
 if (xsyms.msgbufp && xnu_fp[3]) {
  uint64_t mb = xnu_rd64(xsyms.msgbufp);
  uint64_t w0 = xnu_rd64(mb), w1 = xnu_rd64(mb + 8), bufc = xnu_rd64(mb + 16);
  int32_t size = (int32_t)(w0 >> 32), bufx = (int32_t)w1;
  dbg_puts("--- kernel message buffer ---\n");
  if (size > 0 && size <= 1024 * 1024 && bufx >= 0 && bufx <= size) {
   for (int pass = 0; pass < 2; pass++) {
    int from = pass == 0 ? bufx : 0, to = pass == 0 ? size : bufx;
    for (int i = from; i < to; i++) {
     char c = xnu_rd8(bufc + i);
     if (c) {
      dbg_putc(c);
     }
    }
   }
  }
  dbg_puts("\n--- end of message buffer ---\n");
 }
 if (xsyms.log && xsyms.log_len) {
  uint32_t len = rd32(kva_to_pa(xsyms.log_len));
  struct span sp = { (const volatile char *)kva_to_pa(xsyms.log), len > 256 * 1024 ? 256 * 1024 : len };
  dbg_puts("--- loader_log (head) ---\n");
  save_log(&sp, 1);
 }
 save_host_keep(&xsyms);
 dbg_puts("=== end of watcher snapshot ===\n");
 plog_flush();
 snap_end();
 psci_system_reset();
 dbg_puts("=== the PSCI reset did not take; trying the watchdog ===\n");
 wdt_reset();
 dbg_puts("=== the watchdog reset did not take either; power the phone off ===\n");
 for (;;) {
  __asm__ volatile("wfe");
 }
}
static void watcher_start(void)
{
 if (!0) {
  return;
 }
 watcher_started = 0;
 int64_t r = psci_cpu_on(((uint32_t)((core_order[4]) << 8)), (uint64_t)watcher_entry, 0);
 out("watcher on cpu");
 out_dec(core_order[4]);
 out(": PSCI CPU_ON ");
 out_dec((uint64_t)r);
 for (int i = 0; i < 100 && !watcher_started; i++) {
  dbg_delay_ms(10);
 }
 out(watcher_started ? ", running\n" : ", not seen\n");
}
static uint32_t cpu_mhz(void)
{
 uint64_t pmcr, cycles_before, cycles_after, ticks_before, ticks_after, hz = dbg_freq();
 __asm__ volatile("mrs %0, pmcr_el0" : "=r"(pmcr));
 __asm__ volatile("msr pmcr_el0, %0" : : "r"((pmcr | 5u) & ~2u));
 __asm__ volatile("msr pmcntenset_el0, %0" : : "r"(1ull << 31));
 __asm__ volatile("isb");
 ticks_before = dbg_ticks();
 __asm__ volatile("mrs %0, pmccntr_el0" : "=r"(cycles_before));
 dbg_delay_ms(20);
 __asm__ volatile("mrs %0, pmccntr_el0" : "=r"(cycles_after));
 ticks_after = dbg_ticks();
 if (cycles_after <= cycles_before || ticks_after <= ticks_before) {
  return 0;
 }
 return (uint32_t)(((cycles_after - cycles_before) * hz) /
     ((ticks_after - ticks_before) * 1000000ull));
}
static uint32_t touch_clk_cfg2;
static int touch_clk_cfg2_seen;
static uint8_t touch_bring_up_once(void)
{
 static const uint32_t gate_clr[4] = { 0x8c, 0xa8, 0xc4, 0x84 };
 uint8_t buf[64], code = 0;
 uint32_t got = 0;
 uint8_t *stage = (uint8_t *)0x50000000ULL;
 for (int i = 0; i < 4; i++) {
  wr32(0x10001000ULL + gate_clr[i], 0xffffffffu);
 }
 __asm__ volatile("dsb sy");
 touch_clk_cfg2 = rd32(0x10000000ULL + 0x60);
 touch_clk_cfg2_seen = 1;
 spi_pins_to_spi2();
 spi_setup();
 tcm_reset();
 if (tcm_read_patiently(&code, buf, sizeof(buf), &got) != 1 || got < 2) {
  out("touch: the controller said nothing after a reset (code ");
  out_hex(code);
  out(")\n");
  return 0;
 }
 out("touch: after a reset, code ");
 out_hex(code);
 out(", mode ");
 out_hex(buf[1]);
 out("\n");
 if (buf[1] != 0x04) {
  out("touch: not in its ROM bootloader (mode ");
  out_hex(buf[1]);
  out("), leaving it alone\n");
  return buf[1];
 }
 memcpy(stage + 3 + 14, touch_fw, 0u);
 tcm_load_firmware(stage);
 if (tcm_read_patiently(&code, buf, sizeof(buf), &got) != 1 || code != 0x01) {
  out("touch: the image was not taken (");
  out_hex(code);
  out(")\n");
  return 0;
 }
 out("touch: the image was taken\n");
 tcm_run_firmware();
 for (uint32_t waited = 0; waited < 4000; waited += 10) {
  if (!tcm_irq()) {
   dbg_delay_ms(10);
   continue;
  }
  if (tcm_read(&code, buf, sizeof(buf), &got) != 1) {
   dbg_delay_ms(10);
   waited += 10;
   continue;
  }
  out("touch: it said code ");
  out_hex(code);
  out(" after ");
  out_hex(waited);
  out(" ms\n");
  if (code == 0x10 && got >= 2) {
   out("touch: ");
   for (uint32_t i = 2; i < got && i < 18; i++) {
    out_putc(buf[i] >= 0x20 && buf[i] < 0x7f ? (char)buf[i] : ' ');
   }
   out(" is up, mode ");
   out_hex(buf[1]);
   out("\n");
   return buf[1];
  }
 }
 out("touch: it never said it was up\n");
 return 0;
}
static uint8_t __attribute__((unused)) touch_bring_up(void)
{
 for (int attempt = 1; attempt <= 6; attempt++) {
  uint8_t mode = touch_bring_up_once();
  if (mode == 0x01 || mode == 0x02) {
   return mode;
  }
  out("touch: attempt ");
  out_dec((uint32_t)attempt);
  out(" failed, starting over\n");
  dbg_delay_ms(100 * (uint32_t)attempt);
 }
 return 0;
}
static void __attribute__((unused)) big_cores_clock(void)
{
 uint32_t before = rd32(0x1000c000ULL + 0x20c), post = (before >> 24) & 7u;
 uint32_t pcw = (uint32_t)(((uint64_t)1176u << post) * 16384u / 26u);
 if (post != 1 || (before & 0x3fffffu) >= pcw) {
  return;
 }
 wr32(0x1000c000ULL + 0x20c, (1u << 31) | (post << 24) | pcw);
 __asm__ volatile("dsb sy");
 dbg_delay_us(20);
 out("big cores: ");
 out_dec((uint32_t)(((26ull * (before & 0x3fffffu)) >> 14) >> post));
 out(" -> ");
 out_dec((uint32_t)(((26ull * (rd32(0x1000c000ULL + 0x20c) & 0x3fffffu)) >> 14) >> post));
 out(" MHz at the same 800 mV\n");
}
static void __attribute__((noreturn)) run_xnu(void)
{
 struct payload_info pi;
 struct xnu_syms s;
 {
  int payload_ok = parse_payload(&pi);
  if (!payload_ok) {
  out("ERROR: no XNU payload in this image\n");
  dbg_delay_ms(5000);
  chain_android();
  }
 }
 out("XNU kernel ");
 out_dec(pi.kernel_size);
 out(" bytes, ramdisk ");
 out_dec(pi.ramdisk_size);
 out(" bytes\n");
 do { } while (0);
 const struct mach_header_64 *mh = (const struct mach_header_64 *)pi.kernel;
 uint64_t vm_hi = 0, entry_va = 0;
 const struct load_command *lc = (const struct load_command *)(mh + 1);
 for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const void *)((const uint8_t *)lc + lc->cmdsize)) {
  if (lc->cmd == 0x19) {
   const struct segment_command_64 *sg = (const void *)lc;
   if (sg->vmsize == 0) {
    continue;
   }
   if (sg->vmaddr + sg->vmsize > vm_hi) {
    vm_hi = sg->vmaddr + sg->vmsize;
   }
   uint64_t dst_pa = kva_to_pa(sg->vmaddr);
   memset((void *)dst_pa, 0, sg->vmsize);
   memcpy((void *)dst_pa, pi.kernel + sg->fileoff, sg->filesize);
  } else if (lc->cmd == 0x5) {
   entry_va = ((const uint64_t *)((const uint8_t *)lc + 16))[32];
  }
 }
 out("kernel at ");
 out_hex(kva_to_pa(0xfffffff007004000ULL));
 out(", entry ");
 out_hex(kva_to_pa(entry_va));
 out("\n");
 do { } while (0);
 find_syms(pi.kernel, &s);
 xsyms = s;
 xkernel = pi.kernel;
 do { } while (0);
 if (s.vc_colors) {
  volatile uint32_t *c = (volatile uint32_t *)kva_to_pa(s.vc_colors);
  for (int i = 0; i < 8; i++) {
   c[i * 4 + 2] |= 0xff000000u;
  }
  out("vc_colors: alpha set\n");
 }
 if (s.uart_tx_ready && s.uart_tx_data) {
  volatile uint32_t *r = (volatile uint32_t *)kva_to_pa(s.uart_tx_ready);
  volatile uint32_t *d = (volatile uint32_t *)kva_to_pa(s.uart_tx_data);
  r[0] = 0x52800020u;
  r[1] = 0xd65f03c0u;
  d[0] = 0xd65f03c0u;
  if (s.log_putc) {
   int64_t rel = (int64_t)(s.log_putc - s.uart_tx_data) >> 2;
   if (rel >= -(1 << 25) && rel < (1 << 25)) {
    d[0] = 0x14000000u | ((uint32_t)rel & 0x03ffffffu);
   }
  }
  if (s.uart_rx_ready) {
   volatile uint32_t *rx = (volatile uint32_t *)kva_to_pa(s.uart_rx_ready);
   rx[0] = 0x52800000u;
   rx[1] = 0xd65f03c0u;
  }
  out("vmapple_uart: transmit_ready patched, transmit_data -> loader_log_putc, receive off\n");
 }
 if (s.log_putc) {
  const uint64_t from[2] = { 0, 0 };
  for (int i = 0; i < 2; i++) {
   if (!from[i]) {
    continue;
   }
   volatile uint32_t *at = (volatile uint32_t *)kva_to_pa(from[i]);
   int64_t rel = (int64_t)(s.log_putc - from[i]) >> 2;
   if (rel >= -(1 << 25) && rel < (1 << 25)) {
    at[0] = 0x14000000u | ((uint32_t)rel & 0x03ffffffu);
   }
  }
  out("uart_putc and uart_putc_options -> loader_log_putc\n");
 }
 if (s.gic_cpu_init) {
  volatile uint32_t *at = (volatile uint32_t *)kva_to_pa(s.gic_cpu_init);
  int done = 0;
  for (int i = 0; i < 256 && !done; i++) {
   if (at[i] == 0xd518cc7fu) {
    at[i] = 0xd518cc9fu;
    done = 1;
   }
  }
  out(done ? "gic_v3_irq_cpu_init: every core clears EOImode\n" :
      "gic_v3_irq_cpu_init: the BPR1 write is not where it was -- EOImode stays set\n");
 }
 if (s.stub_slot) {
  volatile uint64_t *slot = (volatile uint64_t *)kva_to_pa(s.stub_slot);
  if (slot[0] == 0x545352444c554e58ULL) {
   slot[1] = (uint64_t)xnu_return_stub;
   slot[2] = 0;
   slot[3] = (0x100000000ULL - 0xfffffff004000000ULL);
   out("return stub at ");
   out_hex(slot[1]);
   out("\n");
  }
 }
 uint64_t kern_hi = (kva_to_pa(vm_hi) + 0x4000ULL - 1) & ~(0x4000ULL - 1);
 uint64_t rd_pa = kern_hi;
 uint64_t rd_size = (pi.ramdisk_size + 0x4000ULL - 1) & ~(0x4000ULL - 1);
 if (rd_size) {
  memset((void *)rd_pa, 0, rd_size);
  memcpy((void *)rd_pa, pi.ramdisk, pi.ramdisk_size);
 }
 uint64_t sys_size = (0 || !1) ? 0 : load_system_image();
 uint64_t sys_pa = sys_size ? (0x100000000ULL + 0x08000000ULL) : 0;
 uint64_t sys_span = (sys_size + 0x4000ULL - 1) & ~(0x4000ULL - 1);
 {
  fb_rect(0, 0, 1080u, 2460u, 0xff000000u);
  fbc.x = 0;
  fbc.y = fbc.top = 0;
  fbc.scale = 4;
  fbc.fg = 0xffffffffu;
  fbc.bg = 0xff000000u;
  out("SYSTEM IMAGE 86 full hs\n");
  out("found "); out_dec(sysimg_found); out("\n");
  out("bytes "); out_dec(sysimg_bytes); out("\n");
  out("kb    "); out_dec(sysimg_bytes / 1024u); out("\n");
  out("secs  "); out_dec(sysimg_secs); out("\n");
  out("io    "); out_dec(ufs_io_stage);
  out(" fdev "); out_dec(ufs_io_fdev);
  out(" fst "); out_dec(ufs_fdevinit_stage);
  out(" lun "); out_dec(ufs_lun);
  out(" found "); out_dec(ufs_lun_found); out("\n");
  out("what  "); out(board_storage_what());
  out(" 0x"); out_hex(board_storage_status()); out("\n");
  out("md0   ");
  out(sys_size ? "system.apfs" : "root.apfs (small!)");
  out("\n");
  out("hs    ");
  out_dec(ufs_hs_stage); out(" ok "); out_dec(ufs_hs_ok);
  out(" upm "); out_dec(ufs_hs_upmcrs);
  out(" rx "); out_dec(ufs_hs_gear_rx); out("/"); out_dec(ufs_hs_lane_rx);
  out(" tx "); out_dec(ufs_hs_gear_tx); out("/"); out_dec(ufs_hs_lane_tx);
  out("\n");
  dbg_delay_ms(60000);
 }
 uint64_t dt_pa = sys_size ? sys_pa + sys_span : rd_pa + rd_size;
 uint32_t dt_len = build_device_tree(dt_pa, rd_pa, rd_size, sys_pa, sys_span);
 do { } while (0);
 uint64_t ba_pa = (dt_pa + dt_len + 0x4000ULL - 1) & ~(0x4000ULL - 1);
 uint64_t top = ba_pa + 0x4000ULL;
 struct boot_args *ba = (struct boot_args *)ba_pa;
 memset(ba, 0, sizeof(*ba));
 ba->revision = 2;
 ba->version = 2;
 ba->physBase = 0x100000000ULL;
 ba->virtBase = 0xfffffff004000000ULL;
 ba->memSize = 0x100000000ULL;
 ba->memSizeActual = 0x100000000ULL;
 ba->topOfKernelData = top;
 ba->deviceTreeP = dt_pa - (0x100000000ULL - 0xfffffff004000000ULL);
 ba->deviceTreeLength = dt_len;
 ba->video.v_baseAddr = fb_base;
 ba->video.v_display = 0;
 ba->video.v_rowBytes = fb_pitch;
 ba->video.v_width = 1080u;
 ba->video.v_height = 2460u;
 ba->video.v_depth = 32;
 memcpy(ba->commandLine, "rd=md0" " -v " "cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=0 mali_trial=715 headset=0 mtk_ufs_probe=1" " loader_return_after=" "150", sizeof("rd=md0" " -v " "cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=0 mali_trial=715 headset=0 mtk_ufs_probe=1" " loader_return_after=" "150"));
 if (pi.kernel_version != ((void*)0)) {
  static const char prefix[] = " hakios_kver=";
  uint32_t n = sizeof("rd=md0" " -v " "cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=0 mali_trial=715 headset=0 mtk_ufs_probe=1" " loader_return_after=" "150") - 1, len = 0;
  while (pi.kernel_version[len] != 0 && pi.kernel_version[len] != ' ' && len < 128) {
   len++;
  }
  if (n + sizeof(prefix) - 1 + len < 608) {
   memcpy(ba->commandLine + n, prefix, sizeof(prefix) - 1);
   memcpy(ba->commandLine + n + sizeof(prefix) - 1, pi.kernel_version, len);
   ba->commandLine[n + sizeof(prefix) - 1 + len] = 0;
   out("kernel.ver=");
   for (uint32_t i = 0; i < len; i++) {
    out_putc(pi.kernel_version[i]);
   }
   out("\n");
  }
 }
 out("md0 ");
 out_hex(sys_size ? sys_pa : rd_pa);
 out(sys_size ? " (the system image), md1 " : 0 ? " is none: md1 (root.packfs), the system image on the eMMC" :
     " (root.packfs only)");
 if (sys_size) {
  out_hex(rd_pa);
 }
 out("\nboot-args: " "rd=md0" " -v " "cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=0 mali_trial=715 headset=0 mtk_ufs_probe=1" " loader_return_after=" "150" "\nframebuffer ");
 out_hex(fb_base);
 out(" pitch ");
 out_dec(fb_pitch);
 out(", XNU memory ");
 out_hex(0x100000000ULL);
 out("+");
 out_hex(0x100000000ULL);
 out("\n");
 display_alpha_off();
 do { } while (0);
 marker_set(0x4e52584eu);
 if (0) {
  uint32_t t = 0 > 31 ? 31 : 0;
  wr32((0x1c007000ULL + 0x04), ((t * 32768u / 512u) << 5) | 0x8u);
  wr32((0x1c007000ULL + 0x08), 0x1971u);
  wr32((0x1c007000ULL + 0x00), 0x22000000u | 0x1u);
  out("watchdog: back to Android in ");
  out_dec(t);
  out(" s\n");
 }
 {
  uint64_t pct, vct, frq, kctl, pfr0;
  __asm__ volatile("isb; mrs %0, cntpct_el0; mrs %1, cntvct_el0; mrs %2, cntfrq_el0;"
      "mrs %3, cntkctl_el1; mrs %4, id_aa64pfr0_el1"
      : "=r"(pct), "=r"(vct), "=r"(frq), "=r"(kctl), "=r"(pfr0));
  out("timers: CNTPCT ");
  out_hex(pct);
  out(" CNTVCT ");
  out_hex(vct);
  out(" delta ");
  out_hex(pct - vct);
  out(" CNTFRQ ");
  out_dec(frq);
  out(" CNTKCTL ");
  out_hex(kctl);
  out(" EL2 ");
  out_dec((pfr0 >> 8) & 0xf);
  out("\n");
 }
 {
  uint32_t mhz = cpu_mhz();
  out("this core is running at ");
  if (mhz) {
   out_dec(mhz);
   out(" MHz\n");
  } else {
   out("a speed its cycle counter will not say\n");
  }
 }
 gic_fix_eoimode();
 watcher_start();
 fb_rect(0, 0, 1080u, 2460u, 0xff000000u);
 for (uint64_t at = fb_base; at < fb_base + (uint64_t)2460u * fb_pitch; at += 64) {
  __asm__ volatile("dc cvac, %0" : : "r"(at));
 }
 __asm__ volatile("dsb sy");
 do { } while (0);
 logbook_boot_note();
 out("Jumping to XNU...\n");
 jump_to_kernel(kva_to_pa(entry_va), ba_pa);
 __builtin_unreachable();
}
static const char boot_xnu_flag[] = "DARWIN-REDWOOD:BOOT-XNU";
static int pmsg_take(const char *flag, uint32_t n)
{
 volatile struct ramoops_hdr *h = (volatile struct ramoops_hdr *)0x48160000ULL;
 volatile char *d = (volatile char *)(0x48160000ULL + sizeof(struct ramoops_hdr));
 uint32_t cap = 0x10000u - sizeof(struct ramoops_hdr);
 int found = 0;
 if (h->sig != 0x43474244u || h->size > cap) {
  return 0;
 }
 for (uint32_t at = 0; at + n <= cap; at++) {
  uint32_t k = 0;
  while (k < n && d[at + k] == flag[k]) {
   k++;
  }
  if (k == n) {
   for (k = 0; k < n; k++) {
    d[at + k] = '-';
   }
   found = 1;
  }
 }
 return found;
}
static int xnu_requested(void)
{
 return pmsg_take(boot_xnu_flag, sizeof(boot_xnu_flag) - 1);
}
static int boot_choice(void)
{
 int xnu = 1 || xnu_requested();
 uint64_t screen = fbc.base;
 fbc.base = 0;
 dbg_puts(xnu ? "boot: Darwin / XNU (requested)\n" : "boot: Android\n");
 fbc.base = screen;
 fb_rect(0, 600, 1080u, 160, 0xff000000u);
 fbc.scale = 3;
 fbc.x = 48;
 fbc.y = 640;
 fbc.fg = xnu ? 0xff40ff40u : 0xffffffffu;
 for (const char *c = xnu ? "Booting Darwin / XNU" : "Booting Android"; *c; c++) {
  fbcon_putc(*c);
 }
 fbc.scale = 1;
 fbc.fg = 0xffffffffu;
 return xnu;
}
void mtkloader_main(uint64_t dtb)
{
 uint32_t mark = marker_get();
 wdt_disable();
 lk_dtb = dtb;
 display_find();
 fbcon_init(fb_base, fb_pitch, 0, 1);
 if (mark == 0x4e44584eu) {
  report_park = 0;
  (void)snapshot_take();
  report_park = 1;
 }
 if (mark == 0x4e44584eu) {
  chain_android();
 }
 if (mark == 0x4e52584eu) {
  rescue_log();
  dbg_delay_ms(5000);
  chain_android();
 }
 plog_reset();
 (void)boot_choice;
 const int xnu_chosen = 1;
 fb_rect(0, 0, 1080u, 2460u, 0xff000000u);
 fbc.x = 0;
 fbc.y = fbc.top = 0;
 fbc.fg = 0xff40ff40u;
 out("== mtkloader: Darwin/XNU on " "XAGA (MT6895)" " ==\n");
 fbc.fg = 0xffffffffu;
 out("LK dtb ");
 out_hex(dtb);
 out("\n");
 run_xnu();
}
