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

static struct class *rgb_led_class;

static ssize_t rgb_led_read(struct file *filp, char __user *buf, size_t count,
			    loff_t *f_pos)
{
	return -EOPNOTSUPP;
}

static ssize_t rgb_led_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *f_pos)
{
	char kbuf[BUF_SIZE];
	struct rgb_led_data *data = filp->private_data;

	if (data == NULL)
		return -ENODEV;

	dev_info(data->dev, "%s\n", __func__);

	if (count > sizeof(kbuf) - 1) {
		dev_warn(data->dev, "count > %d\n", sizeof(kbuf) - 1);
		return -EINVAL;
	}

	if (copy_from_user(kbuf, buf, count)) {
		dev_err(data->dev, "copy_from_user() failed\n");
		return -EFAULT;
	}
	kbuf[count] = '\0';

	char *p = kbuf;
	char *tok;
	int RGB = -1;
	u64 val[3] = { 0, 0, 0 };
	bool is_val_changed[3] = { false, false, false };

	while ((tok = strsep(&p, " ,=\t\n")) != NULL) {
		if (tok[0] == '\0')
			continue;
		else if (RGB >= 0 && RGB < 3) {
			if (kstrtou64(tok, 0, &val[RGB]) != 0) {
				dev_info(data->dev, "kstrtoint failed\n");
				return -EINVAL;
			}

			is_val_changed[RGB] = true;
			RGB = -1;
			continue;
		} else if (strcmp(tok, "R") == 0) {
			RGB = R_IDX;
			continue;
		} else if (strcmp(tok, "G") == 0) {
			RGB = G_IDX;
			continue;
		} else if (strcmp(tok, "B") == 0) {
			RGB = B_IDX;
			continue;
		} else {
			dev_info(data->dev, "invalid token: %s\n", tok);
			return -EINVAL;
		}
	}
	if (RGB != -1) {
		dev_info(data->dev, "missing value for %s\n", rgb_str[RGB]);
		return -EINVAL;
	}

	for (int i = 0; i < 3; ++i) {
		if (is_val_changed[i]) {
			struct pwm_state state;
			pwm_get_state(data->pwm_rgb[i], &state);
			state.duty_cycle = DIV_ROUND_CLOSEST_ULL(
				state.period * min(val[i], 100), 100);
			pwm_apply_might_sleep(data->pwm_rgb[i], &state);

			dev_info(data->dev, "%s's duty-cycle is set to %llu\n",
				 rgb_str[i], state.duty_cycle);

		} else {
			dev_info(data->dev, "%s is not changed\n", rgb_str[i]);
		}
	}

	return count;
}

static int rgb_led_open(struct inode *inode, struct file *filp)
{
	struct rgb_led_data *data =
		container_of(inode->i_cdev, struct rgb_led_data, cdev);

	dev_info(data->dev, "%s\n", __func__);
	filp->private_data = data;
	return 0;
}

static int rgb_led_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations rgb_led_fops = { .owner = THIS_MODULE,
						     .read = rgb_led_read,
						     .write = rgb_led_write,
						     .open = rgb_led_open,
						     .release =
							     rgb_led_release };

static int rgb_led_probe(struct platform_device *pdev)
{
	dev_t devno;
	int ret;
	// per-device data 초기화
	struct rgb_led_data *data =
		devm_kzalloc(&pdev->dev, sizeof(*data),
			     GFP_KERNEL); // dev와 memory를 묶어서 관리

	if (!data)
		return dev_err_probe(&pdev->dev, -ENOMEM,
				     "failed to get rgb_led_data memory.\n");

	data->dev = &pdev->dev;
	dev_info(data->dev, "%s\n", __func__);

	for (int i = 0; i < ARRAY_SIZE(data->pwm_rgb); i++) {
		data->pwm_rgb[i] = devm_pwm_get(data->dev, rgb_str[i]);

		if (IS_ERR(data->pwm_rgb[i]))
			return dev_err_probe(&pdev->dev,
					     PTR_ERR(data->pwm_rgb[i]),
					     "failed to get '%s' pwm_device.\n",
					     rgb_str[i]);

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
		return dev_err_probe(data->dev, ret,
				     "failed to allocate chrdev region.\n");

	cdev_init(&data->cdev, &rgb_led_fops);
	data->cdev.owner = THIS_MODULE;
	ret = cdev_add(&data->cdev, devno, 1);
	if (ret) {
		unregister_chrdev_region(devno, 1);
		return dev_err_probe(data->dev, ret, "failed to add cdev.\n");
	}

	// 장치 파일 생성
	struct device *dev = device_create(rgb_led_class, &pdev->dev, devno,
					   data, "rgb-led");
	if (IS_ERR(dev)) {
		cdev_del(&data->cdev);
		unregister_chrdev_region(devno, 1);
		return dev_err_probe(data->dev, PTR_ERR(dev),
				     "failed to create device node.\n");
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

	dev_info(data->dev, "%s\n", __func__);
}

static const struct of_device_id rgb_led_of_match_table[] = {
	{ .compatible = "heehan,rgb_led" },
	{},
};
MODULE_DEVICE_TABLE(of, rgb_led_of_match_table);

static struct platform_driver rgb_led_driver = {
	.driver = { .name = DRIVER_NAME,
		    .of_match_table = rgb_led_of_match_table },
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
