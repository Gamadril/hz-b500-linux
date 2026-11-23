/* SPDX-License-Identifier: GPL-2.0-only */
/* Real UART decoder/IPC handlers with PTY radio and interleaved audio ACKs. */
#define main ab5301b_program_main
#include "../../package/utils/ab5301b/src/ab5301b.c"
#undef main
#include <assert.h>
#include <sys/wait.h>

static void exact(int fd, void *p, size_t n)
{
	int64_t deadline=now_ms()+2000;
	while(n) {
		struct pollfd pollfd={fd,POLLIN,0};
		ssize_t k;
		assert(poll(&pollfd,1,deadline-now_ms())>0);
		k=read(fd,p,n); assert(k>0); p=(uint8_t *)p+k; n-=k;
	}
}
static size_t incoming(uint8_t cmd,const void *p,size_t n,uint8_t *out)
{
	size_t len=encode(cmd,p,n,out);
	out[1]^=1; out[len-1]^=1; return len;
}
static struct frame radio_read(int master)
{
	struct frame f;
	uint8_t bytes[260]; size_t i;
	exact(master,bytes,4);
	assert(bytes[0]==2 && bytes[1]==0x80);
	exact(master,bytes+4,bytes[3]+1);
	f.cls=bytes[1]; f.cmd=bytes[2]; f.len=bytes[3];
	memcpy(f.payload,bytes+4,f.len);
	for(i=2;i<f.len+5U;i++) bytes[1]^=bytes[i];
	assert(!bytes[1]); return f;
}
static void expect_ipc(int peer,uint8_t type,const void *p,size_t n)
{
	uint8_t header[4],data[255];
	exact(peer,header,4);
	assert(header[0]==1 && header[1]==type && header[2]==0 && header[3]==n);
	exact(peer,data,n); assert(!n || !memcmp(data,p,n));
}
static void expect_at(int master,const char *text)
{
	struct frame f=radio_read(master);
	assert(f.cmd==0xd4 && f.len==strlen(text) && !memcmp(f.payload,text,f.len));
}
static int connect_service(const char *path)
{
	struct sockaddr_un address;
	int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
	assert(fd>=0 && !make_address(&address,path));
	assert(!connect(fd,(struct sockaddr *)&address,sizeof(address)));
	return fd;
}
static void service_test(void)
{
	char directory[]="/tmp/ab5301b-bt-XXXXXX",path[128],btpath[128],device[128],lockpath[128];
	struct stat permissions;
	struct frame f;
	uint8_t bytes[260],zero=0;
	int master,slave,bt,cli,second,status,i; pid_t child; size_t length;
	int64_t start;
	assert(mkdtemp(directory));
	snprintf(path,sizeof(path),"%s/control",directory);
	snprintf(btpath,sizeof(btpath),"%s/control.bt",directory);
	snprintf(lockpath,sizeof(lockpath),"%s/control.lock",directory);
	master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
	assert(master>=0 && !grantpt(master) && !unlockpt(master));
	snprintf(device,sizeof(device),"%s",ptsname(master));
	/* Keep the slave open until the child opens it, avoiding PTY master EIO. */
	slave=open(device,O_RDWR|O_NOCTTY); assert(slave>=0);
	child=fork(); assert(child>=0);
	if (!child) {
		struct controller c={.serial=-1,.timeout=300,.bluetooth=true};
		close(master); close(slave);
		strcpy(c.bt_name,"SWL-BT"); c.desired.b8[0]=3;
		signal(SIGTERM,stop_signal); signal(SIGPIPE,SIG_IGN);
		_exit(server(&c,device,path));
	}
	/* Bluetooth first: name, then default-off power (disconnect + no page scan),
	 * so the A7 source switch cannot auto-reconnect the last phone. */
	f=radio_read(master); assert(f.cmd==0xd4 && f.len==13);
	expect_at(master,"AT#CD\r\n"); expect_at(master,"AT#CB\r\n");
	/* Reply to the actual service's startup C3/AA/B8 sequence. */
	for (i=0;i<3;i++) {
		static const uint8_t version[]={0,'V'};
		f=radio_read(master);
		assert(f.cmd==(i==0 ? 0xc3:i==1 ? 0xaa:0xb8));
		length=encode(f.cmd,i==0 ? version:&zero,i==0 ? sizeof(version):1,bytes);
		assert(write(master,bytes,length)==(ssize_t)length);
	}
	close(slave);
	assert(!stat(btpath,&permissions) && (permissions.st_mode&0777)==0600);
	bt=connect_service(btpath); expect_ipc(bt,BT_STATE,&zero,1);
	second=connect_service(btpath);
	{
		struct pollfd pollfd={second,POLLIN,0};
		assert(poll(&pollfd,1,2000)>0 && read(second,bytes,1)==0);
	}
	close(second);
	cli=connect_service(path); assert(write(cli,"sta",3)==3);
	start=now_ms();
	length=incoming(0xd4,"SW\r\n",4,bytes); assert(write(master,bytes,length)==(ssize_t)length);
	length=incoming(0xd6,"bootstrap",9,bytes); assert(write(master,bytes,length)==(ssize_t)length);
	expect_ipc(bt,BT_EVENT,"SW\r\n",4); expect_ipc(bt,BT_DATA,"bootstrap",9);
	assert(now_ms()-start<500); /* An incomplete CLI cannot stall UART for its 1s deadline. */
	assert(write(bt,(uint8_t[]){1,BT_SEND,0,3,'a','b','c'},7)==7);
	f=radio_read(master); assert(f.cmd==0xd6 && f.len==3 && !memcmp(f.payload,"abc",3));
	assert(write(cli,"tus\n",4)==4);
	{
		struct pollfd pollfd={cli,POLLIN,0};
		ssize_t n;
		assert(poll(&pollfd,1,2000)>0); n=read(cli,bytes,sizeof(bytes)-1);
		assert(n>3 && !memcmp(bytes,"OK ",3));
		bytes[n]=0; assert(strstr((char *)bytes," bluetooth=off bt_name=\n"));
	}
	close(cli);
	/* UI toggle path: power on sends stock enable + reconnect and updates status. */
	cli=connect_service(path); assert(write(cli,"bluetooth on\n",13)==13);
	expect_at(master,"AT#CA\r\n"); expect_at(master,"AT#CC\r\n");
	{
		struct pollfd pollfd={cli,POLLIN,0};
		ssize_t n;
		assert(poll(&pollfd,1,2000)>0); n=read(cli,bytes,sizeof(bytes)-1);
		assert(n>3 && !memcmp(bytes,"OK ",3));
	}
	close(cli);
	cli=connect_service(path); assert(write(cli,"status\n",7)==7);
	{
		struct pollfd pollfd={cli,POLLIN,0};
		ssize_t n;
		assert(poll(&pollfd,1,2000)>0); n=read(cli,bytes,sizeof(bytes)-1);
		assert(n>0); bytes[n]=0; assert(strstr((char *)bytes," bluetooth=on bt_name=\n"));
	}
	close(cli);
	/* Rename: AT#MM is sent; status shows only what the radio echoes back. */
	cli=connect_service(path); assert(write(cli,"name HZ-B500\n",13)==13);
	expect_at(master,"AT#MMHZ-B500\r\n");
	{
		struct pollfd pollfd={cli,POLLIN,0};
		assert(poll(&pollfd,1,2000)>0 && read(cli,bytes,3)==3 && !memcmp(bytes,"OK ",3));
	}
	close(cli);
	length=incoming(0xd4,"MMHZ-B500\r\n",11,bytes); assert(write(master,bytes,length)==(ssize_t)length);
	expect_ipc(bt,BT_EVENT,"MMHZ-B500\r\n",11);
	cli=connect_service(path); assert(write(cli,"status\n",7)==7);
	{
		struct pollfd pollfd={cli,POLLIN,0};
		ssize_t n;
		assert(poll(&pollfd,1,2000)>0); n=read(cli,bytes,sizeof(bytes)-1);
		assert(n>0); bytes[n]=0; assert(strstr((char *)bytes," bt_name=HZ-B500\n"));
	}
	close(cli); close(bt); kill(child,SIGTERM);
	assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
	close(master);
	assert(!unlink(lockpath) && !rmdir(directory));
}
/* Stock BT music captures (2026-11-20): phone reports in, controls and AA volume out. */
static void bt_music_test(void)
{
	struct controller c={.timeout=300,.bt_playing=-1,.desired={.volume=18,.speaker=1,.source=SOURCE_BT_MUSIC}};
	struct bt_bridge bridge={.listener=-1,.client=-1};
	static const uint8_t mi1[]={0x02,0x81,0xd4,0x29,0x4d,0x49,0x73,0x70,0x6c,0x69,0x6e,0x5f,0x2d,0x5f,0x70,0x69,0x73,0x6d,0x6f,0xff,
		0x55,0x6e,0x62,0x65,0x6b,0x61,0x6e,0x6e,0x74,0xff,0x44,0x6f};
	static const uint8_t mi2[]={0x77,0x6e,0x6c,0x6f,0x61,0x64,0xff,0x31,0x34,0x39,0xff,0x0d,0x0a,0x18};
	static const uint8_t mb[]={0x02,0x81,0xd4,0x04,0x4d,0x42,0x0d,0x0a,0x59};
	static const uint8_t pause_aa[]={0x00,0x00,0x07,0x07,0x00,0x00,0x0a,0x01,0x12,0x00};
	static const uint8_t play_aa[]={0x12,0x00,0x07,0x07,0x00,0x00,0x0a,0x01,0x12,0x00};
	char line[16], reply[512];
	struct frame f; uint8_t bytes[260]; size_t len; int master, status; pid_t child;
	master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
	assert(master>=0 && !grantpt(master) && !unlockpt(master));
	c.serial=open_serial(ptsname(master)); assert(c.serial>=0); c.bt=&bridge; c.source_applied=true;
	assert(!template_hex("0000070700000a011200",c.desired.aa,10));
	strcpy(line,"bt"); assert(!command(&c,line,reply,sizeof(reply)) && !strncmp(reply,"OK state=unknown\t",17));
	/* MI split across two reads exactly as captured, then MB and the phone name. */
	assert(write(master,mi1,sizeof(mi1))==(ssize_t)sizeof(mi1));
	assert(!serial_frame(&c,&f,now_ms()+20));
	assert(write(master,mi2,sizeof(mi2))==(ssize_t)sizeof(mi2));
	assert(serial_frame(&c,&f,now_ms()+100)==1);
	assert(write(master,mb,sizeof(mb))==(ssize_t)sizeof(mb)); assert(serial_frame(&c,&f,now_ms()+100)==1);
	len=incoming(0xd4,"SAGalaxik\r\n",11,bytes); assert(write(master,bytes,len)==(ssize_t)len);
	assert(serial_frame(&c,&f,now_ms()+100)==1);
	strcpy(line,"bt"); assert(!command(&c,line,reply,sizeof(reply)));
	assert(!strcmp(reply,"OK state=paused\tconnections=off\tlength=149\tphone=Galaxik\t"
		"title=splin_-_pismo\tartist=Unbekannt\talbum=Download\n"));
	/* Truncation never leaves half a UTF-8 character; controls cannot break the line. */
	bt_field(c.bt_phone,4,(const uint8_t *)"a\xc3\xa4z",4); assert(!strcmp(c.bt_phone,"a\xc3\xa4"));
	bt_field(c.bt_phone,3,(const uint8_t *)"a\xc3\xa4z",4); assert(!strcmp(c.bt_phone,"a"));
	bt_field(c.bt_phone,8,(const uint8_t *)"a\tb",3); assert(!strcmp(c.bt_phone,"a?b"));
	child=fork(); assert(child>=0);
	if(!child) {
		uint8_t ack[8], zero=0; size_t size=encode(0xaa,&zero,1,ack);
		close(c.serial);
		expect_at(master,"AT#MB\r\n");
		f=radio_read(master); assert(f.cmd==0xaa && f.len==10 && !memcmp(f.payload,pause_aa,10));
		assert(write(master,ack,size)==(ssize_t)size);
		f=radio_read(master); assert(f.cmd==0xb8); size=encode(0xb8,&zero,1,ack); assert(write(master,ack,size)==(ssize_t)size);
		expect_at(master,"AT#MA\r\n");
		f=radio_read(master); assert(f.cmd==0xaa && f.len==10 && !memcmp(f.payload,play_aa,10));
		size=encode(0xaa,&zero,1,ack); assert(write(master,ack,size)==(ssize_t)size);
		f=radio_read(master); assert(f.cmd==0xb8); size=encode(0xb8,&zero,1,ack); assert(write(master,ack,size)==(ssize_t)size);
		f=radio_read(master); assert(f.cmd==0xb0 && f.len==5 && f.payload[0]==0xa0);
		expect_at(master,"AT#MD\r\n");
		_exit(0);
	}
	strcpy(line,"bt pause"); assert(!command(&c,line,reply,sizeof(reply)));
	strcpy(line,"bt play"); assert(!command(&c,line,reply,sizeof(reply)));
	strcpy(line,"bt next"); assert(!command(&c,line,reply,sizeof(reply)));
	assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
	/* After a wireless AA quit: AT#CD only, no anti-pop or volume frames. */
	child=fork(); assert(child>=0);
	if(!child) { close(c.serial); expect_at(master,"AT#CD\r\n"); _exit(0); }
	strcpy(line,"bt disconnect"); assert(!command(&c,line,reply,sizeof(reply)));
	assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
	/* BT Music open: AT#CC reconnects the last phone. */
	child=fork(); assert(child>=0);
	if(!child) { close(c.serial); expect_at(master,"AT#CC\r\n"); _exit(0); }
	strcpy(line,"bt connect"); assert(!command(&c,line,reply,sizeof(reply)));
	assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
	strcpy(line,"bt stop"); assert(command(&c,line,reply,sizeof(reply))<0);
	close(master); close(c.serial);
}
int main(void)
{
	bt_music_test();
	struct controller c={.timeout=300,.bluetooth=true};
	struct bt_bridge bridge={.listener=-1,.client=-1};
	struct frame f;
	uint8_t bytes[260],header[4]={1,BT_SEND,0,3};
	const uint8_t payload[]={0,4,0,5,8,1,16,0};
	int master,peers[2],status; size_t len; pid_t child;
	master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
	assert(master>=0 && !grantpt(master) && !unlockpt(master));
	c.serial=open_serial(ptsname(master)); assert(c.serial>=0);
	assert(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK,0,peers));
	bridge.client=peers[0]; c.bt=&bridge;
	len=incoming(0xd6,payload,sizeof(payload),bytes);
	assert(write(master,bytes,6)==6);
	assert(!serial_frame(&c,&f,now_ms()+5) && c.rx.n==6);
	assert(write(peers[1],header,4)==4 && write(peers[1],"abc",3)==3);
	child=fork(); assert(child>=0);
	if(!child) {
		uint8_t ack[260],zero=0; size_t size;
		close(c.serial); close(peers[0]); close(peers[1]);
		f=radio_read(master); assert(f.cmd==0xd6 && f.len==3 && !memcmp(f.payload,"abc",3));
		f=radio_read(master); assert(f.cmd==0xaa);
		assert(write(master,bytes+6,len-6)==(ssize_t)len-6);
		size=encode(0xaa,&zero,1,ack);
		assert(write(master,ack,size)==(ssize_t)size);
		_exit(0);
	}
	assert(!exchange(&c,0xaa,NULL,0,0));
	assert(!bt_io(&c,0,POLLOUT));
	expect_ipc(peers[1],BT_DATA,payload,sizeof(payload));
	assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
	/* MM name bytes match the firmware-derived SWL-BT command exactly. */
	strcpy(c.bt_name,"SWL-BT"); strcpy(c.bt_pin,"1234"); c.bt_config_pending=true;
	assert(!bt_configure(&c));
	{
		const uint8_t expected[]={2,0x80,0xd4,13,0x41,0x54,0x23,0x4d,0x4d,0x53,0x57,0x4c,0x2d,0x42,0x54,13,10,0x1b};
		uint8_t actual[sizeof(expected)]; exact(master,actual,sizeof(actual));
		assert(!memcmp(actual,expected,sizeof(expected)));
	}
	f=radio_read(master); assert(f.cmd==0xd4 && f.len==11 && !memcmp(f.payload,"AT#MN1234\r\n",11));
	len=incoming(0xd4,"SW\r\n",4,bytes); assert(write(master,bytes,len)==(ssize_t)len);
	assert(serial_frame(&c,&f,now_ms()+100)==1 && bridge.ready);
	assert(!bt_io(&c,0,POLLOUT)); expect_ipc(peers[1],BT_EVENT,"SW\r\n",4);
	len=incoming(0xd4,"IS\r\n",4,bytes); assert(write(master,bytes,len)==(ssize_t)len);
	assert(serial_frame(&c,&f,now_ms()+100)==1 && !bridge.ready && c.bt_config_pending && c.bt_power_pending);
	c.bt_power_pending=false; /* Re-assertion bytes are covered by service_test. */
	assert(!bt_io(&c,0,POLLOUT)); expect_ipc(peers[1],BT_EVENT,"IS\r\n",4);
	/* Unsolicited post-IS name report (stock capture: MMD-B500) is the radio's real name. */
	bt_text(&c,(uint8_t *)"MMD-B500\r\n",10); assert(!strcmp(c.bt_reported,"D-B500"));
	assert(!bt_io(&c,0,POLLOUT)); expect_ipc(peers[1],BT_EVENT,"MMD-B500\r\n",10);
	/* Status lines can span UART frames and share one payload. */
	bt_text(&c,(uint8_t *)"S",1); assert(!bridge.queued);
	bt_text(&c,(uint8_t *)"W\r\nIA\r\n",7); assert(!bridge.ready);
	assert(!bt_io(&c,0,POLLOUT));
	expect_ipc(peers[1],BT_EVENT,"SW\r\n",4); expect_ipc(peers[1],BT_EVENT,"IA\r\n",4);
	len=incoming(0xc3,"reset",5,bytes); assert(write(master,bytes,len)==(ssize_t)len);
	assert(serial_frame(&c,&f,now_ms()+100)==1 && c.reset_seen);
	assert(!bt_io(&c,0,POLLOUT)); expect_ipc(peers[1],BT_RESET,NULL,0);
	/* A partial pre-request ACK must not satisfy the next command. */
	len=encode(0xaa,(uint8_t[]){0},1,bytes); assert(write(master,bytes,4)==4);
	assert(!serial_frame(&c,&f,now_ms()+5));
	child=fork(); assert(child>=0);
	if(!child) {
		close(c.serial); close(peers[0]); close(peers[1]);
		f=radio_read(master); assert(f.cmd==0xaa);
		assert(write(master,bytes+4,len-4)==(ssize_t)len-4); _exit(0);
	}
	assert(exchange(&c,0xaa,NULL,0,0)<0);
	assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
	/* Oversized IPC is rejected before it can reach UART. */
	header[3]=241; assert(write(peers[1],header,4)==4);
	assert(!bt_io(&c,0,POLLIN) && bridge.client<0);
	close(peers[1]);
	assert(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK,0,peers)); bridge.client=peers[0];
	bridge.queued=sizeof(bridge.output)-3;
	bt_emit(&c,BT_DATA,(uint8_t *)"x",1); assert(bridge.client<0);
	close(peers[1]); close(master); close(c.serial);
	service_test();
	puts("PASS: Bluetooth IPC, partial UART retention, concurrent audio ACK, stale ACK rejection, name/PIN bytes, reset, bounds, backpressure and real service sockets/nonblocking CLI");
	return 0;
}
