/* SPDX-License-Identifier: MIT
 * 48 kHz, second-order Butterworth HP/LP. Formulae:
 * https://www.w3.org/TR/audio-eq-cookbook/ . No extra audio queue/lookahead.
 */
#ifndef PISIGHT_VOICE_FILTER_H
#define PISIGHT_VOICE_FILTER_H
#include <math.h>
#include <string.h>
#include "audio-control.h"
struct voice_section {float b0,b1,b2,a1,a2,z1,z2;};
struct voice_bank {struct voice_section hp,lp;float gain;struct audio_config config;};
struct voice_filter {struct voice_bank current,next;unsigned remaining;int clipped;};
static inline struct voice_section voice_coeff(unsigned hz,int high)
{
 if(!hz) return (struct voice_section){1,0,0,0,0,0,0};
 double w=6.2831853071795864769*hz/48000,c=cos(w),a=sin(w)/1.4142135623730951;
 double b=high?(1+c)/2:(1-c)/2;
 return (struct voice_section){b/(1+a),(high?-2:2)*b/(1+a),b/(1+a),-2*c/(1+a),(1-a)/(1+a),0,0};
}
static inline struct voice_bank voice_bank_create(struct audio_config c)
{return (struct voice_bank){voice_coeff(c.hp,1),voice_coeff(c.lp,0),(float)pow(10,c.gain/200.0),c};}
static inline void voice_filter_reset(struct voice_filter *f)
{memset(f,0,sizeof(*f));f->current=voice_bank_create(audio_default());}
/* Requests during a fade stay in the shared word for the next block. */
static inline void voice_filter_update(struct voice_filter *f,struct audio_config c)
{
 if(!audio_valid(c) || f->remaining || audio_pack(c)==audio_pack(f->current.config)) return;
 f->next=voice_bank_create(c);
 if(c.hp==f->current.config.hp && c.lp==f->current.config.lp && !f->current.config.bypass) {
  f->next.hp.z1=f->current.hp.z1;f->next.hp.z2=f->current.hp.z2;
  f->next.lp.z1=f->current.lp.z1;f->next.lp.z2=f->current.lp.z2;
 }
 f->remaining=960; /* 20 ms crossfade; no coefficient interpolation. */
}
static inline float voice_section_run(struct voice_section *s,float x)
{float y=s->b0*x+s->z1;s->z1=s->b1*x-s->a1*y+s->z2;s->z2=s->b2*x-s->a2*y;return y;}
static inline float voice_bank_run(struct voice_bank *b,float x)
{return b->config.bypass?x:voice_section_run(&b->lp,voice_section_run(&b->hp,x))*b->gain;}
static inline int16_t voice_filter_sample(struct voice_filter *f,float x)
{
 float y=voice_bank_run(&f->current,x);
 if(f->remaining) {
  float next=voice_bank_run(&f->next,x);
  float mix=(961-f->remaining)/960.0f;
  y+=(next-y)*mix;
  if(!--f->remaining) f->current=f->next;
 }
 /* Settled bypass matches the old S32 -> S16 arithmetic-shift conversion. */
 if(!f->remaining && f->current.config.bypass) {
  f->clipped=0;
  if(x>=32767)return 32767;
  if(x<=-32768)return -32768;
  int value=(int)x;return (int16_t)(value-(x<value));
 }
 f->clipped=y>32767.0f || y<-32768.0f;
 if(y>=32767.0f)return 32767;
 if(y<=-32768.0f)return -32768;
 return (int16_t)(y>=0?y+.5f:y-.5f);
}
static inline void voice_quiet_section(struct voice_section *s)
{if(fabsf(s->z1)<1e-15f && fabsf(s->z2)<1e-15f)s->z1=s->z2=0;}
static inline void voice_filter_quiet_tail(struct voice_filter *f)
{
 voice_quiet_section(&f->current.hp);voice_quiet_section(&f->current.lp);
 voice_quiet_section(&f->next.hp);voice_quiet_section(&f->next.lp);
}
#endif
