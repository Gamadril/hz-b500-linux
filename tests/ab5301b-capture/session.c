/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static const char *script,*session;
static void command(const char *operation,const char *text,int expected) {
    pid_t child=fork();assert(child>=0);
    if(!child) {
        execlp("bash","bash",script,operation,session,text,NULL);_exit(127);
    }
    int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status));
    assert(WEXITSTATUS(status)==expected);
}
static int is_state(const char *value) {
    char path[4096],line[64];snprintf(path,sizeof(path),"%s/state",session);
    FILE *file=fopen(path,"r");if(!file)return 0;
    char *result=fgets(line,sizeof(line),file);fclose(file);
    return result && !strncmp(line,value,strlen(value));
}
static void wait_state(const char *value) {
    for(int attempt=0;attempt<300;attempt++) {if(is_state(value))return;usleep(10000);}
    fprintf(stderr,"state %s not reached\n",value);abort();
}
static void wait_bytes(const char *direction,size_t count) {
    char path[4096];snprintf(path,sizeof(path),"%s/%s.bin",session,direction);
    struct stat info;
    for(int attempt=0;attempt<300;attempt++) {
        if(!stat(path,&info) && info.st_size==(off_t)count)return;
        usleep(10000);
    }
    abort();
}
static void verify(const char *direction,const unsigned char *expected,size_t size) {
    char path[4096];snprintf(path,sizeof(path),"%s/%s.bin",session,direction);
    FILE *file=fopen(path,"rb");assert(file);unsigned char bytes[100];
    assert(fread(bytes,1,sizeof(bytes),file)==size);assert(!memcmp(bytes,expected,size));fclose(file);
}
int main(int argc,char **argv) {
    assert(argc==3);script=argv[1];session=argv[2];
    sigset_t mask;sigemptyset(&mask);assert(!sigprocmask(SIG_SETMASK,&mask,NULL));
    int master[2],slave[2];char ports[2][128];
    for(unsigned i=0;i<2;i++)assert(!openpty(&master[i],&slave[i],ports[i],NULL,NULL));
    /* CLOEXEC matters: the recorder must not inherit our probe endpoints. */
    for(unsigned i=0;i<2;i++) {
        assert(!fcntl(master[i],F_SETFD,FD_CLOEXEC));assert(!fcntl(slave[i],F_SETFD,FD_CLOEXEC));
    }
    pid_t recorder=fork();assert(recorder>=0);
    if(!recorder) {execlp("bash","bash",script,"run",ports[0],ports[1],session,NULL);_exit(127);}
    wait_state("running");command("status",NULL,0);
    unsigned char tx[]={2,128,0xaa,1,0,0x2b};
    unsigned char rx[]={2,129,0xdb,1,1,0x5a,2,128,0xaa,1,0,0x2b};
    assert(write(master[0],tx,2)==2);usleep(30000);
    command("begin","Speaker OFF -> ON; FM stays OFF",0);
    command("begin","Should reject overlapping actions",1);
    assert(write(master[0],tx+2,sizeof(tx)-2)==sizeof(tx)-2);
    assert(write(master[1],rx,sizeof(rx))==sizeof(rx));
    wait_bytes("f133-to-ab5301b",sizeof(tx));wait_bytes("ab5301b-to-f133",sizeof(rx));
    command("done","Speaker audible; line-out not checked",0);
    command("done","Should reject unmatched done",1);
    command("note","Returned to settings; leave recorder active",0);
    command("decode",NULL,0);
    for(unsigned i=0;i<2;i++) {
        struct pollfd probe={master[i],POLLIN,0};
        assert(poll(&probe,1,50)==0); /* no bytes transmitted by the host */
    }
    command("stop",NULL,0);int status;
    assert(waitpid(recorder,&status,0)==recorder && WIFEXITED(status) && WEXITSTATUS(status)==0);
    wait_state("stopped");verify("f133-to-ab5301b",tx,sizeof(tx));verify("ab5301b-to-f133",rx,sizeof(rx));
    command("run",NULL,1); /* missing arguments cannot alter the preserved session */
    puts("PASS: actual socat on two PTYs: raw NUL bytes, split frames, action/done markers, no UART writes, bounded stop and preserved evidence");
    /* A vanished serial interface must end the capture as ERROR. */
    char failure[4096];snprintf(failure,sizeof(failure),"%s-disconnect",session);session=failure;
    recorder=fork();assert(recorder>=0);
    if(!recorder) {execlp("bash","bash",script,"run",ports[0],ports[1],session,NULL);_exit(127);}
    wait_state("running");close(master[0]);close(slave[0]);
    assert(waitpid(recorder,&status,0)==recorder && WIFEXITED(status) && WEXITSTATUS(status)==1);
    wait_state("error");command("begin","Must reject markers after disconnect",1);
    puts("PASS: reader disconnect is reported and cannot silently continue as a healthy capture");
    close(master[1]);close(slave[1]);return 0;
}
