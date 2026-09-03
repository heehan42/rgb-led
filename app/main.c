#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define RGB_LED_DEV_NODE "/dev/rgb-led"
#define RE_DEV_NODE "/dev/rotary_encoder"

#define READ_BUF_SZ 16

int main()
{
	int rgb_led_fd, re_fd;
	char read_buf[READ_BUF_SZ];
	ssize_t read_ret, write_ret;

	rgb_led_fd = open(RGB_LED_DEV_NODE, O_RDONLY);
	if (rgb_led_fd < 0) {
		perror("open /dev/rgb-led");
		return EXIT_FAILURE;
	}

	re_fd = open(RE_DEV_NODE, O_RDONLY);
	if (re_fd < 0) {
		perror("open /dev/rotary-encoder");
		close(rgb_led_fd);
		return EXIT_FAILURE;
	}

	while (1) {
		read_ret = read(re_fd, read_buf, sizeof(read_buf) - 1);
		if (read_ret < 0) {
			close(rgb_led_fd);
			close(re_fd);
			perror("failed to read re_fd");
			return read_ret;
		} else if (read_ret == 0) {
			fprintf(stdout, "rotary encoder EOF\n");
			break;
		}
		read_buf[read_ret] = '\0';

		// handle event
		if (!strcmp(read_buf, "CW")) {
		} else if (!strcmp(read_buf, "CCW")) {
		} else if (!strcmp(read_buf, "DOWN")) {
		} else if (!strcmp(read_buf, "UP")) {
		}
	}

	close(rgb_led_fd);
	close(re_fd);
	return EXIT_SUCCESS;
}
