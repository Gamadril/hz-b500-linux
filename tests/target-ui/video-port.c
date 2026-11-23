/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "videoOutPort.h"
#include "sunxi_display_v2.h"
#define RET_FAIL (-1)
#define DISP_DBG_LOG(...) ((void)0)
#define HD2CHN(handle) ((handle)/4)
#define HD2LYL(handle) ((handle)%4)
#define ZORDER_MIN 0
#include "port-zorder.inc"
static disp_layer_config2 current;
static int LayerGetPara(int fd,disp_layer_config2 *config) {
    assert(fd==42 && !config->channel && !config->layer_id);*config=current;return 0;
}
static int LayerSetPara(int fd,disp_layer_config2 *config) {
    assert(fd==42 && !config->channel && !config->layer_id);current=*config;return 0;
}
#include "port-set-zorder.inc"
#include "port-set-enable.inc"
int main(void) {
    dispOutPort port={.disp_fd=42,.hlayer=0,.rect={112,78,800,450}};
    assert(!DispSetZorder(&port,VIDEO_ZORDER_TOP));
    assert(current.info.zorder>16); /* actual BSP opaque framebuffer plane */
    assert(!DispSetEnable(&port,1));assert(current.enable && current.info.zorder>16);
    assert(current.info.screen_win.x==112 && current.info.screen_win.y==78);
    assert(current.info.screen_win.width==800 && current.info.screen_win.height==450);
    assert(!DispSetEnable(&port,0));assert(!current.enable);
    assert(!DispSetEnable(&port,1));assert(current.enable && current.info.zorder>16);
    puts("PASS: production disp2 TOP plane stays above framebuffer zorder 16 through hide/show and preserves the video viewport");
    return 0;
}
