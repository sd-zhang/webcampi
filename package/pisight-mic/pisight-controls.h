/* SPDX-License-Identifier: MIT
 * UVC selectors 3 (audio) and 4 (diagnostics), 32-byte versioned protocol.
 * See docs/uvc-audio-controls.md. Disk writes run in a child, never on the
 * camera event loop. The audio callback uses only shared atomic words.
 */
#ifndef PISIGHT_CONTROLS_H
#define PISIGHT_CONTROLS_H
#include "audio-control.h"
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#ifndef CONFIG_STORE_PATH
#define CONFIG_STORE_PATH "/usr/bin/isight-config-store"
#endif
struct pisight_control_reply {uint32_t token;unsigned status;};
struct pisight_controls {
 struct audio_shared *shared;
 struct pisight_control_reply reply[2];
 uint32_t saved_audio, save_word;
 unsigned saved_diagnostics, save_kind;
 pid_t writer;
};
static inline void pisight_controls_init(struct pisight_controls *s)
{
 memset(s,0,sizeof(*s));
 struct audio_config c=audio_default();char extra;
 const char *env=getenv("ISIGHT_AUDIO");
 if(env) {
  struct audio_config candidate;
  if(sscanf(env,"%d %u %u %u %c",&candidate.gain,&candidate.hp,&candidate.lp,&candidate.bypass,&extra)==4 && audio_valid(candidate))c=candidate;
 }
 s->saved_audio=audio_pack(c);
 env=getenv("ISIGHT_DIAGNOSTICS");s->saved_diagnostics=env && !strcmp(env,"1");
 s->shared=audio_map(AUDIO_RUNTIME_PATH);
 if(s->shared)audio_store(&s->shared->requested,s->saved_audio);
}
static inline void pisight_controls_poll(struct pisight_controls *s)
{
 if(!s->writer)return;
 int status;pid_t ret=waitpid(s->writer,&status,WNOHANG);
 if(!ret || (ret<0 && errno==EINTR))return;
 unsigned ok=ret==s->writer && WIFEXITED(status) && WEXITSTATUS(status)==0;
 s->reply[s->save_kind].status=ok?0:2;
 if(ok) {
  if(!s->save_kind)s->saved_audio=s->save_word;
  else s->saved_diagnostics=s->save_word;
 }
 s->writer=0;
}
static inline void pisight_controls_close(struct pisight_controls *s)
{
 pisight_controls_poll(s);
 if(s->shared)munmap(s->shared,sizeof(*s->shared));
 s->shared=NULL;
 /* A pending save may finish after daemon exit. No blocking wait here. */
}
static inline void pisight_controls_read(struct pisight_controls *s,unsigned kind,uint8_t *p)
{
 memset(p,0,32);p[0]=1;
 if(kind>1) {p[1]=1;return;}
 pisight_controls_poll(s);
 p[1]=s->reply[kind].status;audio_put32(p+4,s->reply[kind].token);
 if(s->writer)p[2]|=4;
 if(kind) {
  p[8]=audio_diagnostics_get();p[16]=s->saved_diagnostics;
  if(p[8]!=p[16])p[2]|=2;
  return;
 }
 if(!s->shared) {p[1]=2;return;}
 uint32_t current=audio_load(&s->shared->requested);
 audio_encode(p+8,audio_unpack(current));
 audio_encode(p+16,audio_unpack(audio_load(&s->shared->applied)));
 if(current!=s->saved_audio)p[2]|=2;
 struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
 uint32_t heartbeat=audio_load(&s->shared->heartbeat);
 if(heartbeat && (uint32_t)now.tv_sec-heartbeat<=2)p[2]|=1;
 audio_put16(p+24,audio_load(&s->shared->peak));
 audio_put32(p+28,audio_load(&s->shared->clips));
}
static inline void pisight_controls_command(struct pisight_controls *s,unsigned kind,const uint8_t *p,int len)
{
 if(kind>1)return;
 pisight_controls_poll(s);
 /* One transaction at a time. While saving keep its token/status intact;
  * another request is not accepted and must be retried after busy clears. */
 if(s->writer)return;
 struct pisight_control_reply *r=&s->reply[kind];
 r->token=len>=8?audio_u32(p+4):0;r->status=1;
 if(len!=32 || p[0]!=1 || p[1]<1 || p[1]>3 || p[2] || p[3] || !r->token)return;
 for(unsigned i=16;i<32;i++)if(p[i])return;
 uint32_t desired;
 if(p[1]==1) {
  if(kind) {if(p[8]>1)return;for(unsigned i=9;i<16;i++)if(p[i])return;desired=p[8];}
  else {struct audio_config c=audio_decode(p+8);if(p[15] || !audio_valid(c))return;desired=audio_pack(c);}
 } else {
  for(unsigned i=8;i<16;i++)if(p[i])return;
  if(p[1]==3)desired=kind?0:audio_pack(audio_default());
  else if(kind)desired=audio_diagnostics_get();
  else {if(!s->shared){r->status=2;return;}desired=audio_load(&s->shared->requested);}
 }
 if(p[1]!=2) {
  if(kind) {if(audio_diagnostics_set(desired)<0){r->status=2;return;}}
  else {if(!s->shared){r->status=2;return;}audio_store(&s->shared->requested,desired);}
  r->status=0;return;
 }
 s->save_kind=kind;s->save_word=desired;
 /* Prepare argv before fork: uvc-gadget has other threads. The child must
  * use only async-signal-safe exec/_exit, never stdio or allocation. */
 char gain[16],hp[16],lp[16],bypass[4];
 if(kind)snprintf(bypass,sizeof bypass,"%u",desired);
 else {
  struct audio_config c=audio_unpack(desired);
  snprintf(gain,sizeof gain,"%d",c.gain);snprintf(hp,sizeof hp,"%u",c.hp);
  snprintf(lp,sizeof lp,"%u",c.lp);snprintf(bypass,sizeof bypass,"%u",c.bypass);
 }
 pid_t child=fork();
 if(child<0){r->status=2;return;}
 if(!child) {
  if(kind)execl(CONFIG_STORE_PATH,"isight-config-store","--diagnostics",bypass,(char *)NULL);
  else execl(CONFIG_STORE_PATH,"isight-config-store","--audio",gain,hp,lp,bypass,(char *)NULL);
  _exit(127);
 }
 s->writer=child;r->status=3;
}
#endif
