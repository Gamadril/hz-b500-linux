/* Exercises the production bridge against the real SDK ABI and deterministic
 * decoder/ioctl mocks. No device access or vendor binary execution. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdarg.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>
int fake_open(const char *, int, ...);
int fake_close(int);
int fake_ioctl(int, unsigned long, ...);
int fake_poll(struct pollfd *, nfds_t, int);
int fake_once(pthread_once_t *, void (*)(void));
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define poll fake_poll
#define pthread_once fake_once
#include "../../native/cedar_sink.c"
#undef open
#undef close
#undef ioctl
#undef poll
#undef pthread_once

static int busy, init_error, poll_timeout, invalid_ring, pictures, available;
static int no_composer;
static int held, destroyed, submitted, sequence, last_acquired, set_calls, create_calls;
static int owners[64], retired[16], displayed[16];
static char head[16], tail[16], image[16][800 * 480 * 3 / 2];
static VideoPicture frames[16];
static unsigned char copied[6];
static int request_calls, packet_calls, last_pts;

int fake_once(pthread_once_t *once, void (*fn)(void)) { (void)once; (void)fn; return 0; }
int fake_open(const char *path, int flags, ...)
{ assert(!strcmp(path, "/dev/disp")); assert(flags & O_CLOEXEC); return 11; }
int fake_close(int fd) { assert(fd >= 0); return 0; }
int fake_ioctl(int fd, unsigned long cmd, ...)
{
    assert(fd == 11);
    va_list v; va_start(v, cmd); unsigned long *a = va_arg(v, unsigned long *); va_end(v);
    if (cmd == DISP_GET_SCN_WIDTH) return 1024;
    if (cmd == DISP_GET_SCN_HEIGHT) return 600;
    if (cmd == DISP_LAYER_GET_CONFIG2) {
        struct disp_layer_config2 *c = (void *)a[1]; c->enable = busy; return 0;
    }
    if (cmd == DISP_LAYER_SET_CONFIG2) {
        struct disp_layer_config2 *c = (void *)a[1];
        set_calls++;
        if (c->enable) {
            assert(c->info.zorder == 17 && c->info.alpha_mode == 1 && c->info.alpha_value == 255);
            assert(c->info.fb.format == DISP_FORMAT_YUV420_SP_VUVU);
            assert(c->info.screen_win.x == 12 && c->info.screen_win.y == 0);
            assert(c->info.screen_win.width == 1000 && c->info.screen_win.height == 600);
            assert(c->info.fb.size[0].width == 800 && c->info.fb.size[1].width == 400);
            assert(c->info.fb.crop.width == (long long)800 << 32);
            int id = c->info.fb.fd; /* Fake FD is also the picture id, including zero. */
            assert(id >= 0 && id < 16);
            owners[last_acquired] = id; displayed[id] = 1;
        }
        return 0;
    }
    assert(cmd == DISP_HWC_COMMIT);
    if (no_composer) return 0; /* Unknown ioctl can falsely report success. */
    switch (a[1]) {
    case HWC_NEW_CLIENT: *(int *)a[2] = 254000000; return 0;
    case HWC_DESTROY_CLIENT: return 0;
    case HWC_ACQUIRE_FENCE: {
        struct sync_info *f = (void *)a[2];
        last_acquired = ++sequence; f->count = sequence; f->fd = sequence + 1000; return 0;
    }
    case HWC_SUBMIT_FENCE: submitted = (int)a[2]; return 0;
    default: abort();
    }
}
int fake_poll(struct pollfd *p, nfds_t n, int timeout)
{
    assert(n == 1 && timeout > 0);
    int seq = p->fd - 1000;
    assert(seq > 0);
    if (poll_timeout || seq >= submitted) return 0;
    for (int i = 1; i < submitted; ++i)
        if (owners[i] >= 0) retired[owners[i]] = 1;
    p->revents = POLLIN; return 1;
}
static VideoDecoder *create(void) { create_calls++; return (VideoDecoder *)frames; }
static void destroy(VideoDecoder *d) { (void)d; assert(held == 0); destroyed++; }
static struct ScMemOpsS memory_ops;
static struct ScMemOpsS *memory(void) { return &memory_ops; }
static int get_fd(void *p)
{
    for (int i = 0; i < 16; ++i) if (p == image[i]) return i;
    return -1;
}
static int initialize(VideoDecoder *d, VideoStreamInfo *info, VConfig *v)
{
    (void)d;
    assert(info->eCodecFormat == VIDEO_CODEC_FORMAT_H264 && !info->bIsFramePackage);
    assert(info->nWidth == 800 && info->nHeight == 480 && info->nFrameRate == 30000);
    assert(v->eOutputPixelFormat == PIXEL_FORMAT_NV21 && v->nDisplayHoldingFrameBufferNum == 2);
    assert(!v->bIsSoftDecoderFlag && !v->bThumbnailMode);
    return init_error ? -1 : 0;
}
static int request(VideoDecoder *d, int len, char **b, int *n, char **w, int *nw, int stream)
{
    (void)d; assert(stream == 0 && len == 6); request_calls++;
    *b = head; *w = tail; *n = invalid_ring ? -1 : 2; *nw = 4; return 0;
}
static int submit(VideoDecoder *d, VideoStreamDataInfo *info, int stream)
{
    (void)d; assert(stream == 0 && info->nLength == 6);
    assert(info->bIsFirstPart && info->bIsLastPart && info->bValid);
    assert(info->pData == head);
    memcpy(copied, head, 2); memcpy(copied + 2, tail, 4);
    last_pts = (int)info->nPts; packet_calls++; available = 1; return 0;
}
static int decode(VideoDecoder *d, int eos, int key, int drop, int64_t now)
{ (void)d; assert(!eos && !key && !drop && !now); return available ? 1 : VDECODE_RESULT_NO_BITSTREAM; }
static VideoPicture *picture(VideoDecoder *d, int stream)
{
    (void)d; assert(stream == 0);
    if (!available) return NULL;
    available = 0;
    int i = pictures++; assert(i < 16);
    frames[i] = (VideoPicture){.nID = i, .ePixelFormat = PIXEL_FORMAT_NV21,
        .nWidth = 800, .nHeight = 480, .nLineStride = 800,
        .nRightOffset = 800, .nBottomOffset = 480,
        .pData0 = image[i], .pData1 = image[i] + 800*480};
    held++; return &frames[i];
}
static int give_back(VideoDecoder *d, VideoPicture *p)
{
    (void)d;
    /* Catch decoder reuse before a real later display fence has signaled. */
    assert(!displayed[p->nID] || retired[p->nID]); held--; assert(held >= 0); return 0;
}
static void reset(void)
{
    busy = init_error = poll_timeout = invalid_ring = pictures = available = 0;
    no_composer = 0;
    held = destroyed = submitted = sequence = last_acquired = set_calls = create_calls = 0;
    request_calls = packet_calls = last_pts = 0;
    memset(retired, 0, sizeof(retired)); memset(displayed, 0, sizeof(displayed));
    for (int i = 0; i < 64; ++i) owners[i] = -1;
    memory_ops.get_fd_by_vir = get_fd;
    api = (struct decoder_api){.create=create, .destroy=destroy, .init=initialize,
        .request=request, .submit=submit, .decode=decode, .picture=picture,
        .release=give_back, .memory=memory};
}
static const struct aa_cedar_config cfg = {0, 0, 0, 800, 480, 1024*1024, 500, 0, 0};
static const uint8_t packet[] = {0,0,0,1,0x65,0x80};
int main(void)
{
    char error[256]; struct aa_cedar *s;
    reset();
    uint32_t width, height;
    assert(!aa_cedar_display_size(0, &width, &height, error, sizeof(error)));
    assert(width == 1024 && height == 600 && !set_calls && !create_calls);
    assert(aa_cedar_display_size(2, &width, &height, error, sizeof(error)) < 0);
    s = aa_cedar_open(&cfg, error, sizeof(error)); assert(s);
    assert(!aa_cedar_push(s, packet, sizeof(packet), 123456, error, sizeof(error)));
    assert(last_pts == 123456 && !memcmp(copied, packet, 6) && held == 1);
    assert(!aa_cedar_push(s, packet, sizeof(packet), -1, error, sizeof(error)));
    assert(held == 1 && retired[0] && !retired[1]);
    assert(!aa_cedar_set_visible(s, 0, error, sizeof(error)) && held == 0);
    assert(!aa_cedar_push(s, packet, sizeof(packet), -1, error, sizeof(error)) && held == 0);
    assert(!aa_cedar_set_visible(s, 1, error, sizeof(error)));
    assert(!aa_cedar_push(s, packet, sizeof(packet), -1, error, sizeof(error)) && held == 1);
    assert(!aa_cedar_close(s, error, sizeof(error)) && held == 0 && destroyed == 1);

    reset(); s = aa_cedar_open(&cfg, error, sizeof(error)); assert(s);
    assert(!aa_cedar_push(s, packet, 6, 0, error, sizeof(error)));
    poll_timeout = 1;
    assert(aa_cedar_push(s, packet, 6, 0, error, sizeof(error)) < 0);
    assert(held == 2 && destroyed == 0);
    assert(aa_cedar_close(s, error, sizeof(error)) < 0 && held == 2 && destroyed == 0);
    poll_timeout = 0;
    assert(!aa_cedar_close(s, error, sizeof(error)) && held == 0 && destroyed == 1);

    reset(); busy = 1;
    assert(!aa_cedar_open(&cfg, error, sizeof(error)) && !set_calls && !create_calls);
    reset(); no_composer = 1;
    assert(!aa_cedar_open(&cfg, error, sizeof(error)) && !set_calls && !create_calls);
    reset(); init_error = 1;
    assert(!aa_cedar_open(&cfg, error, sizeof(error)) && !set_calls && destroyed == 1);
    reset(); s = aa_cedar_open(&cfg, error, sizeof(error)); assert(s);
    assert(aa_cedar_push(s, packet, 1024*1024+1, 0, error, sizeof(error)) < 0);
    assert(!request_calls && !packet_calls);
    invalid_ring = 1;
    assert(aa_cedar_push(s, packet, 6, 0, error, sizeof(error)) < 0 && !packet_calls);
    assert(!aa_cedar_close(s, error, sizeof(error)));
    puts("PASS: SDK ABI, ring wrap, fd zero, crop/scale, video above framebuffer, display retirement, focus, timeout retention and failure cleanup");
    return 0;
}
