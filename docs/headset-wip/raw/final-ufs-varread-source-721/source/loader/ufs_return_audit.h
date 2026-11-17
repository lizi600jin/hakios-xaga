/* Loader-only formatting of an accepted double-read snapshot. No storage or kernel logger. */
#ifndef MT6895_UFS_RETURN_AUDIT_H
#define MT6895_UFS_RETURN_AUDIT_H
#include "ufs_loader_guard.h"
static inline bool ur_audit_text(char *out,size_t cap,size_t *n,const char *s)
{
    while (*s) { if (*n>=cap-1) return false;out[(*n)++]=*s++; }
    return true;
}
static inline bool ur_audit_u32(char *out,size_t cap,size_t *n,uint32_t v,bool hex)
{
    char digits[10];size_t count=0;uint32_t radix=hex?16:10;
    do { digits[count++]="0123456789abcdef"[v%radix];v/=radix; } while(v);
    while (count) { if (*n>=cap-1) return false;out[(*n)++]=digits[--count]; }
    return true;
}
static inline size_t ur_guard_format_return(const struct ur_lease *r,char *out,size_t cap)
{
    char text[192];size_t n=0;
    if (!out || !ur_loader_may_use_storage(r)) return 0;
    if (!ur_audit_text(text,sizeof(text),&n,"\nmt6895-ufs-varread: return-gate magic=0x") ||
        !ur_audit_u32(text,sizeof(text),&n,r->magic,true) ||
        !ur_audit_text(text,sizeof(text),&n," version=") ||
        !ur_audit_u32(text,sizeof(text),&n,r->version,false) ||
        !ur_audit_text(text,sizeof(text),&n," trial=") ||
        !ur_audit_u32(text,sizeof(text),&n,r->trial,false) ||
        !ur_audit_text(text,sizeof(text),&n," state=") ||
        !ur_audit_u32(text,sizeof(text),&n,r->state,false) ||
        !ur_audit_text(text,sizeof(text),&n," allowed=1\n") || n>=cap) return 0;
    for (size_t i=0;i<n;i++) out[i]=text[i];out[n]=0;
    return n;
}
#endif
