#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/i2c-dev.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INPUT_SLIDER_X	"/dev/input/event1"
#define INPUT_SLIDER_Y	"/dev/input/event2"
#define POLL_NFDS	2

#define DEBUG		0
#define dbg(fmt, ...)	\
	do { if (DEBUG) printf(fmt, __VA_ARGS__); } while(0)

int file;

int main(void)
{
	int slider_x_fd, slider_y_fd, rc, i, xpos = 0, ypos = 0;
	struct pollfd fds[POLL_NFDS];
	struct input_event ev;
	ssize_t n;

	slider_x_fd = open(INPUT_SLIDER_X, O_RDONLY);
	if (slider_x_fd == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", INPUT_SLIDER_X, strerror(errno));
		exit(EXIT_FAILURE);
	}

	slider_y_fd = open(INPUT_SLIDER_Y, O_RDONLY);
	if (slider_y_fd == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", INPUT_SLIDER_Y, strerror(errno));
		exit(EXIT_FAILURE);
	}

	fds[0].fd = slider_x_fd;
	fds[0].events = POLLIN;
	fds[1].fd = slider_y_fd;
	fds[1].events = POLLIN;

	while (1) {
		rc = poll(fds, POLL_NFDS, -1);

		if (rc < 0) {
			perror("poll() failed\n");
			exit(EXIT_FAILURE);
		}

		for (i = 0; i < POLL_NFDS; i++) {
			if (fds[i].revents == 0)
				continue;

			if (fds[i].revents != POLLIN) {
				fprintf(stderr, "error, revents = %d\n", fds[i].revents);
				break;
			}

			/* Get slider X event. */
			if (fds[i].fd == slider_x_fd) {
				n = read(slider_x_fd, &ev, sizeof(ev));
				if (n == (ssize_t) - 1) {
						break;
				} else {
					if (n != sizeof(ev)) {
						errno = EIO;
						break;
					}
				}

				dbg("Slider X event: type=%d, code=%d, value=%d\n", ev.type, ev.code, ev.value);

				/* Slider touch. */
				if (ev.type == EV_KEY)
					if (ev.value == 0)
						xpos = 0;

				/* Slider position. */
				if (ev.type == EV_ABS)
					xpos = ev.value;
			}

			/* Get slider Y event. */
			if (fds[i].fd == slider_y_fd) {
				n = read(slider_y_fd, &ev, sizeof(ev));
				if (n == (ssize_t) - 1) {
						break;
				} else {
					if (n != sizeof(ev)) {
						errno = EIO;
						break;
					}
				}

				dbg("Slider Y event: type=%d, code=%d, value=%d\n", ev.type, ev.code, ev.value);

				/* Slider touch. */
				if (ev.type == EV_KEY)
					if (ev.value == 0)
						ypos = 0;

				/* Slider position. */
				if (ev.type == EV_ABS)
					ypos = ev.value;
			}
		}

		printf("x=%d - y=%d\n", xpos, ypos);
	}

	exit(EXIT_SUCCESS);
}
