/*
 * Changes by bmcleodcd: 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
/* Nonblocking optional display worker; no X11 linkage in the panel process. */
#define _GNU_SOURCE
#include "display-monitor.h"
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
extern char **environ;
void display_monitor_close(struct display_monitor *m)
{
 if(m->fd>=0)close(m->fd);
 if(m->pid>0) {kill(m->pid,SIGKILL);while(waitpid(m->pid,NULL,0)<0 && errno==EINTR){}}
 m->pid=0;m->fd=-1;
}
int display_monitor_poll(struct display_monitor *m,double now,struct display_mode *mode)
{
 if(m->pid>0) {
  pid_t done=waitpid(m->pid,NULL,WNOHANG);
  if(done==m->pid) {m->pid=0;display_monitor_close(m);m->next_start=now+2;}
  else if(now-m->last_sample>3) {display_monitor_close(m);m->next_start=now+2;}
 }
 if(m->pid<=0 && now>=m->next_start) {
  m->next_start=now+2;char path[1024];ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);
  if(n<=0)return 0;
  path[n]=0;char *slash=strrchr(path,'/');if(!slash)return 0;
  if((size_t)(slash-path)+sizeof("/picolcd-display-probe")>sizeof(path))return 0;
  strcpy(slash,"/picolcd-display-probe");int fds[2];if(pipe2(fds,O_CLOEXEC|O_NONBLOCK)<0)return 0;
  posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions,fds[1],STDOUT_FILENO);
  posix_spawn_file_actions_addclosefrom_np(&actions,3);
  char *argv[]={path,NULL};pid_t pid;
  int result=posix_spawn(&pid,path,&actions,NULL,argv,environ);
  posix_spawn_file_actions_destroy(&actions);close(fds[1]);
  if(result){close(fds[0]);return 0;}
  m->pid=pid;m->fd=fds[0];m->last_sample=now;
 }
 int valid=0;struct display_mode sample;
 if(m->fd>=0)while(read(m->fd,&sample,sizeof(sample))==(ssize_t)sizeof(sample)) {
  m->last_sample=now;
  if(sample.width && sample.width<=65535 && sample.height && sample.height<=65535 &&
     sample.centihz && sample.centihz<=100000 && (sample.interlaced==0 || sample.interlaced==1)) {
   *mode=sample;valid=1;
  }
 }
 return valid;
}
