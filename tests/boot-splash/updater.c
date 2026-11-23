/* Exercise the actual updater with a sparse SD image and injected storage failures. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <assert.h>
#include <stdio.h>
static int writes, fail_write, corrupt_read, fail_sync;
static ssize_t injected_pwrite(int fd, const void *buf, size_t n, off_t at);
static ssize_t injected_pread(int fd, void *buf, size_t n, off_t at);
static int injected_fsync(int fd);
#define pwrite injected_pwrite
#define pread injected_pread
#define fsync injected_fsync
#define main boot_logo_main
#include "../../package/utils/boot-logo/src/boot-logo.c"
#undef main
#undef pwrite
#undef pread
#undef fsync
static ssize_t injected_pwrite(int fd, const void *buf, size_t n, off_t at)
{
	++writes;
	if (writes == fail_write) { errno = EIO; return -1; }
	/* Exercise complete-transfer handling, too. */
	if (n > 65536) n = 65536;
	return pwrite(fd, buf, n, at);
}
static ssize_t injected_pread(int fd, void *buf, size_t n, off_t at)
{
	if (n > 32768) n = 32768;
	ssize_t r = pread(fd, buf, n, at);
	if (corrupt_read && r > 0 && at >= (off_t)LOGO_START * 512 + 512)
		((unsigned char *)buf)[0] ^= 1;
	return r;
}
static int injected_fsync(int fd)
{
	if (fail_sync && writes > 1) { fail_sync = 0; errno = EIO; return -1; }
	return fsync(fd);
}
static void install_child(int fd, off_t at, int fault)
{
	pid_t pid = fork(); assert(pid >= 0);
	if (!pid) {
		writes = 0;
		fail_write = fault == 1 ? 3 : 0;
		corrupt_read = fault == 2;
		fail_sync = fault == 3;
		install_logo(fd, at);
		exit(0);
	}
	int status;
	assert(waitpid(pid, &status, 0) == pid);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == (fault ? 1 : 0));
}
int main(int argc, char **argv)
{
	assert(argc == 3);
	artwork(argv[1]);
	int fd = open(argv[2], O_RDWR | O_CREAT | O_TRUNC, 0600); assert(fd >= 0);
	assert(!ftruncate(fd, DISK_BYTES));
	unsigned char mbr[512] = {0}, before[512], after[512];
	mbr[450] = 0x83; mbr[466] = HZ_SPLASH_TYPE; mbr[510] = 0x55; mbr[511] = 0xaa;
	hz_splash_put32(mbr + 454, ROOT_START); hz_splash_put32(mbr + 458, ROOT_SECTORS);
	hz_splash_put32(mbr + 470, LOGO_START); hz_splash_put32(mbr + 474, HZ_SPLASH_SECTORS);
	assert(pwrite(fd, mbr, sizeof(mbr), 0) == sizeof(mbr));
	memset(before, 0x5a, sizeof(before));
	assert(pwrite(fd, before, sizeof(before), (off_t)ROOT_START * 512) == sizeof(before));
	assert(pwrite(fd, before, sizeof(before), (off_t)LOGO_START * 512 - 512) == sizeof(before));
	off_t at;
	int dest = destination(argv[2], &at);
	assert(at == (off_t)LOGO_START * 512);
	for (int fault = 0; fault <= 3; ++fault) {
		install_child(dest, at, fault);
		assert(valid_image(dest, at) == !fault);
		assert(pread(fd, after, sizeof(after), 0) == sizeof(after)); assert(!memcmp(after, mbr, 512));
		assert(pread(fd, after, sizeof(after), (off_t)ROOT_START * 512) == sizeof(after)); assert(!memcmp(after, before, 512));
		assert(pread(fd, after, sizeof(after), at - 512) == sizeof(after)); assert(!memcmp(after, before, 512));
	}
	close(dest);
	/* Restore a valid image for the shell CLI tests. */
	install_child(fd, at, 0);
	close(fd);
	puts("PASS: partition bounds, short I/O, write/fsync failure and corrupt readback invalidate logo");
	return 0;
}
