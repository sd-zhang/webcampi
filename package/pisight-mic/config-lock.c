/* SPDX-License-Identifier: MIT
 * Serialize persistent JSON settings updates. Lock survives exec
 * and is released by the kernel even if the writer dies. No waiting. */
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#ifndef CONFIG_LOCK_PATH
#define CONFIG_LOCK_PATH "/run/isight-config.lock"
#endif
int main(int argc,char **argv)
{
 if(argc<2)return 2;
 int fd=open(CONFIG_LOCK_PATH,O_RDWR|O_CREAT,0600);
 if(fd<0 || flock(fd,LOCK_EX|LOCK_NB)<0) {perror("config lock");return 1;}
 execv(argv[1],argv+1);
 perror("config writer");return 1;
}
