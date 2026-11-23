/* SPDX-License-Identifier: GPL-2.0-only */
#include <libubus.h>
#include <libubox/blobmsg.h>
#include <assert.h>
#include <stdlib.h>
int main(int argc,char **argv) {
    assert(argc==3);struct ubus_context *c=ubus_connect(argv[1]);assert(c);
    uint32_t id;assert(!ubus_lookup_id(c,"vehicle",&id));
    struct blob_buf b={0};
    for(int i=0;i<atoi(argv[2]);i++) for(int edge=0;edge<2;edge++) {
        blob_buf_init(&b,0);blobmsg_add_u32(&b,"id",0x1d6);blobmsg_add_string(&b,"data",edge?"C00C":"E00C");
        assert(!ubus_invoke(c,id,"inject",b.head,NULL,NULL,1000));
    }
    blob_buf_free(&b);ubus_free(c);return 0;
}
