// SPDX-License-Identifier: GPL-2.0-or-later
#include "maininterface/dcomp_visual_transfer.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
struct Tree { int owner, failAdd, failCommit, commits; };
static int removeVisual(void *p, int dst) { struct Tree *t=p; if(t->owner!=dst)return -1; t->owner=-1;return 0; }
static int addVisual(void *p, int dst) { struct Tree *t=p; if(t->owner!=-1||t->failAdd==dst)return -2; t->owner=dst;return 0; }
static int commit(void *p) { struct Tree *t=p;++t->commits;if(t->failCommit){--t->failCommit;return -3;}return 0; }
int main(void) {
 struct Tree t={0,-2,0,0}; struct vlc_dcomp_transfer state={0,1};
 struct vlc_dcomp_transfer_ops ops={&t,removeVisual,addVisual,commit};
 int i; for(i=0;i<32;++i){assert(vlc_dcomp_move(&state,&ops,1));assert(t.owner==1&&state.detached);assert(vlc_dcomp_move(&state,&ops,0));assert(t.owner==0&&!state.detached);}
 i=t.commits;assert(vlc_dcomp_move(&state,&ops,0));assert(t.commits==i);
 t.failAdd=1;assert(!vlc_dcomp_move(&state,&ops,1));assert(t.owner==0&&!state.detached&&state.healthy);
 t.failAdd=-2;t.failCommit=1;assert(!vlc_dcomp_move(&state,&ops,1));assert(t.owner==0&&!state.detached&&state.healthy);
 puts("failed commit rollback PASS");return 0;
}
