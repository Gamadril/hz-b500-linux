#define _POSIX_C_SOURCE 200809L
#include "cedar_sink.h"
#include <stdbool.h>
#include <vdecoder.h>
#include <video/sunxi_display2.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>

/* ABI from this BSP's dev_composer.c, not the Android HWC commit ABI. */
enum { HWC_NEW_CLIENT = 1, HWC_DESTROY_CLIENT, HWC_ACQUIRE_FENCE, HWC_SUBMIT_FENCE };
struct sync_info { int fd; unsigned count; };

struct decoder_api {
    VideoDecoder *(*create)(void);
    void (*destroy)(VideoDecoder *);
    int (*init)(VideoDecoder *, VideoStreamInfo *, VConfig *);
    int (*request)(VideoDecoder *, int, char **, int *, char **, int *, int);
    int (*submit)(VideoDecoder *, VideoStreamDataInfo *, int);
    int (*decode)(VideoDecoder *, int, int, int, int64_t);
    VideoPicture *(*picture)(VideoDecoder *, int);
    int (*release)(VideoDecoder *, VideoPicture *);
    struct ScMemOpsS *(*memory)(void);
};
static struct decoder_api api;
static pthread_once_t load_once = PTHREAD_ONCE_INIT;
static char load_error[256];

static void load_decoder(void)
{
    /* Keep handles for process lifetime: the plugin registers function pointers globally. */
    void *v = dlopen("libvdecoder.so", RTLD_NOW | RTLD_GLOBAL);
    if (!v) { snprintf(load_error, sizeof(load_error), "libvdecoder.so: %s", dlerror()); return; }
    void *m = dlopen("libMemAdapter.so", RTLD_NOW | RTLD_GLOBAL);
    if (!m) { snprintf(load_error, sizeof(load_error), "libMemAdapter.so: %s", dlerror()); return; }
    void *h = dlopen("libawh264.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { snprintf(load_error, sizeof(load_error), "libawh264.so: %s", dlerror()); return; }
#define LOAD(field, handle, symbol) do { \
    *(void **)(&api.field) = dlsym(handle, symbol); \
    if (!api.field) { snprintf(load_error, sizeof(load_error), "missing %s", symbol); return; } \
} while (0)
    LOAD(create, v, "CreateVideoDecoder"); LOAD(destroy, v, "DestroyVideoDecoder");
    LOAD(init, v, "InitializeVideoDecoder"); LOAD(request, v, "RequestVideoStreamBuffer");
    LOAD(submit, v, "SubmitVideoStreamData"); LOAD(decode, v, "DecodeVideoStream");
    LOAD(picture, v, "RequestPicture"); LOAD(release, v, "ReturnPicture");
    LOAD(memory, m, "MemAdapterGetOpsS");
#undef LOAD
    void (*plugin)(void);
    *(void **)(&plugin) = dlsym(h, "CedarPluginVDInit");
    if (!plugin) { snprintf(load_error, sizeof(load_error), "missing CedarPluginVDInit"); return; }
    plugin();
}

struct aa_cedar {
    struct aa_cedar_config cfg;
    int disp, client, screen_width, screen_height;
    VideoDecoder *decoder;
    struct ScMemOpsS *memory;
    VideoPicture *front, *pending;
    struct sync_info front_fence, pending_fence;
    int dirty, faulted, visible;
};

static int fail(char *error, size_t size, const char *fmt, ...)
{
    va_list args; va_start(args, fmt); vsnprintf(error, size, fmt, args); va_end(args);
    return -1;
}
static int64_t now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static int hwc(struct aa_cedar *s, unsigned command, unsigned long arg)
{
    unsigned long args[4] = {s->cfg.screen, command, arg, 0};
    return ioctl(s->disp, DISP_HWC_COMMIT, args);
}
static int layer(struct aa_cedar *s, struct disp_layer_config2 *c, int command)
{
    unsigned long args[4] = {s->cfg.screen, (unsigned long)c, 1, 0};
    return ioctl(s->disp, command, args);
}
static int fence_wait(struct aa_cedar *s, int fd, char *error, size_t size)
{
    int64_t deadline = now_ms() + s->cfg.timeout_ms;
    for (;;) {
        int64_t left = deadline - now_ms();
        if (left <= 0) return fail(error, size, "display release fence timed out; buffers retained");
        struct pollfd p = {.fd = fd, .events = POLLIN};
        int r = poll(&p, 1, (int)left);
        if (r > 0 && (p.revents & POLLIN) && !(p.revents & (POLLERR | POLLNVAL))) return 0;
        if (r < 0 && errno == EINTR) continue;
        return fail(error, size, "display release fence failed: %s", r == 0 ? "timeout" : strerror(errno));
    }
}
static void release(struct aa_cedar *s, VideoPicture **p, struct sync_info *f)
{
    if (*p) { api.release(s->decoder, *p); *p = NULL; }
    if (f->fd >= 0) { close(f->fd); f->fd = -1; }
}

static int present(struct aa_cedar *s, VideoPicture *p, char *error, size_t size)
{
    s->pending = p;
    if (p->ePixelFormat != PIXEL_FORMAT_NV21 || p->b10BitPicFlag || p->bEnableAfbcFlag ||
        p->nLbcLossyComMod || p->nWidth <= 0 || p->nHeight <= 0 ||
        p->nLineStride != p->nWidth || p->nLeftOffset < 0 || p->nTopOffset < 0 ||
        p->nRightOffset > p->nWidth || p->nBottomOffset > p->nHeight ||
        p->nRightOffset <= p->nLeftOffset || p->nBottomOffset <= p->nTopOffset)
        return fail(error, size, "unsupported Cedar picture layout/crop (requires linear 8-bit NV21)");
    if (!p->pData0 || p->pData1 != p->pData0 + (size_t)p->nWidth * p->nHeight)
        return fail(error, size, "Cedar NV21 planes are not contiguous in one DMA buffer");
    int fd = s->memory->get_fd_by_vir(p->pData0);
    if (fd < 0) return fail(error, size, "decoder picture has no DMA-buf fd");
    struct disp_layer_config2 c = {0};
    c.channel = s->cfg.channel; c.layer_id = s->cfg.layer; c.enable = 1;
    /* The BSP framebuffer remains at z-order 16 while another fbdev client
     * owns it. Match libuapi's TOP plane so AA stays above that framebuffer. */
    c.info.mode = LAYER_MODE_BUFFER; c.info.zorder = 17;
    c.info.alpha_mode = 1; c.info.alpha_value = 255;
    c.info.fb.fd = fd; c.info.fb.format = DISP_FORMAT_YUV420_SP_VUVU;
    c.info.fb.color_space = DISP_BT601;
    if (p->matrix_coeffs == VIDEO_MATRIX_COEFFS_BT709)
        c.info.fb.color_space = p->video_full_range_flag == VIDEO_FULL_RANGE_FULL ? DISP_BT709_F : DISP_BT709;
    else if (p->video_full_range_flag == VIDEO_FULL_RANGE_FULL)
        c.info.fb.color_space = DISP_BT601_F;
    c.info.fb.size[0].width = p->nWidth; c.info.fb.size[0].height = p->nHeight;
    c.info.fb.size[1].width = p->nWidth / 2; c.info.fb.size[1].height = p->nHeight / 2;
    int w = p->nRightOffset - p->nLeftOffset, h = p->nBottomOffset - p->nTopOffset;
    int mw = (int)s->cfg.margin_width, mh = (int)s->cfg.margin_height;
    if (w <= mw || h <= mh) return fail(error, size, "Cedar picture %dx%d is smaller than its margins", w, h);
    w -= mw; h -= mh; /* the phone draws its UI into the centred area */
    c.info.fb.crop.x = (long long)(p->nLeftOffset + mw / 2) << 32;
    c.info.fb.crop.y = (long long)(p->nTopOffset + mh / 2) << 32;
    c.info.fb.crop.width = (long long)w << 32; c.info.fb.crop.height = (long long)h << 32;
    int dw = s->screen_width, dh = (int)((int64_t)dw * h / w);
    if (dh > s->screen_height) { dh = s->screen_height; dw = (int)((int64_t)dh * w / h); }
    c.info.screen_win.x = (s->screen_width - dw) / 2;
    c.info.screen_win.y = (s->screen_height - dh) / 2;
    c.info.screen_win.width = dw; c.info.screen_win.height = dh;
    if (hwc(s, HWC_ACQUIRE_FENCE, (unsigned long)&s->pending_fence) < 0 || s->pending_fence.fd < 0)
        return fail(error, size, "acquire display fence: %s", strerror(errno));
    /* From here, even an ioctl failure is treated as possibly changing scanout. */
    s->dirty = 1;
    if (layer(s, &c, DISP_LAYER_SET_CONFIG2) < 0 ||
        hwc(s, HWC_SUBMIT_FENCE, s->pending_fence.count) < 0)
        return fail(error, size, "submit display layer/fence: %s", strerror(errno));
    /* This BSP retires fence N only after submission N+1 reaches sync_finish. */
    if (s->front && fence_wait(s, s->front_fence.fd, error, size)) return -1;
    release(s, &s->front, &s->front_fence);
    s->front = s->pending; s->pending = NULL;
    s->front_fence = s->pending_fence; s->pending_fence.fd = -1;
    return 0;
}

static int pump(struct aa_cedar *s, char *error, size_t size)
{
    int64_t deadline = now_ms() + s->cfg.timeout_ms;
    do {
        int r = api.decode(s->decoder, 0, 0, 0, 0);
        VideoPicture *p;
        while ((p = api.picture(s->decoder, 0))) {
            if (!s->visible) api.release(s->decoder, p);
            else if (present(s, p, error, size)) return -1;
        }
        if (r == VDECODE_RESULT_NO_BITSTREAM || r == VDECODE_RESULT_OK) return 0;
        if (r < 0 || r == VDECODE_RESULT_RESOLUTION_CHANGE)
            return fail(error, size, "Cedar decode failed/requires reconfiguration: %d", r);
        if (r == VDECODE_RESULT_NO_FRAME_BUFFER)
            return fail(error, size, "Cedar exhausted frame buffers");
    } while (now_ms() < deadline);
    return fail(error, size, "Cedar decode made no bounded progress");
}

int aa_cedar_display_size(unsigned screen, uint32_t *width, uint32_t *height, char *error, size_t size)
{
    if (screen > 1) return fail(error, size, "invalid display index");
    int fd = open("/dev/disp", O_RDWR | O_CLOEXEC);
    if (fd < 0) return fail(error, size, "open /dev/disp: %s", strerror(errno));
    unsigned long args[4] = {screen, 0, 0, 0};
    int w = ioctl(fd, DISP_GET_SCN_WIDTH, args);
    int h = ioctl(fd, DISP_GET_SCN_HEIGHT, args);
    close(fd);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return fail(error, size, "invalid display dimensions");
    *width = (uint32_t)w; *height = (uint32_t)h;
    return 0;
}

struct aa_cedar *aa_cedar_open(const struct aa_cedar_config *cfg, char *error, size_t size)
{
    int sized = (cfg && cfg->width == 800 && cfg->height == 480) ||
                (cfg && cfg->width == 1280 && cfg->height == 720);
    if (!cfg || cfg->screen > 1 || cfg->channel > 3 || cfg->layer > 3 || !sized ||
        cfg->margin_width % 2 || cfg->margin_height % 2 ||
        cfg->margin_width >= cfg->width || cfg->margin_height >= cfg->height ||
        cfg->vbv_bytes < 256*1024 ||
        cfg->vbv_bytes > 4*1024*1024 || cfg->timeout_ms < 50 || cfg->timeout_ms > 5000) {
        fail(error, size, "invalid configuration (800x480 or 1280x720, even margins inside the frame)"); return NULL;
    }
    pthread_once(&load_once, load_decoder);
    if (*load_error) { fail(error, size, "%s", load_error); return NULL; }
    struct aa_cedar *s = calloc(1, sizeof(*s));
    if (!s) { fail(error, size, "allocate Cedar context"); return NULL; }
    s->cfg = *cfg; s->disp = -1; s->visible = 1; s->front_fence.fd = s->pending_fence.fd = -1;
    s->disp = open("/dev/disp", O_RDWR | O_CLOEXEC);
    if (s->disp < 0) { fail(error, size, "open /dev/disp: %s", strerror(errno)); goto bad; }
    struct disp_layer_config2 saved = {.channel = cfg->channel, .layer_id = cfg->layer};
    if (layer(s, &saved, DISP_LAYER_GET_CONFIG2) < 0 || saved.enable) {
        fail(error, size, "video layer unavailable or already enabled; stop the other renderer"); goto bad;
    }
    unsigned long args[4] = {cfg->screen, 0, 0, 0};
    s->screen_width = ioctl(s->disp, DISP_GET_SCN_WIDTH, args);
    s->screen_height = ioctl(s->disp, DISP_GET_SCN_HEIGHT, args);
    if (s->screen_width <= 0 || s->screen_height <= 0 || s->screen_width > 4096 || s->screen_height > 4096) {
        fail(error, size, "invalid display dimensions"); goto bad;
    }
    /* Some BSP ioctl dispatchers return success for unknown commands. Require
     * the HWC_NEW_CLIENT output to change too, before touching a video layer. */
    int rate = -1;
    if (hwc(s, HWC_NEW_CLIENT, (unsigned long)&rate) < 0 || rate <= 0) {
        fail(error, size, "Sunxi HWC release-fence API unavailable"); goto bad;
    }
    s->client = 1;
    s->memory = api.memory();
    if (!s->memory || !s->memory->get_fd_by_vir) { fail(error, size, "missing DMA-buf memory operations"); goto bad; }
    s->decoder = api.create();
    if (!s->decoder) { fail(error, size, "CreateVideoDecoder failed"); goto bad; }
    VideoStreamInfo info = {0}; VConfig v = {0};
    info.eCodecFormat = VIDEO_CODEC_FORMAT_H264; info.nWidth = cfg->width; info.nHeight = cfg->height;
    info.nFrameRate = 30000; info.nFrameDuration = 33333;
    /* Stream mode accepts SPS/PPS and access units split across AA packets. */
    info.bIsFramePackage = 0;
    v.memops = s->memory; v.eOutputPixelFormat = PIXEL_FORMAT_NV21;
    v.nVbvBufferSize = cfg->vbv_bytes; v.nDisplayHoldingFrameBufferNum = 2;
    if (api.init(s->decoder, &info, &v) != 0) {
        fail(error, size, "InitializeVideoDecoder failed"); goto bad;
    }
    return s;
bad:
    /* No picture has been submitted to display yet. */
    if (s->decoder) api.destroy(s->decoder);
    if (s->client) hwc(s, HWC_DESTROY_CLIENT, 0);
    if (s->disp >= 0) close(s->disp);
    free(s); return NULL;
}

int aa_cedar_push(struct aa_cedar *s, const uint8_t *data, size_t len, int64_t pts, char *error, size_t size)
{
    if (!s || s->faulted) return fail(error, size, "Cedar sink is faulted/closed");
    if (!data || !len || len > s->cfg.vbv_bytes || len > INT_MAX)
        return fail(error, size, "invalid/oversized H.264 packet");
    char *buf = NULL, *wrap = NULL; int first = 0, second = 0;
    if (api.request(s->decoder, (int)len, &buf, &first, &wrap, &second, 0) != 0) {
        if (pump(s, error, size)) goto fault;
        if (api.request(s->decoder, (int)len, &buf, &first, &wrap, &second, 0) != 0) {
            fail(error, size, "Cedar bitstream buffer full"); goto fault;
        }
    }
    if (first < 0 || second < 0 || (size_t)first + (size_t)second < len ||
        (first && !buf) || ((size_t)first < len && !wrap)) {
        fail(error, size, "invalid Cedar ring buffer"); goto fault;
    }
    size_t n = len < (size_t)first ? len : (size_t)first;
    if (n) memcpy(buf, data, n);
    if (len > n) memcpy(wrap, data + n, len - n);
    VideoStreamDataInfo info = {0};
    info.pData = buf; info.nLength = (int)len; info.nPts = pts; info.nPcr = -1;
    info.bIsFirstPart = info.bIsLastPart = info.bValid = 1;
    if (api.submit(s->decoder, &info, 0) != 0) { fail(error, size, "SubmitVideoStreamData failed"); goto fault; }
    if (pump(s, error, size)) goto fault;
    return 0;
fault:
    s->faulted = 1; return -1;
}

static int retire_display(struct aa_cedar *s, char *error, size_t size)
{
    if (!s) return 0;
    if (s->dirty) {
        struct disp_layer_config2 disabled = {.channel = s->cfg.channel, .layer_id = s->cfg.layer};
        struct sync_info retire = {.fd = -1};
        /* A later submission retires both the old and the possibly submitted pending frame. */
        if (hwc(s, HWC_ACQUIRE_FENCE, (unsigned long)&retire) < 0 || retire.fd < 0 ||
            layer(s, &disabled, DISP_LAYER_SET_CONFIG2) < 0 ||
            hwc(s, HWC_SUBMIT_FENCE, retire.count) < 0) {
            if (retire.fd >= 0) close(retire.fd);
            return fail(error, size, "cannot disable/retire video layer; buffers retained");
        }
        int r = 0;
        if (s->front) r = fence_wait(s, s->front_fence.fd, error, size);
        if (!r && s->pending && s->pending_fence.fd >= 0) r = fence_wait(s, s->pending_fence.fd, error, size);
        close(retire.fd);
        if (r) return -1;
    }
    release(s, &s->front, &s->front_fence); release(s, &s->pending, &s->pending_fence);
    s->dirty = 0;
    return 0;
}

int aa_cedar_set_visible(struct aa_cedar *s, int visible, char *error, size_t size)
{
    if (!s || s->faulted) return fail(error, size, "Cedar sink is faulted/closed");
    if (!visible && retire_display(s, error, size)) { s->faulted = 1; return -1; }
    s->visible = !!visible;
    return 0;
}

int aa_cedar_close(struct aa_cedar *s, char *error, size_t size)
{
    if (!s) return 0;
    if (retire_display(s, error, size)) return -1;
    api.destroy(s->decoder);
    if (s->client) hwc(s, HWC_DESTROY_CLIENT, 0);
    close(s->disp); free(s); return 0;
}
