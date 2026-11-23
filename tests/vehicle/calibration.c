#define main vehicle_slcan_main
#include "vehicle_slcan.c"
#undef main
#include "calibration-fixture.h"
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
static struct calibration_fixture fixture=CALIBRATION_FIXTURE_INIT;
static int tty_fd=-1,master_fd;
static unsigned detaches,attaches;
int __real_ioctl(int,unsigned long,...);
int __wrap_ioctl(int fd,unsigned long request,...) {
    va_list args;va_start(args,request);void *value=va_arg(args,void *);va_end(args);
    if(fd==tty_fd && request==TIOCSETD) {
        if(*(int *)value==N_SLCAN)attaches++;else {assert(*(int *)value==N_TTY);detaches++;}
        return 0;
    }
    if(fd==tty_fd && request==SIOCGIFNAME) {strcpy(value,"can0");return 0;}
    if(request==SIOCGIFFLAGS) {((struct ifreq *)value)->ifr_flags=IFF_UP;return 0;}
    if(request==SIOCSIFFLAGS) {assert(((struct ifreq *)value)->ifr_flags&IFF_UP);return 0;}
    return __real_ioctl(fd,request,value);
}
static void *adapter(void *unused) {
    (void)unused;char command[VEHICLE_CAL_COMMAND_SIZE];size_t used=0;
    for(;;) {
        struct pollfd p={.fd=master_fd,.events=POLLIN};if(poll(&p,1,200)<=0)continue;
        char c;if(read(master_fd,&c,1)!=1)continue;
        if(c=='\n')continue;
        if(c!='\r') {assert(used+1<sizeof(command));command[used++]=c;continue;}
        command[used]=0;used=0;char reply[VEHICLE_CAL_REPLY_SIZE];fixture_reply(&fixture,command,reply);
        if(!strcmp(command,"GB") && !strcmp(reply,"\r"))strcpy(reply,"\rt1D2131\r\r");
        for(size_t i=0;i<strlen(reply);i++) {assert(write(master_fd,reply+i,1)==1);usleep(500);}
    }
    return NULL;
}
static void parser_checks(void) {
    struct vehicle_cal_table table={0};
    assert(!vehicle_cal_parse("GP","GP1,12.500,-4.250\r",&table));
    assert(table.fresh && table.forward==12500 && table.side==-4250);
    const char *bad[]={"GP1,NaN,0.000\r","GP1,1.00,2.000\r","GP1,1.000,2.000\rextra","GP1,99999999999999.000,0.000\r","GP1,1.000,2.000"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        struct vehicle_cal_table before=table;
        assert(vehicle_cal_parse("GP",bad[i],&table)==VEHICLE_CAL_PROTOCOL);
        assert(!memcmp(&before,&table,sizeof(table)));
    }
    assert(!vehicle_cal_parse("GP","GP0\r",&table) && !table.fresh);
    assert(vehicle_cal_parse("GC1","\a",&table)==VEHICLE_CAL_REJECTED);
    assert(!vehicle_cal_command("GC7") && !vehicle_cal_command("gb") && !vehicle_cal_command("GS\r"));
    fixture.active=true;char reply[VEHICLE_CAL_REPLY_SIZE];fixture_reply(&fixture,"GL",reply);fixture.active=false;
    assert(!vehicle_cal_parse("GL",reply,&table));
    reply[2]='1';assert(vehicle_cal_parse("GL",reply,&table)==VEHICLE_CAL_PROTOCOL);
    const char *valid[]={"GT","GTR,0.001,180","GTN,8,10","GT6,3.12,5.123","GT1,180,180"};
    for(unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);i++)assert(vehicle_cal_command(valid[i]));
    const char *invalid[]={"GT7,1,2","GT1,0,2","GT1,3,2","GT1,-1,2","GT1,+1,2","GT1,1,180.001",
        "GT1,1e0,2","GT1,NaN,2","GT1,1,inf","GT1,1.0001,2","GT1, 1,2","GT1,1,2,3","GT1,1,2x","GT1,1,2\r","GT1,1.,2"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)assert(!vehicle_cal_command(invalid[i]));
    fixture.active=true;fixture_reply(&fixture,"GT",reply);fixture.active=false;
    assert(!vehicle_cal_parse("GT",reply,&table) && table.thresholds_known && table.thresholds[7].exit==10000);
    struct vehicle_cal_table before=table;
    reply[strlen(reply)-1]=0;assert(vehicle_cal_parse("GT",reply,&table)==VEHICLE_CAL_PROTOCOL);
    assert(!memcmp(&before,&table,sizeof(table))); /* No partial threshold tables. */
    char valid_reply[VEHICLE_CAL_REPLY_SIZE];fixture.active=true;fixture_reply(&fixture,"GT",valid_reply);fixture.active=false;
    const char *rest=strchr(valid_reply,'\r')+1;
    const char *bad_records[]={"GTN,8.000,10.000\r","GTR,0.000,10.000\r","GTR,11.000,10.000\r",
        "GTR,-1.000,10.000\r","GTR,8.000,180.001\r","GTR,8.000,10.000x\r"};
    for(unsigned i=0;i<sizeof(bad_records)/sizeof(bad_records[0]);i++) {
        snprintf(reply,sizeof(reply),"%s%s",bad_records[i],rest);
        assert(vehicle_cal_parse("GT",reply,&table)==VEHICLE_CAL_PROTOCOL && !memcmp(&before,&table,sizeof(table)));
    }
    puts("PASS: calibration GP/GL/GT fixed-point parsing, ordered transactional tables and threshold command validation");
}
static void overlap_checks(void) {
    struct vehicle_cal_table table={.positions_known=true,.thresholds_known=true};
    table.positions[0]=(struct vehicle_cal_position){true,179000,0};
    table.positions[1]=(struct vehicle_cal_position){true,-179000,0};
    table.thresholds[0]=(struct vehicle_cal_threshold){500,1500};
    table.thresholds[1]=(struct vehicle_cal_threshold){500,1000};
    struct vehicle_cal_overlap overlap=vehicle_cal_overlaps(&table,0);
    assert(!overlap.entry && overlap.exit==2 && !overlap.center); /* Wrapped distance 2 degrees; tangent exit/entry. */
    overlap=vehicle_cal_overlaps(&table,1);assert(!overlap.entry && !overlap.exit && !overlap.center);
    table.thresholds[0].entry=1500;overlap=vehicle_cal_overlaps(&table,0);assert(overlap.entry==2);
    table.thresholds[0].exit=2000;overlap=vehicle_cal_overlaps(&table,0);assert(overlap.center==2);
    table.positions[1].side=3000;overlap=vehicle_cal_overlaps(&table,0);assert(!overlap.entry && !overlap.exit && !overlap.center);
    table.positions[1]=table.positions[0];overlap=vehicle_cal_overlaps(&table,0);assert(overlap.center==2);
    table.positions[1].set=false;overlap=vehicle_cal_overlaps(&table,0);assert(!overlap.exit);
    table.positions[1].set=true;table.thresholds_known=false;
    overlap=vehicle_cal_overlaps(&table,0);assert(!overlap.exit);
    table.thresholds_known=true;table.positions_known=false;
    overlap=vehicle_cal_overlaps(&table,0);assert(!overlap.exit);
    puts("PASS: wrapped two-axis overlap, tangency, directional exit/entry conflict, center lock and unset/unknown exclusion");
}
struct server_context { int listener;struct serial_owner *owner; };
static void *server(void *arg) {
    struct server_context *context=arg;
    for(unsigned i=0;i<5;i++) {
        struct pollfd p={.fd=context->listener,.events=POLLIN};assert(poll(&p,1,5000)>0);
        control_client(context->listener,context->owner);
    }
    return NULL;
}
int main(int argc,char **argv) {
    assert(argc==2);parser_checks();overlap_checks();
    master_fd=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);assert(master_fd>=0);
    assert(!grantpt(master_fd) && !unlockpt(master_fd));
    int slave=open(ptsname(master_fd),O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);assert(slave>=0);
    struct termios2 raw;assert(!ioctl(slave,TCGETS2,&raw));raw.c_iflag=raw.c_oflag=raw.c_lflag=0;
    raw.c_cflag=BOTHER|CS8|CREAD|CLOCAL;raw.c_ispeed=raw.c_ospeed=750000;raw.c_cc[VMIN]=1;raw.c_cc[VTIME]=0;
    assert(!ioctl(slave,TCSETS2,&raw));tty_fd=slave;
    pthread_t adapter_thread;assert(!pthread_create(&adapter_thread,NULL,adapter,NULL));
    struct serial_owner owner={.fd=slave,.net=999,.attached=true,.name="can0"};
    char reply[VEHICLE_CAL_REPLY_SIZE];struct vehicle_cal_table table={0};
    assert(!calibration(&owner,"GB",reply,sizeof(reply)) && !owner.attached && detaches==1);
    assert(!calibration(&owner,"GL",reply,sizeof(reply)) && !vehicle_cal_parse("GL",reply,&table));
    assert(!calibration(&owner,"GT",reply,sizeof(reply)) && !vehicle_cal_parse("GT",reply,&table));
    assert(!calibration(&owner,"GT1,3.000,5.000",reply,sizeof(reply))); /* Freshness is not required. */
    assert(calibration(&owner,"GS",reply,sizeof(reply))==VEHICLE_CAL_REJECTED && !owner.attached);
    pthread_mutex_lock(&fixture.mutex);fixture.fresh=false;pthread_mutex_unlock(&fixture.mutex);
    assert(!calibration(&owner,"GP",reply,sizeof(reply)) && !strcmp(reply,"GP0\r"));
    assert(calibration(&owner,"GC1",reply,sizeof(reply))==VEHICLE_CAL_REJECTED);
    pthread_mutex_lock(&fixture.mutex);fixture.fresh=true;fixture.reject_capture=true;pthread_mutex_unlock(&fixture.mutex);
    assert(calibration(&owner,"GC1",reply,sizeof(reply))==VEHICLE_CAL_REJECTED);
    pthread_mutex_lock(&fixture.mutex);fixture.reject_capture=false;pthread_mutex_unlock(&fixture.mutex);
    assert(!calibration(&owner,"GC1",reply,sizeof(reply)));
    assert(!calibration(&owner,"GL",reply,sizeof(reply)) && !vehicle_cal_parse("GL",reply,&table));
    assert(table.positions[2].set && table.positions[2].forward==12500 && table.positions[2].side==-4250);
    assert(!calibration(&owner,"GT",reply,sizeof(reply)) && !vehicle_cal_parse("GT",reply,&table));
    assert(table.thresholds[2].entry==3000 && table.thresholds[2].exit==5000); /* Capture preserves radii. */
    pthread_mutex_lock(&fixture.mutex);fixture.reject_save=true;pthread_mutex_unlock(&fixture.mutex);
    assert(calibration(&owner,"GS",reply,sizeof(reply))==VEHICLE_CAL_REJECTED && !owner.attached);
    pthread_mutex_lock(&fixture.mutex);fixture.reject_save=false;pthread_mutex_unlock(&fixture.mutex);
    assert(!calibration(&owner,"GS",reply,sizeof(reply)) && owner.attached && attaches==1);
    assert(!calibration(&owner,"GB",reply,sizeof(reply)));
    assert(!calibration(&owner,"GT1,4.000,6.000",reply,sizeof(reply)));
    assert(!calibration(&owner,"GC2",reply,sizeof(reply)));
    assert(!calibration(&owner,"GX",reply,sizeof(reply)) && owner.attached);
    pthread_mutex_lock(&fixture.mutex);
    assert(fixture.saved[2].set && !fixture.saved[3].set && fixture.saves==1 && fixture.saved_thresholds[2].entry==3000);
    fixture.active=true;pthread_mutex_unlock(&fixture.mutex);
    assert(calibration(&owner,"GB",reply,sizeof(reply))==VEHICLE_CAL_REJECTED && !owner.attached);
    assert(!calibration(&owner,"GL",reply,sizeof(reply))); /* Resume a surviving session. */
    pthread_mutex_lock(&fixture.mutex);fixture.drop=true;pthread_mutex_unlock(&fixture.mutex);
    assert(vehicle_cal_serial(slave,"GP",reply,sizeof(reply),20)==VEHICLE_CAL_TRANSPORT);
    assert(!calibration(&owner,"GP",reply,sizeof(reply))); /* Tagged read recovers the stream. */
    assert(!calibration(&owner,"GX",reply,sizeof(reply)) && owner.attached);
    int listener=control_socket(argv[1]);assert(listener>=0);
    struct server_context context={listener,&owner};pthread_t server_thread;
    assert(!pthread_create(&server_thread,NULL,server,&context));
    assert(!vehicle_cal_request(argv[1],"GB",reply,sizeof(reply),5000));
    assert(!vehicle_cal_request(argv[1],"GP",reply,sizeof(reply),5000));
    assert(!vehicle_cal_request(argv[1],"GTR,2.125,4.250",reply,sizeof(reply),5000));
    assert(!vehicle_cal_request(argv[1],"GT",reply,sizeof(reply),5000));
    assert(!vehicle_cal_parse("GT",reply,&table) && table.thresholds[0].entry==2125 && table.thresholds[0].exit==4250);
    assert(!vehicle_cal_request(argv[1],"GX",reply,sizeof(reply),5000));
    pthread_join(server_thread,NULL);close(listener);assert(!unlink(argv[1]));
    assert(owner.attached);
    puts("PASS: exclusive UART owner, queued SLCAN/CR boundary, fragmented replies, GP0/BEL, capture/save/cancel, restart recovery and socket control");
    return 0;
}
