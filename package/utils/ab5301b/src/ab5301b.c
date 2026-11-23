/* SPDX-License-Identifier: GPL-2.0-only */
/* HZ-B500 stock companion protocol. Physical audio routing remains unverified. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define FRAME_MAX 260
#define HEALTH_MS 10000
#define BT_QUEUE 8192
#define BT_STATE 1
#define BT_EVENT 2
#define BT_DATA 3
#define BT_RESET 4
#define BT_SEND 0x83
/* IPC: version=1, type, u16 big-endian length, payload. One exclusive client. */
struct bt_bridge {
	int listener, client;
	bool ready;
	uint8_t input[259], output[BT_QUEUE];
	size_t used, queued;
	uint8_t text[255];
	size_t text_used;
	bool text_overflow;
};
struct decoder { uint8_t data[FRAME_MAX]; size_t n; uint64_t consumed; };
struct frame { uint8_t cls, cmd, len, payload[255]; uint64_t offset; };
struct settings {
	uint8_t aa[10], b8[7];
	int volume, speaker, mute, source;
};
struct controller {
	int serial, timeout;
	struct decoder rx;
	struct settings desired;
	bool online, applied, reset_seen, source_applied;
	char version[64];
	bool bluetooth, bt_config_pending;
	/* Radio power, independent of the wireless-AA bridge socket. */
	bool bt_power, bt_power_pending;
	char bt_name[49], bt_pin[17];
	/* Last MM<name> the radio reported (unsolicited after IS, echoed after AT#MM). */
	char bt_reported[49];
	/* Phone media reports: MA/MB playing/paused, MI title FF artist FF album FF
	 * length_s FF, SA phone name. bt_playing -1 = not reported yet. */
	int bt_playing, bt_length;
	bool bt_paused; /* bt pause: effective volume 0 in bt-music, as stock does. */
	char bt_title[97], bt_artist[97], bt_album[97], bt_phone[49];
	struct bt_bridge *bt;
};
static int bt_io(struct controller *c, short listener_events, short client_events);
static int write_all(int fd, const void *buf, size_t n, int timeout);
static volatile sig_atomic_t stopping;
static const char *source_names[] = {
	"none", "power", "sd", "sd2", "usb", "usb2", "radio", "auto-wireless", "bt-music"
};
static const uint8_t source_modes[][2] = {
	{0,0}, {0,0}, {2,0}, {2,1}, {3,0}, {3,1}, {4,0}, {16,4}, {11,0}
};
#define SOURCE_BT_MUSIC 8

static int64_t now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void stop_signal(int sig) { (void)sig; stopping = 1; }
static int number(const char *s, int max)
{
	char *end;
	long v;
	if (!s || !*s) return -1;
	errno = 0; v = strtol(s, &end, 10);
	return errno || *end || v < 0 || v > max ? -1 : (int)v;
}
static int toggle(const char *s)
{
	if (!strcmp(s, "on") || !strcmp(s, "1")) return 1;
	if (!strcmp(s, "off") || !strcmp(s, "0")) return 0;
	return -1;
}
static int source_id(const char *s)
{
	unsigned i;
	for (i = 0; i < sizeof(source_names)/sizeof(source_names[0]); ++i)
		if (!strcmp(s, source_names[i])) return i;
	return -1;
}
static int template_hex(const char *s, uint8_t *dst, size_t n)
{
	size_t i;
	if (strlen(s) != n * 2) return -1;
	for (i = 0; i < n; ++i) {
		char byte[3] = {s[i*2], s[i*2+1], 0}, *end;
		unsigned long v;
		if (!((byte[0] >= '0' && byte[0] <= '9') || (byte[0] >= 'a' && byte[0] <= 'f') || (byte[0] >= 'A' && byte[0] <= 'F')) ||
		    !((byte[1] >= '0' && byte[1] <= '9') || (byte[1] >= 'a' && byte[1] <= 'f') || (byte[1] >= 'A' && byte[1] <= 'F'))) return -1;
		v = strtoul(byte, &end, 16);
		if (*end) return -1;
		dst[i] = v;
	}
	return 0;
}
static size_t encode(uint8_t cmd, const uint8_t *p, size_t n, uint8_t *out)
{
	size_t i;
	uint8_t sum = 0;
	out[0] = 2; out[1] = 0x80; out[2] = cmd; out[3] = n;
	if (n) memcpy(out + 4, p, n);
	for (i = 1; i < n + 4; ++i) sum ^= out[i];
	out[n+4] = sum;
	return n + 5;
}
/* Retain embedded start bytes until length/checksum disprove the candidate. */
static int decode(struct decoder *d, struct frame *f)
{
	while (d->n) {
		size_t n, i;
		uint8_t sum = 0;
		if (d->data[0] != 2 || (d->n > 1 && d->data[1] != 0x80 && d->data[1] != 0x81)) goto discard;
		if (d->n < 4) return 0;
		n = d->data[3] + 5U;
		if (d->n < n) return 0;
		for (i = 1; i < n; ++i) sum ^= d->data[i];
		if (sum) { fprintf(stderr, "ab5301b: invalid RX checksum; resynchronizing\n"); goto discard; }
		f->cls = d->data[1]; f->cmd = d->data[2]; f->len = d->data[3];
		f->offset = d->consumed;
		memcpy(f->payload, d->data + 4, f->len);
		d->consumed += n;
		d->n -= n; memmove(d->data, d->data+n, d->n);
		return 1;
	discard:
		++d->consumed;
		--d->n; memmove(d->data, d->data+1, d->n);
	}
	return 0;
}
static void log_frame(const char *direction, const uint8_t *p, size_t n)
{
	struct timespec t;
	struct tm tm;
	char stamp[32]; size_t i;
	clock_gettime(CLOCK_REALTIME, &t); gmtime_r(&t.tv_sec, &tm);
	strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &tm);
	fprintf(stderr, "%s.%03ldZ %s", stamp, t.tv_nsec/1000000, direction);
	if (n >= 5 && (p[2] == 0xd4 || p[2] == 0xd6)) {
		fprintf(stderr, " class=%02X cmd=%02X len=%u <Bluetooth payload redacted>\n", p[1], p[2], p[3]);
		return;
	}
	for (i = 0; i < n; ++i) fprintf(stderr, " %02X", p[i]);
	fputc('\n', stderr);
}
static void bt_disconnect(struct bt_bridge *b)
{
	if (b->client >= 0) close(b->client);
	b->client = -1; b->used = b->queued = 0;
}
static void bt_emit(struct controller *c, uint8_t type, const uint8_t *p, size_t n)
{
	struct bt_bridge *b = c->bt;
	if (!b || b->client < 0) return;
	if (n + 4 > sizeof(b->output) - b->queued) {
		fprintf(stderr, "ab5301b: Bluetooth client overrun; closing stream\n");
		bt_disconnect(b); return;
	}
	b->output[b->queued++] = 1; b->output[b->queued++] = type;
	b->output[b->queued++] = n >> 8; b->output[b->queued++] = n;
	if (n) memcpy(b->output+b->queued, p, n);
	b->queued += n;
}
/* Copies phone text for single-line status: controls become '?', 0xFF never
 * reaches here (field separator), and a UTF-8 sequence cut by the cap is dropped. */
static void bt_field(char *out, size_t size, const uint8_t *p, size_t n)
{
	size_t k;
	if (n >= size) {
		n = size-1;
		while (n && (p[n] & 0xc0) == 0x80) n--; /* p[n] starts the dropped tail */
	}
	for (k=0; k<n; k++) out[k] = p[k] < 32 || p[k] == 127 ? '?' : (char)p[k];
	out[n] = 0;
}
static void bt_media(struct controller *c, const uint8_t *p, size_t n)
{
	char *fields[] = {c->bt_title, c->bt_artist, c->bt_album};
	char length[12] = "";
	size_t i, start = 0, field = 0;
	for (i=0; i<=n && field<4; i++) {
		if (i < n && p[i] != 0xff) continue;
		if (field < 3) bt_field(fields[field], sizeof(c->bt_title), p+start, i-start);
		else bt_field(length, sizeof(length), p+start, i-start);
		field++; start = i+1;
	}
	c->bt_length = atoi(length);
}
/* D4 is a CRLF text stream: a UART packet can split or combine status lines. */
static void bt_text(struct controller *c, const uint8_t *p, size_t n)
{
	struct bt_bridge *b=c->bt;
	size_t i;
	if (!b) return;
	for (i=0;i<n;i++) {
		if (!p[i]) continue; /* Ignore C-string padding around CRLF records. */
		if (b->text_used==sizeof(b->text)) b->text_overflow=true;
		if (!b->text_overflow) b->text[b->text_used++]=p[i];
		if (p[i]!='\n') continue;
		if (!b->text_overflow && b->text_used>=2 && b->text[b->text_used-2]=='\r') {
			if (b->text_used>4 && b->text[0]=='M' && b->text[1]=='M') {
				size_t k, len=b->text_used-4;
				if (len>=sizeof(c->bt_reported)) len=sizeof(c->bt_reported)-1;
				for (k=0;k<len;k++) c->bt_reported[k]=b->text[k+2]>=32 && b->text[k+2]<127 ? b->text[k+2]:'?';
				c->bt_reported[len]=0;
			}
			if (b->text_used>4 && b->text[0]=='M' && b->text[1]=='I')
				bt_media(c,b->text+2,b->text_used-4);
			if (b->text_used>4 && b->text[0]=='S' && b->text[1]=='A')
				bt_field(c->bt_phone,sizeof(c->bt_phone),b->text+2,b->text_used-4);
			if (b->text_used==4) {
				if (!memcmp(b->text,"MA\r\n",4)) c->bt_playing=1;
				if (!memcmp(b->text,"MB\r\n",4)) c->bt_playing=0;
				/* IS: radio (re)started; re-assert power and name/PIN. */
				if (!memcmp(b->text,"IS\r\n",4)) c->bt_config_pending=c->bt_power_pending=true;
				if (!memcmp(b->text,"SW\r\n",4)) b->ready=true;
				if (!memcmp(b->text,"IA\r\n",4) || !memcmp(b->text,"IS\r\n",4)) b->ready=false;
			}
			bt_emit(c,BT_EVENT,b->text,b->text_used);
		}
		b->text_used=0; b->text_overflow=false;
	}
}
static void received(struct controller *c, const struct frame *f)
{
	uint8_t bytes[FRAME_MAX]; size_t i, n = f->len + 5U;
	bytes[0]=2; bytes[1]=f->cls; bytes[2]=f->cmd; bytes[3]=f->len;
	memcpy(bytes+4, f->payload, f->len); bytes[n-1]=0;
	for (i=1; i<n-1; ++i) bytes[n-1] ^= bytes[i];
	log_frame("RX", bytes, n);
	if (f->cls == 0x81 && f->cmd == 0xc3) {
		c->reset_seen = true; c->applied = false; c->source_applied = false;
		fprintf(stderr, "ab5301b: companion version report; scheduling state restore\n");
		if (c->bt) { c->bt->ready=false; c->bt->text_used=0; c->bt->text_overflow=false; }
		bt_emit(c, BT_RESET, NULL, 0);
	}
	if (f->cls == 0x81 && f->cmd == 0xd4) bt_text(c,f->payload,f->len);
	if (f->cls == 0x81 && f->cmd == 0xd6) bt_emit(c, BT_DATA, f->payload, f->len);
}
static int serial_frame(struct controller *c, struct frame *f, int64_t deadline)
{
	for (;;) {
		struct pollfd pfd[3] = {{c->serial, POLLIN, 0}, {-1, POLLIN, 0}, {-1, POLLIN, 0}};
		int r, remaining;
		ssize_t n;
		if (decode(&c->rx, f)) { received(c, f); return 1; }
		remaining = deadline - now_ms();
		if (remaining <= 0 || stopping) return 0;
		if (c->bt) {
			pfd[1].fd=c->bt->listener; pfd[2].fd=c->bt->client;
			if (c->bt->queued) pfd[2].events |= POLLOUT;
		}
		r = poll(pfd, 3, remaining);
		if (r < 0 && errno == EINTR) continue;
		if (r < 0) return -1;
		if (!r) return 0;
		if (bt_io(c, pfd[1].revents, pfd[2].revents)) return -1;
		if (pfd[0].revents & (POLLERR|POLLNVAL)) return -1;
		if (!(pfd[0].revents & POLLIN)) {
			if (pfd[0].revents & POLLHUP) return -1;
			continue;
		}
		n = read(c->serial, c->rx.data+c->rx.n, sizeof(c->rx.data)-c->rx.n);
		if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
		if (n <= 0) return -1;
		c->rx.n += n;
	}
}
static int send_frame(struct controller *c, uint8_t cmd, const uint8_t *p, size_t n)
{
	uint8_t bytes[FRAME_MAX];
	if (n > 255) return -1;
	n=encode(cmd,p,n,bytes); log_frame("TX",bytes,n);
	return write_all(c->serial,bytes,n,c->timeout);
}
static int bt_io(struct controller *c, short listener_events, short client_events)
{
	struct bt_bridge *b=c->bt;
	ssize_t n;
	if (!b) return 0;
	if (listener_events & POLLIN) {
		int fd=accept4(b->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
		if (fd>=0) {
			if (b->client>=0) close(fd);
			else { uint8_t ready=b->ready; b->client=fd; bt_emit(c,BT_STATE,&ready,1); }
		}
	}
	if (b->client<0) return 0;
	if (client_events & POLLIN) {
		n=recv(b->client,b->input+b->used,sizeof(b->input)-b->used,0);
		if (n<=0) {
			if (!n || (errno!=EAGAIN && errno!=EINTR)) bt_disconnect(b);
		} else {
			b->used+=n;
			while (b->used>=4) {
				size_t len=((size_t)b->input[2]<<8)|b->input[3];
				if (b->input[0]!=1 || b->input[1]!=BT_SEND || !len || len>240) {
					bt_disconnect(b); break;
				}
				if (b->used<len+4) break;
				if (send_frame(c,0xd6,b->input+4,len)) { bt_disconnect(b); return -1; }
				b->used-=len+4; memmove(b->input,b->input+len+4,b->used);
			}
		}
	}
	if (b->client>=0 && b->queued) {
		n=send(b->client,b->output,b->queued,MSG_NOSIGNAL|MSG_DONTWAIT);
		if (n>0) { b->queued-=n; memmove(b->output,b->output+n,b->queued); }
		else if (n<0 && errno!=EAGAIN && errno!=EINTR) bt_disconnect(b);
	}
	if (b->client>=0 && (client_events & (POLLHUP|POLLERR|POLLNVAL))) bt_disconnect(b);
	return 0;
}
static int bt_send(struct controller *c, const char *text)
{
	return send_frame(c,0xd4,(const uint8_t *)text,strlen(text));
}
static int bt_configure(struct controller *c)
{
	char text[64];
	/* Name/PIN first, then power: stock boot order is AT#MM, then AT#CA/CC. */
	if (c->bt_config_pending) {
		c->bt_config_pending=false;
		if (*c->bt_name) {
			snprintf(text,sizeof(text),"AT#MM%s\r\n",c->bt_name);
			if (bt_send(c,text)) return -1;
		}
		if (*c->bt_pin) {
			snprintf(text,sizeof(text),"AT#MN%s\r\n",c->bt_pin);
			if (bt_send(c,text)) return -1;
		}
	}
	if (c->bt_power_pending) {
		c->bt_power_pending=false;
		/* Stock init.axf EnableBtAndReconnect / DisableBtAndDisconnect sequences. */
		if (c->bt_power ? bt_send(c,"AT#CA\r\n") || bt_send(c,"AT#CC\r\n") :
		                  bt_send(c,"AT#CD\r\n") || bt_send(c,"AT#CB\r\n")) return -1;
	}
	return 0;
}
static int write_all(int fd, const void *buf, size_t n, int timeout)
{
	const uint8_t *p = buf;
	int64_t deadline = now_ms()+timeout;
	while (n && !stopping) {
		ssize_t k = write(fd, p, n);
		struct pollfd pollfd = {fd, POLLOUT, 0};
		int remaining;
		if (k > 0) { p+=k; n-=k; continue; }
		if (k < 0 && errno == EINTR) continue;
		if (k < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
		remaining = deadline-now_ms();
		if (remaining <= 0 || poll(&pollfd, 1, remaining) <= 0) return -1;
	}
	return n ? -1 : 0;
}
/* Exactly one request outstanding. Old bytes are decoded but never count as ACK. */
static int exchange(struct controller *c, uint8_t cmd, const uint8_t *payload, size_t len, int kind)
{
	uint8_t bytes[FRAME_MAX]; struct frame f;
	int r;
	uint64_t boundary;
	int64_t deadline, drain_deadline = now_ms()+20;
	r = 0;
	while (now_ms() < drain_deadline && (r = serial_frame(c, &f, now_ms()+1)) > 0) {}
	if (r < 0) goto failed;
	/* Preserve partial Bluetooth packets. A frame that began before TX is not an ACK. */
	boundary = c->rx.consumed + c->rx.n;
	len = encode(cmd, payload, len, bytes);
	log_frame("TX", bytes, len);
	if (write_all(c->serial, bytes, len, c->timeout)) goto failed;
	deadline = now_ms()+c->timeout;
	while ((r = serial_frame(c, &f, deadline)) > 0) {
		if (f.offset < boundary || f.cls != 0x80 || f.cmd != cmd) continue;
		if (kind == 2) {
			if (f.len != 10) continue; /* A4 captured response shape. */
		} else if (kind == 1) {
			size_t i, n;
			if (f.len < 2 || f.payload[0] != 0) continue;
			n = f.len-1; if (n >= sizeof(c->version)) n=sizeof(c->version)-1;
			for (i=0; i<n; ++i) c->version[i] = f.payload[i+1] >= 32 && f.payload[i+1] < 127 ? f.payload[i+1] : '?';
			c->version[n]=0;
		} else {
			if (f.len != 1) continue;
			if (f.payload[0]) {
				fprintf(stderr, "ab5301b: command %02X reply status %02X\n", cmd, f.payload[0]);
				goto failed;
			}
		}
		c->online = true;
		return 0;
	}
	fprintf(stderr, "ab5301b: command %02X has no matching reply\n", cmd);
failed:
	c->online = false; c->applied = false; c->source_applied = false;
	return -1;
}
static int apply(struct controller *c)
{
	uint8_t aa[10], b8[7];
	c->applied = false;
	memcpy(aa, c->desired.aa, sizeof(aa)); memcpy(b8, c->desired.b8, sizeof(b8));
	/* Stock BT pause: effective volume 0, mute flag unchanged. */
	aa[0] = c->desired.mute || (c->bt_paused && c->desired.source == SOURCE_BT_MUSIC) ? 0 : c->desired.volume;
	aa[4] = c->desired.mute; aa[8] = c->desired.volume;
	aa[7] = (aa[7] & ~1U) | c->desired.speaker;
	b8[1] = (b8[1] & ~1U) | c->desired.speaker;
	if (c->desired.source && !c->source_applied) {
		if (exchange(c, 0xa7, source_modes[c->desired.source], 2, 0)) return -1;
		c->source_applied = true;
	}
	if (exchange(c, 0xaa, aa, sizeof(aa), 0) || exchange(c, 0xb8, b8, sizeof(b8), 0)) return -1;
	c->applied = true;
	return 0;
}
static int recover(struct controller *c)
{
	/* Power before A7: a source switch auto-reconnects the last phone unless
	 * AT#CB/CD already blocked it (MCU RAM 11833); CB also drops page scan. */
	if (c->reset_seen) c->bt_power_pending = true;
	c->reset_seen = false;
	if (bt_configure(c) || exchange(c, 0xc3, NULL, 0, 1)) return -1;
	return apply(c);
}
static int initialize(struct controller *c)
{
	static const uint8_t complete[] = {15, 0};
	c->source_applied = false;
	/* Diagnostic subset of observed init, not a proven cold-start handshake. */
	if (exchange(c, 0xa4, NULL, 0, 2) || exchange(c, 0xa0, NULL, 0, 0) ||
	    exchange(c, 0xc3, NULL, 0, 1) || exchange(c, 0xa7, complete, 2, 0)) return -1;
	return apply(c);
}
static int command(struct controller *c, char *line, char *reply, size_t n)
{
	char *save, *cmd = strtok_r(line, " \t\r\n", &save);
	char *arg = strtok_r(NULL, " \t\r\n", &save);
	char *extra = strtok_r(NULL, " \t\r\n", &save);
	int v, result;
	if (!cmd || extra) goto usage;
	if (!strcmp(cmd, "status") && !arg) {
		snprintf(reply, n, "OK online=%d settings_acknowledged=%d requested_speaker=%s requested_mute=%s requested_volume=%d requested_source=%s version=%s physical_output=unverified bluetooth=%s bt_name=%s\n",
			 c->online, c->applied, c->desired.speaker ? "on":"off", c->desired.mute ? "on":"off",
			 c->desired.volume, source_names[c->desired.source], c->version[0] ? c->version:"unknown",
			 c->bt_power ? "on":"off", c->bt_reported); /* bt_name is last: rest of line, may be empty. */
		return 0;
	}
	if (!strcmp(cmd, "name") && arg) {
		size_t k, len=strlen(arg);
		if (len>16) goto usage; /* ponytail: 16 is our cap; radio limit unknown, MM echo shows truncation. */
		for (k=0;k<len;k++) if ((unsigned char)arg[k]<33 || (unsigned char)arg[k]>126) goto usage;
		strcpy(c->bt_name,arg); c->bt_config_pending=true;
		result=bt_configure(c);
		snprintf(reply, n, result ? "ERR UART write failed; inspect logread\n" :
			 "OK sent; status bt_name shows the name the radio reports\n");
		return result;
	}
	if (!strcmp(cmd, "bluetooth") && arg && (v=toggle(arg)) >= 0) {
		/* D4 AT commands have no reply frame; OK means written to UART. */
		c->bt_power=v; c->bt_power_pending=true;
		result=bt_configure(c);
		snprintf(reply, n, result ? "ERR UART write failed; inspect logread\n" :
			 "OK sent; Bluetooth radio does not acknowledge power commands\n");
		return result;
	}
	if (!strcmp(cmd, "bt") && !arg) {
		/* One line; text fields are tab-separated because titles contain spaces. */
		snprintf(reply, n, "OK state=%s\tconnections=%s\tlength=%d\tphone=%s\ttitle=%s\tartist=%s\talbum=%s\n",
			 c->bt_playing < 0 ? "unknown" : c->bt_playing ? "playing" : "paused",
			 c->bt_power ? "on" : "off", c->bt_length, c->bt_phone, c->bt_title, c->bt_artist, c->bt_album);
		return 0;
	}
	if (!strcmp(cmd, "bt") && arg && (!strcmp(arg, "disconnect") || !strcmp(arg, "connect"))) {
		/* disconnect: AT#CD drops the phone's current link only (connections stay
		 * allowed). After a wireless AA quit the phone otherwise keeps re-opening
		 * its AA setup channel every ~12 s and shows "Starting Android Auto".
		 * connect: AT#CC pages the last connected phone (the module itself calls
		 * it, so it works while connections are off); ignored while connected. */
		result = bt_send(c, arg[0] == 'd' ? "AT#CD\r\n" : "AT#CC\r\n");
		snprintf(reply, n, result ? "ERR UART write failed; inspect logread\n" : "OK sent\n");
		return result;
	}
	if (!strcmp(cmd, "bt") && arg) {
		/* Captured stock sequences; D4 and B0 A0 get no reply, MA/MB/MI report the result. */
		static const uint8_t quiet[] = {0xa0, 0, 0, 0, 0}; /* B0 A0: short anti-pop mute */
		const char *at = !strcmp(arg,"play") ? "AT#MA\r\n" : !strcmp(arg,"pause") ? "AT#MB\r\n" :
				 !strcmp(arg,"next") ? "AT#MD\r\n" : !strcmp(arg,"prev") ? "AT#ME\r\n" : NULL;
		if (!at) goto usage;
		if (at[4] == 'D' || at[4] == 'E') result = send_frame(c, 0xb0, quiet, sizeof(quiet)) || bt_send(c, at);
		else {
			c->bt_paused = at[4] == 'B';
			result = bt_send(c, at) || apply(c);
		}
		snprintf(reply, n, result ? "ERR UART write or volume apply failed; inspect logread\n" :
			 "OK sent; bt shows the phone's reported state\n");
		return result;
	}
	if ((!strcmp(cmd, "fm") || !strcmp(cmd, "lineout"))) {
		snprintf(reply, n, "ERR independent %s control has not been decoded\n", cmd); return -1;
	}
	if (!strcmp(cmd, "speaker") && arg && (v=toggle(arg)) >= 0) c->desired.speaker=v;
	else if (!strcmp(cmd, "mute") && arg && (v=toggle(arg)) >= 0) c->desired.mute=v;
	else if (!strcmp(cmd, "volume") && arg && (v=number(arg,20)) >= 0) c->desired.volume=v;
	else if (!strcmp(cmd, "source") && arg && (v=source_id(arg)) >= 0) {
		c->desired.source=v; c->source_applied=false;
	}
	else if (!arg && (!strcmp(cmd,"apply") || !strcmp(cmd,"initialize") || !strcmp(cmd,"probe"))) {}
	else goto usage;
	if (strcmp(cmd,"probe")) c->applied = false;
	if (!strcmp(cmd,"initialize")) result=initialize(c);
	else if (!strcmp(cmd,"probe")) result=exchange(c, 0xc3, NULL, 0, 1);
	else result=apply(c);
	snprintf(reply, n, result ? "ERR no acknowledgement; requested settings retained for retry; inspect logread\n" :
		 "OK acknowledged; physical output requires verification\n");
	return result;
usage:
	snprintf(reply, n, "ERR use status|probe|initialize|apply|speaker on/off|mute on/off|bluetooth on/off|name TEXT(1..16, no spaces)|bt [play/pause/next/prev/connect/disconnect]|volume 0..20|source none/power/sd/sd2/usb/usb2/radio/auto-wireless/bt-music\n");
	return -1;
}
static int open_serial(const char *path)
{
	struct termios t;
	int fd = open(path, O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
	if (fd < 0) return -1;
	if (flock(fd, LOCK_EX|LOCK_NB) || ioctl(fd, TIOCEXCL) || tcgetattr(fd, &t)) goto bad;
	cfmakeraw(&t); cfsetispeed(&t, B115200); cfsetospeed(&t, B115200);
	t.c_cflag &= ~(CSIZE|PARENB|CSTOPB|CRTSCTS); t.c_cflag |= CS8|CLOCAL|CREAD;
	t.c_cc[VMIN]=0; t.c_cc[VTIME]=0;
	if (tcsetattr(fd,TCSANOW,&t)) goto bad;
	return fd;
bad:
	close(fd); return -1;
}
static int make_address(struct sockaddr_un *a, const char *path)
{
	memset(a, 0, sizeof(*a)); a->sun_family=AF_UNIX;
	if (!*path || strlen(path)>=sizeof(a->sun_path)) return -1;
	strcpy(a->sun_path, path); return 0;
}
static int server(struct controller *c, const char *device, const char *path)
{
	struct sockaddr_un a;
	struct bt_bridge bridge={.listener=-1,.client=-1};
	int listener, lockfd, control_fd=-1, rc=1;
	char lockpath[PATH_MAX], btpath[sizeof(a.sun_path)], line[256];
	size_t used=0;
	int64_t control_deadline=0;
	int64_t health;
	if (make_address(&a,path) || snprintf(lockpath,sizeof(lockpath),"%s.lock",path)>=(int)sizeof(lockpath)) return 1;
	lockfd=open(lockpath,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
	if (lockfd<0 || flock(lockfd,LOCK_EX|LOCK_NB)) { perror("ab5301b: service lock"); if(lockfd>=0)close(lockfd); return 1; }
	c->serial=open_serial(device);
	if (c->serial<0) { perror("ab5301b: serial"); close(lockfd); return 1; }
	listener=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
	if(listener<0) goto done;
	/* Lock ownership excludes a running service with this path. */
	if (unlink(path) && errno!=ENOENT) goto socket_done;
	if(bind(listener,(struct sockaddr*)&a,sizeof(a)) || chmod(path,0600) || listen(listener,4)) { perror("ab5301b: socket"); goto socket_done; }
	if (c->bluetooth) {
		if (snprintf(btpath,sizeof(btpath),"%s.bt",path)>=(int)sizeof(btpath) || make_address(&a,btpath)) goto socket_done;
		bridge.listener=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
		if (bridge.listener<0) goto socket_done;
		/* Shares the control socket's exclusive service lock and namespace. */
		if ((unlink(btpath) && errno!=ENOENT) || bind(bridge.listener,(struct sockaddr *)&a,sizeof(a)) ||
		    chmod(btpath,0600) || listen(bridge.listener,2)) { perror("ab5301b: Bluetooth socket"); goto socket_done; }
	}
	/* Always parse D4 text so IS re-asserts radio power; sockets exist only with the bridge. */
	c->bt=&bridge;
	fprintf(stderr,"ab5301b: started, 115200 8N1; requested state is not hardware readback\n");
	c->bt_config_pending=c->bt_power_pending=true; recover(c); health=now_ms()+HEALTH_MS;
	while(!stopping) {
		struct pollfd p[5]={{listener,POLLIN,0},{c->serial,POLLIN,0},
			{bridge.listener,POLLIN,0},{bridge.client,POLLIN,0},{control_fd,POLLIN,0}};
		int r;
		if (bridge.queued) p[3].events|=POLLOUT;
		if (bt_configure(c)) break;
		r=poll(p,5,100);
		if(r<0 && errno==EINTR) continue;
		if(r<0 || (p[1].revents&(POLLHUP|POLLERR|POLLNVAL))) break;
		if (bt_io(c,p[2].revents,p[3].revents)) break;
		if(p[1].revents&POLLIN) {
			struct frame f;
			while(serial_frame(c,&f,now_ms()+1)>0) {}
		}
		if(p[0].revents&POLLIN) {
			int fd=accept4(listener,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);
			if(fd>=0 && control_fd>=0) close(fd);
			else if(fd>=0) { control_fd=fd; used=0; line[0]=0; control_deadline=now_ms()+1000; }
		}
		if(control_fd>=0) {
			bool finished=false;
			if(p[4].revents&POLLIN) {
				ssize_t k=read(control_fd,line+used,sizeof(line)-1-used);
				if(k>0) { used+=k; line[used]=0; }
				else if(!k || (errno!=EAGAIN && errno!=EINTR)) finished=true;
			}
			if(strchr(line,'\n') || used==sizeof(line)-1 || now_ms()>=control_deadline ||
			   (p[4].revents&(POLLHUP|POLLERR|POLLNVAL))) finished=true;
			if(finished) {
				char reply[512];
				if(!strchr(line,'\n')) snprintf(reply,sizeof(reply),"ERR incomplete command\n");
				else command(c,line,reply,sizeof(reply));
				write_all(control_fd,reply,strlen(reply),1000); close(control_fd); control_fd=-1;
			}
		}
		if(c->reset_seen || now_ms()>=health) {
			if(c->reset_seen || !c->applied) recover(c);
			else if(exchange(c,0xc3,NULL,0,1)) c->applied=false;
			health=now_ms()+HEALTH_MS;
		}
	}
	rc=stopping ? 0:1;
	unlink(path);
socket_done:
	if(control_fd>=0) close(control_fd);
	bt_disconnect(&bridge);
	if(bridge.listener>=0) { close(bridge.listener); unlink(btpath); }
	c->bt=NULL;
	close(listener);
done:
	close(c->serial); close(lockfd); return rc;
}
static int client(const char *path, const char *request, int timeout)
{
	struct sockaddr_un a;
	char reply[1024]; size_t used=0;
	int fd, rc=1;
	int64_t deadline=now_ms()+timeout*8+2000;
	if(make_address(&a,path)) return 1;
	fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
	if(fd<0 || connect(fd,(struct sockaddr*)&a,sizeof(a))) { perror("ab5301b: connect (is the service running?)"); if(fd>=0)close(fd); return 1; }
	fcntl(fd,F_SETFL,O_NONBLOCK);
	if(write_all(fd,request,strlen(request),1000)) goto done;
	while(used<sizeof(reply)-1) {
		struct pollfd p={fd,POLLIN,0};
		int remaining=deadline-now_ms(); ssize_t k;
		if(remaining<=0 || poll(&p,1,remaining)<=0) break;
		k=read(fd,reply+used,sizeof(reply)-1-used);
		if(k<=0) break;
		used+=k; reply[used]=0;
		if(strchr(reply,'\n')) { fputs(reply,stdout); rc=strncmp(reply,"OK ",3)!=0; goto done; }
	}
	fprintf(stderr,"ab5301b: incomplete service response\n");
done:
	close(fd); return rc;
}
static void usage(void)
{
	puts("Usage: ab5301b [--socket PATH] status|probe|initialize|apply\n"
	     "       ab5301b [--socket PATH] speaker on|off\n"
	     "       ab5301b [--socket PATH] mute on|off\n"
	     "       ab5301b [--socket PATH] bluetooth on|off\n"
	     "       ab5301b [--socket PATH] name TEXT\n"
	     "       ab5301b [--socket PATH] volume 0..20\n"
	     "       ab5301b [--socket PATH] source none|power|sd|sd2|usb|usb2|radio|auto-wireless|bt-music\n"
	     "       ab5301b [--socket PATH] bt [play|pause|next|prev|connect|disconnect]\n"
	     "Service: ab5301b [--device TTY] [--socket PATH] [--timeout MS]\n"
	     "         [--volume 0..20] [--speaker 0|1] [--mute 0|1] [--source NAME]\n"
	     "         [--aa-template 20HEX] [--b8-template 14HEX] serve\n"
	     "         [--bluetooth 0|1] [--bt-name NAME] [--bt-pin PIN] [--bt-power 0|1]\n"
	     "Bluetooth bridge (--bluetooth 1, the packaged default); IPC is CONTROL_SOCKET.bt (version 1).\n"
	     "Name/PIN are always sent on startup/IS; status bt_name is the name the radio reports (MM).\n"
	     "--bt-power (default 0) sends AT#CA+CC (on) or AT#CD+CB (off) on startup and on every IS.\n"
	     "Defaults: /dev/ttyS4, /var/run/ab5301b.sock, 1500 ms, volume 18, speaker on, mute on, source none.\n"
	     "initialize sends A4/A0/C3/A7(0F00) then settings; experimental subset, no CF replay.\n"
	     "FM and independent line-out control remain undecoded. Volume cap 20 is provisional.\n"
	     "Runtime changes are RAM-only; boot settings are in /etc/config/ab5301b.");
}
int main(int argc,char **argv)
{
	struct controller c={.serial=-1,.timeout=1500,.bt_playing=-1,.desired={.volume=18,.speaker=1,.mute=1}};
	const char *device="/dev/ttyS4", *path="/var/run/ab5301b.sock";
	char request[256]; int i, used=0;
	template_hex("0000070700000a011200",c.desired.aa,10);
	template_hex("03838000040001",c.desired.b8,7);
	signal(SIGTERM,stop_signal); signal(SIGINT,stop_signal); signal(SIGPIPE,SIG_IGN);
	for(i=1;i<argc && !strncmp(argv[i],"--",2);i++) {
		const char *option=argv[i], *value;
		int v;
		if(!strcmp(option,"--help")) { usage(); return 0; }
		if(++i>=argc) goto bad;
		value=argv[i];
		if(!strcmp(option,"--device")) device=value;
		else if(!strcmp(option,"--socket")) path=value;
		else if(!strcmp(option,"--volume") && (v=number(value,20))>=0) c.desired.volume=v;
		else if(!strcmp(option,"--speaker") && (v=toggle(value))>=0) c.desired.speaker=v;
		else if(!strcmp(option,"--mute") && (v=toggle(value))>=0) c.desired.mute=v;
		else if(!strcmp(option,"--source") && (v=source_id(value))>=0) c.desired.source=v;
		else if(!strcmp(option,"--timeout") && (v=number(value,10000))>=50) c.timeout=v;
		else if(!strcmp(option,"--bluetooth") && (v=toggle(value))>=0) c.bluetooth=v;
		else if(!strcmp(option,"--bt-power") && (v=toggle(value))>=0) c.bt_power=v;
		else if(!strcmp(option,"--bt-name") || !strcmp(option,"--bt-pin")) {
			size_t k, len=strlen(value);
			bool name=!strcmp(option,"--bt-name");
			if(len>=(name ? sizeof(c.bt_name):sizeof(c.bt_pin))) goto bad;
			for(k=0;k<len;k++) if((unsigned char)value[k]<32 || (unsigned char)value[k]>126 ||
				(!name && (value[k]<'0' || value[k]>'9'))) goto bad;
			strcpy(name ? c.bt_name:c.bt_pin,value);
		}
		else if(!strcmp(option,"--aa-template") && !template_hex(value,c.desired.aa,10)) {}
		else if(!strcmp(option,"--b8-template") && !template_hex(value,c.desired.b8,7) && c.desired.b8[0]==3) {}
		else goto bad;
	}
	if(i==argc) goto bad;
	if(!strcmp(argv[i],"serve") && i==argc-1) return server(&c,device,path);
	for(;i<argc;i++) {
		int n=snprintf(request+used,sizeof(request)-used,"%s%s",used ? " ":"",argv[i]);
		if(n<0 || (size_t)n>=sizeof(request)-used-1) goto bad;
		used+=n;
	}
	request[used++]='\n'; request[used]=0;
	return client(path,request,c.timeout);
bad:
	usage(); return 2;
}
