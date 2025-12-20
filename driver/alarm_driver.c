#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#define GPIO_PIN (517) // GPIO 5 (RPi 3B+ Kernel 6.12)
#define BLINK_PERIOD_MS (200) 

dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;
static struct timer_list blink_timer;

// 新增：倒數計數器 (-1 代表無限，0 代表停止，>0 代表剩餘次數)
static int blink_counter = 0; 

static int __init etx_driver_init(void);
static void __exit etx_driver_exit(void);
static int etx_open(struct inode *inode, struct file *file);
static int etx_release(struct inode *inode, struct file *file);
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);

static struct file_operations fops = {
    .owner      = THIS_MODULE,
    .write      = etx_write,
    .open       = etx_open,
    .release    = etx_release,
};

// --- 計時器中斷函式 ---
void timer_callback(struct timer_list * data) {
    int current_value;
    
    // 1. 檢查是否需要停止
    if (blink_counter == 0) {
        gpio_set_value(GPIO_PIN, 0); // 確保燈滅
        return; // 不再設定計時器，結束閃爍
    }

    // 2. 閃爍邏輯
    current_value = gpio_get_value(GPIO_PIN);
    gpio_set_value(GPIO_PIN, !current_value); 

    // 3. 處理倒數
    if (blink_counter > 0) {
        blink_counter--; // 扣除一次
    }

    // 4. 重新設定計時器
    mod_timer(&blink_timer, jiffies + msecs_to_jiffies(BLINK_PERIOD_MS));
}

static int etx_open(struct inode *inode, struct file *file) { return 0; }
static int etx_release(struct inode *inode, struct file *file) { return 0; }

// --- 寫入函式 ---
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    uint8_t rec_buf[10] = {0};
    
    if (copy_from_user(rec_buf, buf, len) > 0) {
        pr_err("ERROR: Copy from user failed\n");
    }
    
    if (rec_buf[0] == '1') {
        // '1': 無限閃爍
        blink_counter = -1; 
        mod_timer(&blink_timer, jiffies + msecs_to_jiffies(BLINK_PERIOD_MS));
        
    } else if (rec_buf[0] == '5') {
        // '5': 閃爍 5 秒
        // 計算：5000ms / 200ms = 25 次 (但因為亮滅各算一次，可能需要調整，這裡以觸發次數計算)
        // 為了讓它閃 5 秒，大約需要觸發 25 次 (假設 200ms 換一次狀態)
        blink_counter = 25; 
        mod_timer(&blink_timer, jiffies + msecs_to_jiffies(BLINK_PERIOD_MS));
        
    } else if (rec_buf[0] == '0') {
        // '0': 強制停止
        blink_counter = 0;
        del_timer(&blink_timer);
        gpio_set_value(GPIO_PIN, 0);
    }
    
    return len;
}

static int __init etx_driver_init(void) {
    if ((alloc_chrdev_region(&dev, 0, 1, "etx_alarm")) < 0) return -1;
    cdev_init(&etx_cdev, &fops);
    cdev_add(&etx_cdev, dev, 1);
    dev_class = class_create("etx_alarm_class");
    if (IS_ERR(dev_class)) { unregister_chrdev_region(dev, 1); return -1; }
    device_create(dev_class, NULL, dev, NULL, "etx_alarm");

    gpio_request(GPIO_PIN, "GPIO_ALARM");
    gpio_direction_output(GPIO_PIN, 0);
    timer_setup(&blink_timer, timer_callback, 0);

    pr_info("Alarm Driver (Auto-Stop) Loaded...\n");
    return 0;
}

static void __exit etx_driver_exit(void) {
    del_timer(&blink_timer);
    gpio_set_value(GPIO_PIN, 0);
    gpio_free(GPIO_PIN);
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&etx_cdev);
    unregister_chrdev_region(dev, 1);
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);
MODULE_LICENSE("GPL");
