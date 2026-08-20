#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/gpio/consumer.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define DRIVER_NAME "rgb_led"
#define BUF_SIZE 16
#define R_IDX 0
#define G_IDX 1
#define B_IDX 2

static char *rgb_str[3] = {"r", "g", "b"};

struct rgb_led_data 
{
    struct cdev cdev;
    struct gpio_desc *rgb[3];
};

static struct class *rgb_led_class;

ssize_t rgb_led_read(struct file *, char __user *buf, size_t count, loff_t *f_pos)
{
    pr_info("rgb_led_read()\n");
    return -EOPNOTSUPP;
}

ssize_t rgb_led_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    pr_info("rgb_led_write() | count : %d\n", count);

    char kbuf[BUF_SIZE];
    if (count > sizeof(kbuf) - 1)
    {
        pr_info("count > %d\n", sizeof(kbuf) - 1);
        return -EINVAL;
    }

    struct rgb_led_data *data = filp->private_data;
    if(data == NULL)
    {
        pr_info("data == NULL\n");
        return -ENODEV;
    }

    if(copy_from_user(kbuf, buf, count))
    {
        pr_info("copy_from_user() failed\n");
        return -EFAULT;
    }
    kbuf[count] = '\0';

    char *p = kbuf;
    char *tok;
    int RGB = -1;
    int val[3] = {-1, -1, -1};
    bool is_val_changed[3] = {false, false, false};

    while ((tok = strsep(&p, " ,=\t\n")) != NULL)
    {
        if (tok[0] == '\0')
            continue;
        else if (0 <= RGB && RGB < 3)
        {
            if (kstrtoint(tok, 0, &val[RGB]) != 0)
            {
                pr_info("kstrtoint failed\n");
                return -EINVAL;
            }

            is_val_changed[RGB] = true;
            RGB = -1;
            continue;
        }
        else if(strcmp(tok, "R") == 0)
        {
            RGB = R_IDX;
            continue;
        }
        else if(strcmp(tok, "G") == 0)
        {
            RGB = G_IDX;
            continue;
        }
        else if(strcmp(tok, "B") == 0)
        {
            RGB = B_IDX;
            continue;
        }
        else
        {
            pr_info("invalid token: %s\n", tok);
            return -EINVAL;
        }
    }
    if (RGB != -1)
    {
        pr_info("missing value for %s\n", rgb_str[RGB]);
        return -EINVAL;
    }

    for (int i = 0; i < 3; ++i)
    {
        if (is_val_changed[i]) 
        {
            pr_info("%s is set to %d\n", rgb_str[i], val[i]);
            gpiod_set_value_cansleep(data->rgb[i], val[i]);
        }
        else
        {
            pr_info("%s is not changed\n", rgb_str[i], val[i]);
        }
    }

    return count;
}

int rgb_led_open(struct inode *inode, struct file *filp)
{
    pr_info("rgb_led_open()\n");

    struct rgb_led_data *data;
    data = container_of(inode->i_cdev, struct rgb_led_data, cdev);
    filp->private_data = data;
    return 0;
}

int rgb_led_release(struct inode *inode, struct file *filp)
{
    pr_info("rgb_led_release()\n");
    return 0;
}

struct file_operations rgb_led_fops =
{
    .owner = THIS_MODULE,
    .read = rgb_led_read,
    .write = rgb_led_write,
    .open = rgb_led_open,
    .release = rgb_led_release
};

int rgb_led_probe(struct platform_device * pdev)
{
    pr_info("rgb_led_probe() begin\n");

    // per-device data 초기화
    struct rgb_led_data *data;
    data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL); // dev와 memory를 묶어서 관리
    if(!data)
        return -ENOMEM;

    data->rgb[R_IDX] = devm_gpiod_get(&pdev->dev, "r", GPIOD_OUT_HIGH);
    if (IS_ERR(data->rgb[R_IDX]))
        return PTR_ERR(data->rgb[R_IDX]);

    data->rgb[G_IDX] = devm_gpiod_get(&pdev->dev, "g", GPIOD_OUT_HIGH);
    if (IS_ERR(data->rgb[G_IDX]))
        return PTR_ERR(data->rgb[G_IDX]);

    data->rgb[B_IDX] = devm_gpiod_get(&pdev->dev, "b", GPIOD_OUT_HIGH);
    if (IS_ERR(data->rgb[B_IDX]))
        return PTR_ERR(data->rgb[B_IDX]);

    platform_set_drvdata(pdev, data);

    
    // cdev 초기화
    dev_t devno;
    int ret = alloc_chrdev_region(&devno, 0, 1, "rgb-led");
    if (ret != 0)
        return ret;
    
    cdev_init(&data->cdev, &rgb_led_fops);
    data->cdev.owner = THIS_MODULE;
    ret = cdev_add(&data->cdev, devno, 1);
    if(ret != 0)
        return ret;
    
    // 장치 파일 생성
    device_create(rgb_led_class, &pdev->dev, devno, data, "rgb-led");
    

    pr_info("rgb_led_probe() end\n");
    return 0;
}

void rgb_led_remove(struct platform_device* pdev)
{
    pr_info("rgb_led_remove()\n");
    struct rgb_led_data *data = platform_get_drvdata(pdev);

    // 장치 파일 삭제
    device_destroy(rgb_led_class, data->cdev.dev);

    // cdev 해제
    unregister_chrdev_region(data->cdev.dev, 1);
    cdev_del(&data->cdev);
}

static const struct of_device_id rgb_led_of_match_table[] = {
    { .compatible = "heehan,rgb_led" },
    { }
};
MODULE_DEVICE_TABLE(of, rgb_led_of_match_table);

static struct platform_driver rgb_led_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = rgb_led_of_match_table
    },
    .probe = rgb_led_probe,
    .remove = rgb_led_remove
};


int __init rgb_led_init(void)
{
    pr_info("rgb_led_init()\n");

    rgb_led_class = class_create("rgb-led");
    if (IS_ERR(rgb_led_class))
        return PTR_ERR(rgb_led_class);

    return platform_driver_register(&rgb_led_driver);
}

void __exit rgb_led_exit(void)
{
    pr_info("rgb_led_exit\n");

    platform_driver_unregister(&rgb_led_driver);
    class_destroy(rgb_led_class);
}

module_init(rgb_led_init);
module_exit(rgb_led_exit);
MODULE_LICENSE("GPL");