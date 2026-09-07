// SPDX-License-Identifier: GPL-2.0-only

#include <linux/math.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/pwm.h>

#define DRIVER_NAME "rgb_led"
#define BUF_SIZE 64
#define R_IDX 0
#define G_IDX 1
#define B_IDX 2

static char *rgb_str[3] = { "r", "g", "b" };

struct rgb_led_data {
	struct cdev cdev;
	struct device *dev;
	struct pwm_device *pwm_rgb[3];
};

enum rgb_led_col {
	RGB_COL_R,
	RGB_COL_G,
	RGB_COL_B
};

enum rgb_led_op_type {
	RGB_OP_SET,
	RGB_OP_ADD,
	RGB_OP_SUB
};

struct rgb_led_command {
	enum rgb_led_col col;
	enum rgb_led_op_type op;
	int val;
};

static struct class *rgb_led_class;

static ssize_t rgb_led_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	return -EOPNOTSUPP;
}

static int parse_command(const char *kbuf, size_t count, struct rgb_led_data *data)
{
	struct rgb_led_command cmd;
	const char *p = kbuf;
	const char *const end = kbuf + count;

	while (p < end) {
		while (p < end && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n'))
			++p;

		if (p == end)
			break;

		// col
		switch (*p) {
		case 'R':
		case 'r':
			cmd.col = RGB_COL_R;
			++p;
			break;

		case 'G':
		case 'g':
			cmd.col = RGB_COL_G;
			++p;
			break;

		case 'B':
		case 'b':
			cmd.col = RGB_COL_B;
			++p;
			break;

		default:
			pr_err("[%s:%d] command has no color.");
			return -EINVAL;
		}

		while (p < end && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n'))
			++p;

		if (p == end)
			break;

		// operator
		cmd.op = RGB_OP_SET;
		if (!strncmp("+=", p, 2)) {
			cmd.op = RGB_OP_ADD;
			p += 2;
		} else if (!strncmp("-=", p, 2)) {
			cmd.op = RGB_OP_SUB;
			p += 2;
		} else if (!strncmp("=", p, 1)) {
			++p;
		}

		while (p < end && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n'))
			++p;

		if (p == end)
			break;

		// number
		const char *digit_start = p;
		char digit_buf[32];

		if (p < end && (*p == '+' || *p == '-'))
			++p;

		while (p < end && (*p >= '0' && *p <= '9'))
			++p;

		int digit_cnt = p - digit_start;

		if (digit_cnt == 0 || (*p != ' ' && *p != ',' && *p != '\t' && *p != '\0' && *p != '\n')) {
			pr_err("[%s:%d] command has no number.");
			return -EINVAL;
		}

		if (digit_cnt >= sizeof(digit_buf)) {
			pr_err("[%s:%d] too long digit count of number.", __func__, __LINE__);
			return -EINVAL;
		}

		memcpy(digit_buf, digit_start, digit_cnt);
		digit_buf[digit_cnt] = '\0';
		if (kstrtoint(digit_buf, 0, &cmd.val) != 0) {
			pr_warn("[%s:%d] wrong input number : %s", digit_buf);
			return -EINVAL;
		}
		cmd.val = max(0, cmd.val);
		cmd.val = min(100, cmd.val);

		// execute cmd
		int current_val;
		struct pwm_state next_state, current_state;
		pwm_get_state(data->pwm_rgb[cmd.col], &current_state);
		next_state = current_state;
		current_val = DIV_ROUND_CLOSEST_ULL(current_state.duty_cycle * 100, current_state.period);

		switch (cmd.op) {
		case RGB_OP_SET:
			next_state.duty_cycle = DIV_ROUND_CLOSEST_ULL(cmd.val * current_state.period, 100);
			break;

		case RGB_OP_ADD:
			next_state.duty_cycle = DIV_ROUND_CLOSEST_ULL(min(100, current_val + cmd.val) * current_state.period, 100);
			break;

		case RGB_OP_SUB:
			next_state.duty_cycle = DIV_ROUND_CLOSEST_ULL(max(0, current_val - cmd.val) * current_state.period, 100);
			break;
		}

		pwm_apply_might_sleep(data->pwm_rgb[cmd.col], &next_state);
		pr_info("command executed.");

		while (p < end && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n'))
			++p;
	}

	return count;
}

static ssize_t rgb_led_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	char kbuf[BUF_SIZE];
	struct rgb_led_data *data = filp->private_data;

	if (data == NULL) {
		pr_info("failed to get valid data.\n");
		return -ENODEV;
	}

	dev_dbg(data->dev, "%s\n", __func__);

	if (count > sizeof(kbuf) - 1) {
		dev_err(data->dev, "count > %d\n", sizeof(kbuf) - 1);
		return -EINVAL;
	}

	if (copy_from_user(kbuf, buf, count)) {
		dev_err(data->dev, "copy_from_user() failed\n");
		return -EFAULT;
	}
	kbuf[count] = '\0';

	return parse_command(kbuf, count, data);
}

static int rgb_led_open(struct inode *inode, struct file *filp)
{
	struct rgb_led_data *data = container_of(inode->i_cdev, struct rgb_led_data, cdev);
	dev_dbg(data->dev, "%s\n", __func__);
	filp->private_data = data;
	return 0;
}

static int rgb_led_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations rgb_led_fops = {
	.owner = THIS_MODULE,
	.read = rgb_led_read,
	.write = rgb_led_write,
	.open = rgb_led_open,
	.release = rgb_led_release,
};

static int rgb_led_probe(struct platform_device *pdev)
{
	dev_t devno;
	int ret;
	// per-device data 초기화
	struct rgb_led_data *data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL); // dev와 memory를 묶어서 관리

	if (!data)
		return dev_err_probe(&pdev->dev, -ENOMEM, "failed to get rgb_led_data memory.\n");

	data->dev = &pdev->dev;
	dev_dbg(data->dev, "%s\n", __func__);

	for (int i = 0; i < ARRAY_SIZE(data->pwm_rgb); i++) {
		data->pwm_rgb[i] = devm_pwm_get(data->dev, rgb_str[i]);

		if (IS_ERR(data->pwm_rgb[i]))
			return dev_err_probe(&pdev->dev, PTR_ERR(data->pwm_rgb[i]), "failed to get '%s' pwm_device.\n", rgb_str[i]);

		struct pwm_state state;
		pwm_init_state(data->pwm_rgb[i], &state);
		state.duty_cycle = 0;
		state.enabled = true;
		pwm_apply_might_sleep(data->pwm_rgb[i], &state);
	}

	platform_set_drvdata(pdev, data);

	// cdev 초기화
	ret = alloc_chrdev_region(&devno, 0, 1, "rgb-led");
	if (ret)
		return dev_err_probe(data->dev, ret, "failed to allocate chrdev region.\n");

	cdev_init(&data->cdev, &rgb_led_fops);
	data->cdev.owner = THIS_MODULE;
	ret = cdev_add(&data->cdev, devno, 1);
	if (ret) {
		unregister_chrdev_region(devno, 1);
		return dev_err_probe(data->dev, ret, "failed to add cdev.\n");
	}

	// 장치 파일 생성
	struct device *dev = device_create(rgb_led_class, &pdev->dev, devno, data, "rgb-led");
	if (IS_ERR(dev)) {
		cdev_del(&data->cdev);
		unregister_chrdev_region(devno, 1);
		return dev_err_probe(data->dev, PTR_ERR(dev), "failed to create device node.\n");
	}

	return 0;
}

// probe() 실패 시 remove는 호출되지 않음
static void rgb_led_remove(struct platform_device *pdev)
{
	struct rgb_led_data *data = platform_get_drvdata(pdev);

	// pwm 종료
	for (int i = 0; i < 3; ++i) {
		struct pwm_state state;
		pwm_get_state(data->pwm_rgb[i], &state);
		state.duty_cycle = 0;
		state.enabled = false;
		pwm_apply_might_sleep(data->pwm_rgb[i], &state);
	}

	// 장치 파일 삭제
	device_destroy(rgb_led_class, data->cdev.dev);

	// cdev 해제
	dev_t devno = data->cdev.dev;
	cdev_del(&data->cdev);
	unregister_chrdev_region(devno, 1);

	dev_dbg(data->dev, "%s\n", __func__);
}

static const struct of_device_id rgb_led_of_match_table[] = {
	{ .compatible = "heehan,rgb_led" },
	{},
};
MODULE_DEVICE_TABLE(of, rgb_led_of_match_table);

static struct platform_driver rgb_led_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = rgb_led_of_match_table,
	},
	.probe = rgb_led_probe,
	.remove = rgb_led_remove
};

static int __init rgb_led_init(void)
{
	rgb_led_class = class_create("rgb-led");
	if (IS_ERR(rgb_led_class)) {
		class_destroy(rgb_led_class);
		return PTR_ERR(rgb_led_class);
	}

	return platform_driver_register(&rgb_led_driver);
}

static void __exit rgb_led_exit(void)
{
	platform_driver_unregister(&rgb_led_driver);
	class_destroy(rgb_led_class);
}

module_init(rgb_led_init);
module_exit(rgb_led_exit);
MODULE_LICENSE("GPL");
