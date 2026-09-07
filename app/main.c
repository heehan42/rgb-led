#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define RGB_LED_DEV_NODE "/dev/rgb-led"
#define RE_DEV_NODE "/dev/rotary_encoder"

#define READ_BUF_SZ 16
#define COL_R 0
#define COL_G 1
#define COL_B 2
#define NR_COL 3
#define OP_ADD 0
#define OP_SUB 1

int main()
{
	int rgb_led_fd, re_fd;
	char read_buf[READ_BUF_SZ];
	ssize_t read_ret, write_ret;
	int col = COL_R;

	rgb_led_fd = open(RGB_LED_DEV_NODE, O_WRONLY);
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

		printf("[%s:%d] event : %s\n", __func__, __LINE__, read_buf);

		// handle event
		if (!strcmp(read_buf, "CW")) {
			char buf[32];
			const char *col_str;
			switch (col) {
			case COL_R:
				col_str = "R";
				break;
			case COL_G:
				col_str = "G";
				break;
			case COL_B:
				col_str = "B";
				break;
			}

			int len = snprintf(buf, sizeof(buf), "%s+=10", col_str);
			printf("buf(len) : %s(%d)\n", buf, len);
			write(rgb_led_fd, buf, len);

		} else if (!strcmp(read_buf, "CCW")) {
			char buf[32];
			const char *col_str;
			switch (col) {
			case COL_R:
				col_str = "R";
				break;
			case COL_G:
				col_str = "G";
				break;
			case COL_B:
				col_str = "B";
				break;
			}

			int len = snprintf(buf, sizeof(buf), "%s-=10", col_str);
			write(rgb_led_fd, buf, len);

		} else if (!strcmp(read_buf, "DOWN")) {
			col = (col + 1) % NR_COL;
			printf("[%s:%d] col = %d\n", __func__, __LINE__, col);

		} else if (!strcmp(read_buf, "UP")) {
		}
	}

	close(rgb_led_fd);
	close(re_fd);
	return EXIT_SUCCESS;
}
