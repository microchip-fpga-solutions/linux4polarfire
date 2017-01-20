#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INPUT_BUTTONS	"/dev/input/event1"
#define INPUT_SLIDER	"/dev/input/event2"
#define INPUT_WHEEL	"/dev/input/event3"
#define LED_0		"/sys/class/leds/led0/brightness"
#define LED_1		"/sys/class/leds/led1/brightness"
#define LED_2		"/sys/class/leds/led2/brightness"
#define LED_3		"/sys/class/leds/led3/brightness"
#define LED_4		"/sys/class/leds/led4/brightness"
#define LED_5		"/sys/class/leds/led5/brightness"
#define LED_6		"/sys/class/leds/led6/brightness"
#define LED_7		"/sys/class/leds/led7/brightness"
#define LED_8		"/sys/class/leds/led8/brightness"
#define LED_9		"/sys/class/leds/led9/brightness"
#define LED_RED		"/sys/class/leds/red/brightness"
#define LED_GREEN	"/sys/class/leds/green/brightness"
#define LED_BLUE	"/sys/class/leds/blue/brightness"

#define BUTTON1_KEYCODE	KEY_1
#define BUTTON2_KEYCODE	KEY_2

#define SLIDER_NLEDS	8
#define WHEEL_NLEDS	3

#define POLL_NFDS	3

#define DEBUG		0
#define dbg(fmt, ...)	\
	do { if (DEBUG) printf(fmt, __VA_ARGS__); } while(0)

int main(void)
{
	int slider_fd, wheel_fd, buttons_fd;
	int slider_led_fds[SLIDER_NLEDS], wheel_led_fds[WHEEL_NLEDS], button1_led_fd, button2_led_fd;
	struct pollfd fds[POLL_NFDS];
	struct input_event ev;
	ssize_t n;
	int rc, i, j;

	/* Open all files needed: input devices and leds brigthness. */

	buttons_fd = open(INPUT_BUTTONS, O_RDONLY);
	if (buttons_fd == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", INPUT_BUTTONS, strerror(errno));
		return EXIT_FAILURE;
	}

	slider_fd = open(INPUT_SLIDER, O_RDONLY);
	if (slider_fd == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", INPUT_SLIDER, strerror(errno));
		return EXIT_FAILURE;
	}

	wheel_fd = open(INPUT_WHEEL, O_RDONLY);
	if (wheel_fd == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", INPUT_WHEEL, strerror(errno));
		return EXIT_FAILURE;
	}

	fds[0].fd = buttons_fd;
	fds[0].events = POLLIN;
	fds[1].fd = slider_fd;
	fds[1].events = POLLIN;
	fds[2].fd = wheel_fd;
	fds[2].events = POLLIN;

	slider_led_fds[0] = open(LED_0, O_WRONLY);
	if (slider_led_fds[0] == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_0, strerror(errno));
		return EXIT_FAILURE;
	}

	slider_led_fds[1] = open(LED_1, O_WRONLY);
	if (slider_led_fds[1] == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_1, strerror(errno));
		return EXIT_FAILURE;
	}

	slider_led_fds[2] = open(LED_2, O_WRONLY);
	if (slider_led_fds[2] == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_2, strerror(errno));
		return EXIT_FAILURE;
	}

	slider_led_fds[3] = open(LED_3, O_WRONLY);
	if (slider_led_fds[3] == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_3, strerror(errno));
		return EXIT_FAILURE;
	}

	slider_led_fds[4] = open(LED_4, O_WRONLY);
	if (slider_led_fds[4] == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_4, strerror(errno));
		return EXIT_FAILURE;
	}

	slider_led_fds[5] = open(LED_5, O_WRONLY);
	if (slider_led_fds[5] == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_5, strerror(errno));
		return EXIT_FAILURE;
	}

	slider_led_fds[6] = open(LED_6, O_WRONLY);
	if (slider_led_fds[6] == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_6, strerror(errno));
		return EXIT_FAILURE;
	}

	slider_led_fds[7] = open(LED_7, O_WRONLY);
	if (slider_led_fds[7] == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_7, strerror(errno));
		return EXIT_FAILURE;
	}

	wheel_led_fds[0] = open(LED_RED, O_WRONLY);
	if (wheel_led_fds[0] == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_RED, strerror(errno));
		return EXIT_FAILURE;
	}

	wheel_led_fds[1] = open(LED_GREEN, O_WRONLY);
	if (wheel_led_fds[1] == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_GREEN, strerror(errno));
		return EXIT_FAILURE;
	}

	wheel_led_fds[2] = open(LED_BLUE, O_WRONLY);
	if (wheel_led_fds[2] == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_BLUE, strerror(errno));
		return EXIT_FAILURE;
	}

	button1_led_fd = open(LED_8, O_WRONLY);
	if (button1_led_fd == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_8, strerror(errno));
		return EXIT_FAILURE;
	}

	button2_led_fd = open(LED_9, O_WRONLY);
	if (button2_led_fd == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", LED_9, strerror(errno));
		return EXIT_FAILURE;
	}

	while (1) {
		rc = poll(fds, POLL_NFDS, -1);

		if (rc < 0) {
			fprintf(stderr, "poll() failed\n");
			break;
		}

		for (i = 0; i < POLL_NFDS; i++) {
			if (fds[i].revents == 0)
				continue;

			if (fds[i].revents != POLLIN) {
				fprintf(stderr, "error, revents = %d\n", fds[i].revents);
				break;
			}

			/* Get buttons event. */
			if (fds[i].fd == buttons_fd) {
				n = read(buttons_fd, &ev, sizeof(ev));
				if (n == (ssize_t) - 1) {
						break;
				} else {
					if (n != sizeof(ev)) {
						errno = EIO;
						break;
					}
				}

				dbg("Buttons event: type=%d, code=%d, value=%d\n", ev.type, ev.code, ev.value);

				/* Button 1 */
				if (ev.type == EV_KEY && ev.code == BUTTON1_KEYCODE) {
					if (ev.value == 0)
						write(button1_led_fd, "0", 1);
					else
						write(button1_led_fd, "1", 1);
				}

				/* Button 2 */
				if (ev.type == EV_KEY && ev.code == BUTTON2_KEYCODE) {
					if (ev.value == 0)
						write(button2_led_fd, "0", 1);
					else
						write(button2_led_fd, "1", 1);
				}
			}

			/* Get slider event. */
			if (fds[i].fd == slider_fd) {
				n = read(slider_fd, &ev, sizeof(ev));
				if (n == (ssize_t) - 1) {
						break;
				} else {
					if (n != sizeof(ev)) {
						errno = EIO;
						break;
					}
				}

				dbg("Slider event: type=%d, code=%d, value=%d\n", ev.type, ev.code, ev.value);

				/* Slider touch. */
				if (ev.type == EV_KEY) {
					if (ev.value == 0)
						for (j = 0; j < SLIDER_NLEDS; j++)
							write(slider_led_fds[j], "0", 1);
				}

				/* Slider position. */
				if (ev.type == EV_ABS) {
					/*
					 * Values from 0 to 255, split it into 8 parts,
					 * update it if resolution is different.
					 */
					for (j = 0; j < ev.value / 32 + 1; j++)
						write(slider_led_fds[j], "1", 1);
					for (; j < SLIDER_NLEDS; j++)
						write(slider_led_fds[j], "0", 1);
				}
			}

			/* Get wheel event. */
			if (fds[i].fd == wheel_fd) {
				n = read(wheel_fd, &ev, sizeof(ev));
				if (n == (ssize_t) - 1) {
						break;
				} else {
					if (n != sizeof(ev)) {
						errno = EIO;
						break;
					}
				}

				//dbg("Wheel event: type=%d, code=%d, value=%d\n", ev.type, ev.code, ev.value);

				/* Wheel touch. */
				if (ev.type == EV_KEY) {
					if (ev.value == 0)
						for (j = 0; j < WHEEL_NLEDS; j++)
							write(wheel_led_fds[j], "0", 1);
				}

				/* Wheel position. */
				if (ev.type == EV_ABS) {
					/*
					 * Values from 0 to 63, split it into 7 parts,
					 * update it if resolution is different.
					 */
					int value = ev.value / 10 + 1;
					for (j = 0; j < WHEEL_NLEDS; j++)
						write(wheel_led_fds[j], ((value >> j) & 0x1) ? "1" : "0", 1);
				}
			}
		}
	}

	fprintf(stderr, "%s.\n", strerror(errno));

	return EXIT_FAILURE;
}
