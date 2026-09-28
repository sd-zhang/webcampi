/* SPDX-License-Identifier: MIT */
#ifndef PISIGHT_AUDIO_CONTROL_H
#define PISIGHT_AUDIO_CONTROL_H
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#ifndef DIAGNOSTICS_RUNTIME_PATH
#define DIAGNOSTICS_RUNTIME_PATH "/run/pisight-diagnostics"
#endif
static inline int audio_diagnostics_get(void)
{
 FILE *f=fopen(DIAGNOSTICS_RUNTIME_PATH,"r");
 if(!f)return 0;
 int a=fgetc(f),b=fgetc(f);fclose(f);return a=='1' && (b=='\n' || b==EOF);
}
static inline int audio_diagnostics_set(unsigned enabled)
{
 const char *tmp=DIAGNOSTICS_RUNTIME_PATH ".tmp";
 int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0600);
 if(fd<0)return -1;
 const char data[2]={enabled?'1':'0','\n'};
 int ok=write(fd,data,2)==2;
 if(close(fd)<0)ok=0;
 if(ok && !rename(tmp,DIAGNOSTICS_RUNTIME_PATH))return 0;
 unlink(tmp);return -1;
}
#ifndef AUDIO_RUNTIME_PATH
#define AUDIO_RUNTIME_PATH "/run/pisight-audio"
#endif
/* Gain in tenths of dB (0.5 dB steps); Hz, zero disables that section. */
struct audio_config { int gain; unsigned hp, lp, bypass; };
static inline struct audio_config audio_default(void)
{ return (struct audio_config){0,80,8000,0}; }
static inline int audio_valid(struct audio_config c)
{
 return c.gain>=-240 && c.gain<=240 && c.gain%5==0 &&
  (!c.hp || (c.hp>=20 && c.hp<=500)) &&
  (!c.lp || (c.lp>=1000 && c.lp<=20000 && c.lp%100==0)) && c.bypass<=1;
}
/* Entire preset fits in one lock-free ARMv6 word. No torn settings or locks
 * in audio transfer. This encoding is private IPC, not the UVC wire format. */
static inline uint32_t audio_pack(struct audio_config c)
{ return (uint32_t)((c.gain+240)/5) | c.hp<<7 | (c.lp/100)<<16 | c.bypass<<24; }
static inline struct audio_config audio_unpack(uint32_t v)
{ return (struct audio_config){(int)(v&127)*5-240,(v>>7)&511,((v>>16)&255)*100,(v>>24)&1}; }
static inline int audio_valid_word(uint32_t v)
{ return !(v>>25) && audio_valid(audio_unpack(v)); }
struct audio_shared { uint32_t magic, requested, applied, peak, clips, heartbeat; };
_Static_assert(__atomic_always_lock_free(4,0), "Audio IPC requires lock-free words");
static inline uint32_t audio_load(const uint32_t *p) { return __atomic_load_n(p,__ATOMIC_ACQUIRE); }
static inline void audio_store(uint32_t *p,uint32_t v) { __atomic_store_n(p,v,__ATOMIC_RELEASE); }
static inline struct audio_shared *audio_map(const char *path)
{
 int fd=open(path,O_RDWR|O_CREAT,0600);
 if(fd<0) return NULL;
 struct audio_shared *s=NULL;
 struct stat st;
 struct flock lock={0};lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
 while(fcntl(fd,F_SETLKW,&lock)<0)if(errno!=EINTR)goto done;
 if(fstat(fd,&st)<0) goto done;
 if(st.st_size && st.st_size!=(off_t)sizeof(*s)) goto done;
 if(!st.st_size && ftruncate(fd,sizeof(*s))<0) goto done;
 s=mmap(NULL,sizeof(*s),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
 if(s==MAP_FAILED) {s=NULL;goto done;}
 if(!st.st_size || !audio_load(&s->magic)) {
  audio_store(&s->requested,audio_pack(audio_default()));
  audio_store(&s->applied,audio_pack(audio_default()));
  audio_store(&s->peak,0);audio_store(&s->clips,0);audio_store(&s->heartbeat,0);
  audio_store(&s->magic,0x50415531);
 } else if(audio_load(&s->magic)!=0x50415531) {munmap(s,sizeof(*s));s=NULL;}
done:
 close(fd);return s;
}
static inline unsigned audio_u16(const uint8_t *p) {return p[0]|(unsigned)p[1]<<8;}
static inline uint32_t audio_u32(const uint8_t *p) {return audio_u16(p)|(uint32_t)audio_u16(p+2)<<16;}
static inline void audio_put16(uint8_t *p,unsigned v) {p[0]=v;p[1]=v>>8;}
static inline void audio_put32(uint8_t *p,uint32_t v) {audio_put16(p,v);audio_put16(p+2,v>>16);}
static inline void audio_encode(uint8_t *p,struct audio_config c)
{audio_put16(p,(unsigned)c.gain);audio_put16(p+2,c.hp);audio_put16(p+4,c.lp);p[6]=c.bypass;p[7]=0;}
static inline struct audio_config audio_decode(const uint8_t *p)
{return (struct audio_config){(int16_t)audio_u16(p),audio_u16(p+2),audio_u16(p+4),p[6]};}
#endif
