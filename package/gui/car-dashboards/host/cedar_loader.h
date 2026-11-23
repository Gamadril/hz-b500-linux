/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_CEDAR_LOADER_H
#define HZ_CEDAR_LOADER_H
#include <dlfcn.h>
/* Keep Cedar and its plugin constructors outside the ELF startup dependency
 * graph. Load the same installed API on the video worker's first Play only. */
#include "cedar_symbols.h"
static struct {
#define HZ_DECLARE(field,name) __typeof__(&name) field;
    HZ_CEDAR_API(HZ_DECLARE)
#undef HZ_DECLARE
} cedar;
static void *cedar_library;
static bool cedar_loaded;
static bool load_cedar(void) {
    if(cedar_loaded)return true;
    if(!cedar_library)cedar_library=dlopen("libtplayer.so",RTLD_NOW|RTLD_GLOBAL);
    if(!cedar_library) {fprintf(stderr,"Video: cannot load libtplayer: %s\n",dlerror());return false;}
#define HZ_RESOLVE(field,name) \
    cedar.field=(__typeof__(cedar.field))dlsym(cedar_library,#name); \
    if(!cedar.field) {fprintf(stderr,"Video: missing Cedar API %s\n",#name);return false;}
    HZ_CEDAR_API(HZ_RESOLVE)
#undef HZ_RESOLVE
    cedar_loaded=true;return true;
    /* Keep the library mapped for the process lifetime: Cedar constructors
     * register plugin callbacks whose addresses must remain valid. */
}
#define TPlayerCreate cedar.create
#define TPlayerDestroy cedar.destroy
#define TPlayerStop cedar.stop
#define TPlayerSetVideoDisplay cedar.display
#define TPlayerSetNotifyCallback cedar.notify
#define TPlayerSetHoldLastPicture cedar.hold
#define TPlayerSetDataSource cedar.source
#define TPlayerPrepare cedar.prepare
#define TPlayerSetDisplayRect cedar.rect
#define TPlayerGetDuration cedar.duration
#define TPlayerSeekTo cedar.seek
#define TPlayerStart cedar.start
#define TPlayerPause cedar.pause
#define TPlayerGetCurrentPosition cedar.position
#endif
