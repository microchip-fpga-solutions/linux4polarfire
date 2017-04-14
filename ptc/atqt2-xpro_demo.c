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

#define INPUT_BUTTONS	"/dev/input/event1"
#define POLL_NFDS	1

#define IS31FL3728_ADDR 0x60
#define IS31FL3728_UPDATE_COLUMN_REG 0xc

#define DEBUG		1
#define dbg(fmt, ...)	\
	do { if (DEBUG) printf(fmt, __VA_ARGS__); } while(0)

int file;
char col_state[7] = {0};

void led_update()
{
	char buf[2];

	buf[0] = IS31FL3728_UPDATE_COLUMN_REG;
	buf[1] = 0x1;
	if (write(file, buf, 2) != 2) {
		perror("Failed to write to the i2c bus.");
		exit(EXIT_FAILURE);
	}
}

void led_all_off(void)
{
	char buf[2];
	int i;

	buf[1] = 0;
	for (i = 0; i < 8; i++) {
		buf[0] = i;
		if (write(file, buf, 2) != 2) {
			perror("Failed to write to the i2c bus.");
			exit(EXIT_FAILURE);
		}
	}

	led_update();
}

void led_state(unsigned int id, bool state)
{
	char buf[2];

	if (state)
		col_state[id % 7] |= 0b01000000 >> (id / 7);
	else
		col_state[id % 7] &= ~0b01000000 >> (id / 7);

	buf[0] = 1 + id % 7; /* col0 @ 0x1 */
	buf[1] = col_state[id % 7];

	if (write(file, buf, 2) != 2) {
		perror("Failed to write to the i2c bus.");
		exit(EXIT_FAILURE);
	}

	led_update();
}

int main(void)
{
	char *filename = "/dev/i2c-1";
	int buttons_fd, rc;
	struct pollfd fds[POLL_NFDS];
	struct input_event ev;
	ssize_t n;

	if ((file = open(filename, O_RDWR)) < 0) {
		perror("Failed to open the i2c bus.");
		exit(EXIT_FAILURE);
	}

	if (ioctl(file, I2C_SLAVE, IS31FL3728_ADDR) < 0) {
		perror("Failed to acquire bus access and/or talk to slave.");
		exit(EXIT_FAILURE);
	}

	buttons_fd = open(INPUT_BUTTONS, O_RDONLY);
	if (buttons_fd == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", INPUT_BUTTONS, strerror(errno));
		exit(EXIT_FAILURE);
	}

	fds[0].fd = buttons_fd;
	fds[0].events = POLLIN;

	led_all_off();

	while (1) {
		rc = poll(fds, POLL_NFDS, -1);

		if (rc < 0) {
			perror("poll() failed\n");
			exit(EXIT_FAILURE);
		}

		n = read(buttons_fd, &ev, sizeof(ev));
		if (n < 0) {
			perror("Failed to read from the i2c bus.");
			exit(EXIT_FAILURE);
		} else if (n < sizeof(ev)) {
			fprintf(stderr, "Only %zd/%d bytes read\n", n, sizeof(ev));
			exit(EXIT_FAILURE);
		}

		if (ev.type == EV_KEY) {
			int b_row = (ev.code - 1) / 4; /* 0 to 3 */
			int b_col = (ev.code - 1) % 4; /* 0 to 3 */
			int led_id = b_row * 7 * 2 + b_col * 2;
			if (ev.value)
				led_state(led_id, true);
			else
				led_state(led_id, false);
		}
	}

	exit(EXIT_SUCCESS);
}
