#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>

#include <linux/bt-host.h>

struct bttest_data {
	int sent;
	const char msg[64];
};

static int bt_host_fd;

/*
 * btbridged doesn't care about the message EXCEPT the first byte must be
 * correct.
 * The first byte is the size not including the length byte its self.
 * A len less than 4 will constitute an invalid message according to the BT
 * protocol, btbridged will care.
 */
static struct bttest_data data[] = {
	/*
	 * Make the first message look like:
	 * seq = 1, netfn = 2, len = 3 and cmd= 4
	 * (thats how btbridged will print it)
	 */
	{ 0, { 4 , 0xb, 1, 4, 4 }},
	{ 0, { 4 , 0xff, 0xee, 0xdd, 0xcc, 0xbb }},
};
#define BTTEST_NUM 2
#define PREFIX "[BTHOST] "

#define MSG_OUT(f_, ...) do { printf(PREFIX); printf((f_), ##__VA_ARGS__); } while(0)
#define MSG_ERR(f_, ...) do { fprintf(stderr,PREFIX); fprintf(stderr, (f_), ##__VA_ARGS__); } while(0)

typedef int (*orig_open_t)(const char *pathname, int flags);
typedef int (*orig_poll_t)(struct pollfd *fds, nfds_t nfds, int timeout);
typedef int (*orig_read_t)(int fd, void *buf, size_t count);
typedef ssize_t (*orig_write_t)(int fd, const void *buf, size_t count);
typedef int (*orig_ioctl_t)(int fd, unsigned long request, char *p);

int ioctl(int fd, unsigned long request, char *p)
{
	if (fd == bt_host_fd) {
		MSG_OUT("ioctl(%d, %lu, %p)\n", fd, request, p);
		/* TODO Check the request number */
		return 0;
	}

	orig_ioctl_t orig_ioctl;
	orig_ioctl = (orig_ioctl_t)dlsym(RTLD_NEXT,"ioctl");
	return orig_ioctl(fd, request, p);
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	int i, j;
	int ret = 0;
	struct pollfd *new_fds = calloc(nfds, sizeof(struct pollfd));
	j = 0;
	for (i = 0; i  < nfds; i++) {
		if (fds[i].fd == bt_host_fd) {
			MSG_OUT("poll()\n");
			fds[i].revents = POLLIN | POLLOUT;
			ret = 1;
		} else {
			new_fds[j].fd = fds[i].fd;
			new_fds[j].events = fds[i].events;
			/* Copy this to be sure */
			new_fds[j].revents = fds[i].revents;
			j++;
		}
	}
	orig_poll_t orig_poll;
	orig_poll = (orig_poll_t)dlsym(RTLD_NEXT,"poll");
	ret += orig_poll(new_fds, nfds - ret, timeout);
	j = 0;
	for (i = 0; i < nfds; i++) {
		if (fds[i].fd != bt_host_fd) {
			fds[i].fd = new_fds[j].fd;
			fds[i].revents = new_fds[j].revents;
			j++;
		}
	}
	return ret;
}

int open(const char *pathname, int flags)
{
	if (strcmp("/dev/bt-host", pathname) == 0) {
		MSG_OUT("open(%s, %x)\n", pathname, flags);
		bt_host_fd = socket(AF_UNIX, SOCK_STREAM, 0);
		return bt_host_fd;
	}
	orig_open_t orig_open;
	orig_open = (orig_open_t)dlsym(RTLD_NEXT,"open");
	return orig_open(pathname, flags);
}

int read(int fd, void *buf, size_t count)
{
	if (fd == bt_host_fd) {
		int i;
		MSG_OUT("read(%d, %p, %ld)\n", fd, buf, count);
		for (i = 0; i < BTTEST_NUM && data[i].sent; i++);
		if (i == BTTEST_NUM) {
			/*
			 * We're out of test data so just keep resending the first one
			 * untill we get a response to all the ones we've already put
			 * out there
			 */
			i = 0;
		}

		if (count < data[i].msg[0] + 1) {
			/*
			 * TODO handle this, not urgent, the real driver also gets it
			 * wrong
			 */
			MSG_ERR("Read size was too small\n");
			errno = ENOMEM;
			return -1;
		}
		memcpy(buf, data[i].msg, data[i].msg[0] + 1);
		data[i].sent = 1;
		return data[i].msg[0] + 1;
	}
	orig_read_t orig_read;
	orig_read = (orig_read_t)dlsym(RTLD_NEXT,"read");
	return orig_read(fd, buf, count);
}

int write(int fd, const void *buf, size_t count)
{
	if (fd == bt_host_fd) {
		int i, end = 1;
		MSG_OUT("write(%d, %p, %ld)\n", fd, buf, count);
		for (i = 0; i < BTTEST_NUM; i++) {
			if (!data[i].sent)
				end = 0;
			if (data[i].sent && data[i].msg[0] + 1 == count - 1) {
				if (memcmp(buf + 1, data[i].msg + 1, count - 2) == 0)
					break;
			}
		}
		if (end) {
			MSG_OUT("recieved a response to all messages, tentative success\n");
			exit(0);
		}
		if (i == BTTEST_NUM) {
			MSG_ERR("FAIL: Unexpected write data\n");
			exit(1);
		}
		return count;
	}
	orig_write_t orig_write;
	orig_write = (orig_write_t)dlsym(RTLD_NEXT,"write");
	return orig_write(fd, buf, count);
}
