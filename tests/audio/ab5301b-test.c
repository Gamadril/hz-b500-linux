/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual controller handlers/serial transport against a PTY companion. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define main ab5301b_program_main
#include "../../package/utils/ab5301b/src/ab5301b.c"
#undef main

struct shared {
	volatile int quit, failure, corrupt, reset, forbidden, last_cmd, c3_after_d4;
	volatile unsigned count[256];
	uint8_t aa[10], b8[7], source[2];
};
static struct shared *s;
static pid_t emulator_pid=-1;
static struct controller test_controller;
static char test_reply[1024];
static void cleanup(void)
{
	if (emulator_pid>0) { kill(emulator_pid,SIGTERM); waitpid(emulator_pid,NULL,0); }
}
static void require(int condition, const char *what)
{
	if (!condition) { fprintf(stderr,"FAIL: %s\n",what); exit(1); }
}
static size_t packet(uint8_t cls,uint8_t cmd,const uint8_t *payload,size_t len,uint8_t *out)
{
	size_t i; uint8_t sum=0;
	out[0]=2; out[1]=cls; out[2]=cmd; out[3]=len;
	memcpy(out+4,payload,len);
	for(i=1;i<len+4;i++) sum^=out[i];
	out[len+4]=sum; return len+5;
}
static void send_packet(int fd,uint8_t cls,uint8_t cmd,const uint8_t *payload,size_t len)
{
	uint8_t b[260]; size_t n=packet(cls,cmd,payload,len,b);
	/* Force partial reads; a terminal read is never assumed to be a frame. */
	if (write(fd,b,2)!=2) _exit(4);
	usleep(1500);
	if(write(fd,b+2,n-2)!=(ssize_t)n-2) _exit(4);
}
static void emulator(int master)
{
	uint8_t bytes[520]; size_t used=0;
	static const uint8_t version[]="\0AB5301_WIFI_20231120_0931";
	static const uint8_t key[]={0,0xce,0,0,0,0};
	while(!s->quit) {
		struct pollfd p={master,POLLIN,0}; ssize_t k;
		if(s->reset) {
			memset(s->aa,0,sizeof(s->aa)); memset(s->b8,0,sizeof(s->b8));
			s->reset=0;
			send_packet(master,0x81,0xc3,version+1,sizeof(version)-2);
		}
		if(poll(&p,1,20)<=0) continue;
		k=read(master,bytes+used,sizeof(bytes)-used);
		if(k<0 && (errno==EIO || errno==EAGAIN || errno==EINTR)) { usleep(1000); continue; }
		if(k<=0) continue;
		used+=k;
		while(used>=5) {
			size_t n=bytes[3]+5U, i; uint8_t sum=0, cmd=bytes[2], status=0;
			if(used<n) break;
			for(i=1;i<n;i++) sum^=bytes[i];
			if(bytes[0]!=2 || bytes[1]!=0x80 || sum) _exit(5);
			s->count[cmd]++;
			if(cmd==0xc3) s->c3_after_d4=s->last_cmd==0xd4;
			s->last_cmd=cmd;
			if(cmd==0xd4) goto consumed; /* Bluetooth AT: the radio sends no ACK. */
			if(cmd!=0xc3 && cmd!=0xaa && cmd!=0xb8 && cmd!=0xa7 && cmd!=0xa0 && cmd!=0xa4) s->forbidden++;
			if(s->failure && cmd==0xb8) { status=s->failure==2 ? 1:0; i=s->failure; s->failure=0; if(i==1) goto consumed; }
			if(!status) {
				if(cmd==0xaa && bytes[3]==10) memcpy(s->aa,bytes+4,10);
				if(cmd==0xb8 && bytes[3]==7) memcpy(s->b8,bytes+4,7);
				if(cmd==0xa7 && bytes[3]==2) memcpy(s->source,bytes+4,2);
			}
			/* Reports, wrong-command and wrong-class replies cannot satisfy ACK. */
			send_packet(master,0x81,0xb2,key,sizeof(key));
			if(s->corrupt) {
				uint8_t bad[6]={2,0x80,0xaa,1,0,0};
				s->corrupt=0;
				if(write(master,bad,sizeof(bad))!=(ssize_t)sizeof(bad)) _exit(6);
				send_packet(master,0x80,0xff,&status,1);
				send_packet(master,0x81,cmd,&status,1);
			}
			if(cmd==0xc3) send_packet(master,0x80,cmd,version,sizeof(version)-1);
			else if(cmd==0xa4) { uint8_t a4[10]={0,0,4}; send_packet(master,0x80,cmd,a4,10); }
			else send_packet(master,0x80,cmd,&status,1);
		consumed:
			used-=n; memmove(bytes,bytes+n,used);
		}
	}
	_exit(0);
}
static int control(const char *cmd,const char *arg)
{
	char line[256];
	snprintf(line,sizeof(line),"%s%s%s\n",cmd,arg ? " ":"",arg ? arg:"");
	return command(&test_controller,line,test_reply,sizeof(test_reply));
}
static int status_has(const char *text)
{
	require(control("status",NULL)==0,"status request");
	return strstr(test_reply,text)!=NULL;
}
static void captured_stream(const char *path, unsigned expected)
{
	FILE *file=fopen(path,"r");
	struct decoder d={0}; struct frame frame;
	unsigned byte, frames=0;
	require(file!=NULL,"open original stock capture");
	while(fscanf(file,"%2x",&byte)==1) {
		require(d.n<sizeof(d.data),"captured frame bounded");
		d.data[d.n++]=byte;
		while(decode(&d,&frame)) frames++;
	}
	require(feof(file),"stock capture consists of hex bytes");
	fclose(file);
	require(frames==expected && d.n==0,"decode every original stock frame");
}
int main(int argc,char **argv)
{
	int master, i; char device[128]; unsigned aa_count;
	require(argc==3,"arguments: stock TX hex, stock RX hex");
	captured_stream(argv[1],34);
	captured_stream(argv[2],39);
	{
		uint8_t actual[5], expected[]={2,0x80,0xc3,0,0x43};
		require(encode(0xc3,NULL,0,actual)==5 && !memcmp(actual,expected,5),"encode captured firmware query");
	}
	atexit(cleanup);
	s=mmap(NULL,sizeof(*s),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
	require(s!=MAP_FAILED,"shared memory");
	master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
	require(master>=0 && !grantpt(master) && !unlockpt(master),"PTY");
	snprintf(device,sizeof(device),"%s",ptsname(master));
	test_controller.serial=open_serial(device);
	require(test_controller.serial>=0,"open serial");
	test_controller.timeout=300;
	test_controller.desired.volume=18;
	test_controller.desired.speaker=1;
	test_controller.desired.mute=1;
	/* Non-default unrelated flags must survive control changes. */
	require(!template_hex("0000070700000a051200",test_controller.desired.aa,10),"AA template");
	require(!template_hex("03838000040001",test_controller.desired.b8,7),"B8 template");
	emulator_pid=fork(); require(emulator_pid>=0,"fork companion");
	if(!emulator_pid) { close(test_controller.serial); emulator(master); }
	close(master);
	require(!recover(&test_controller),"startup probe and state application");
	require(status_has("settings_acknowledged=1"),"initial settings acknowledged");
	require(s->aa[0]==0 && s->aa[4]==1 && s->aa[8]==18,"muted startup retains volume 18");
	require(s->aa[7]==5 && s->b8[1]==0x83,"speaker on preserves unrelated flags");
	s->corrupt=1;
	require(!control("mute","off"),"fragmented ACK after checksum error and reports");
	require(s->aa[0]==18 && s->aa[4]==0 && s->aa[8]==18,"unmute restores volume");
	require(!control("volume","20"),"volume 20");
	require(s->aa[0]==20 && s->aa[8]==20,"effective and retained volume");
	aa_count=s->count[0xaa];
	require(control("volume","21")!=0,"reject unverified volume range");
	require(control("fm","on")!=0 && control("lineout","on")!=0,"undecoded controls rejected");
	require(s->count[0xaa]==aa_count,"invalid/unsupported commands send no packets");
	require(!control("speaker","off"),"speaker off");
	require(s->aa[7]==4 && s->b8[1]==0x82,"both speaker flags cleared, other flags preserved");
	require(!control("mute","on"),"mute");
	require(s->aa[0]==0 && s->aa[8]==20,"mute retains requested volume");
	require(!control("source","sd"),"logical SD source selection");
	require(s->source[0]==2 && s->source[1]==0,"SD source bytes");
	require(!control("probe",NULL) && status_has("settings_acknowledged=1"),"probe preserves applied status");
	s->failure=1;
	require(control("speaker","on")!=0,"missing B8 ACK fails command");
	require(status_has("settings_acknowledged=0"),"partial apply is visible");
	require(s->aa[7]==5 && s->b8[1]==0x82,"failure leaves partial hardware state");
	require(!control("apply",NULL) && s->b8[1]==0x83,"explicit retry converges desired state");
	s->failure=2;
	require(control("speaker","off")!=0,"nonzero reply rejected");
	require(!control("apply",NULL),"retry after nonzero status");
	require(!control("initialize",NULL),"experimental initialization subset");
	require(s->count[0xa4]==1 && s->count[0xa0]==1,"A4/A0 init exchange");
	aa_count=s->count[0xaa]; s->reset=1;
	for(i=0;i<100 && !test_controller.reset_seen;i++) {
		struct frame f;
		serial_frame(&test_controller,&f,now_ms()+30);
	}
	require(test_controller.reset_seen && !test_controller.applied,"companion report invalidates applied state");
	require(s->count[0xd4]==0,"no Bluetooth AT before a companion reset");
	require(!recover(&test_controller) && s->count[0xaa]>aa_count,"state restored after reset");
	require(s->count[0xd4]==2 && s->c3_after_d4,"Bluetooth power (AT#CD, AT#CB) re-sent before C3/A7 after reset");
	require(status_has("settings_acknowledged=1"),"restored state acknowledged");
	require(s->aa[0]==0 && s->aa[8]==20 && s->b8[1]==0x82,"requested settings survive companion reset");
	require(s->forbidden==0,"no opaque boot, AT or FM packets sent");
	/* A second opener cannot steal the UART. */
	{
		int second=open_serial(device);
		require(second<0,"exclusive UART ownership");
	}
	close(test_controller.serial);
	puts("PASS: 73 stock frames, framing, reports, checksum recovery, state controls, errors, reset recovery, UART ownership");
	return 0;
}
