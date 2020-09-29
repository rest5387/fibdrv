#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include "bignum.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 100

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);
static ktime_t kt;

static inline void add_bigN(bigN *sum, const bigN x, const bigN y)
{
    sum->upper = x.upper + y.upper;
    if (y.lower > ~x.lower)
        sum->upper++;
    sum->lower = x.lower + y.lower;
}

// sum = x - y.
static inline void sub_bigN(bigN *sum, const bigN x, const bigN y)
{
    if ((x.upper >= y.upper) && (x.lower >= y.lower)) {
        sum->upper = x.upper - y.upper;
        sum->lower = x.lower - y.lower;
    } else if ((x.upper >= y.upper) && (y.lower > x.lower)) {
        sum->upper = x.upper - y.upper - 1;
        sum->lower = x.lower + ~y.lower + 1;
    } else {
        /*y is bigger than x, sum can't be stored in bigN
         * set sum be zero.*/
        sum->upper = 0;
        sum->lower = 0;
    }
}
static inline void multiply_bigN(bigN *output,
                                 bigN multiplier,
                                 bigN multiplicand)
{
    int k = 8 * sizeof(unsigned long long);
    bigN product = {.upper = 0, .lower = 0};

    for (int i = 0; i < k; i++) {
        if (multiplier.lower >> i & 1) {
            bigN temp;
            temp.upper = multiplicand.upper << i;
            temp.lower = multiplicand.lower << i;
            temp.upper += (i == 0) ? 0 : multiplicand.lower >> (k - i);
            add_bigN(&product, product, temp);
        }
    }

    for (int i = 0; i < k; i++) {
        if (multiplier.upper >> i & 1)
            product.upper += multiplicand.lower << i;
    }

    output->upper = product.upper;
    output->lower = product.lower;
}

static bigN fib_sequence(long long k)
{
    bigN tmp1, tmp2, tmp3;
    tmp1.lower = 0;
    tmp1.upper = 0;
    tmp2.lower = 1;
    tmp2.upper = 0;

    if (k == 0)
        return tmp1;
    if (k == 1)
        return tmp2;

    for (int i = 2; i <= k; i++) {
        add_bigN(&tmp3, tmp1, tmp2);
        tmp1 = tmp2;
        tmp2 = tmp3;
    }
    return tmp3;
}

static bigN fib_sequence_fast_doubling(long long k)
{
    bigN a = {.upper = 0, .lower = 0};
    bigN b = {.upper = 0, .lower = 1};
    bigN temp1 = {.upper = 0, .lower = 0};
    bigN temp2 = {.upper = 0, .lower = 0};
    bigN temp3 = {.upper = 0, .lower = 0};

    for (int i = 62; i >= 0; i--) {
        // temp1 = a*(2b-a)
        add_bigN(&temp3, b, b);
        sub_bigN(&temp3, temp3, a);
        multiply_bigN(&temp1, a, temp3);
        // temp2 = b*b + a*a;
        multiply_bigN(&temp2, b, b);
        multiply_bigN(&temp3, a, a);
        add_bigN(&temp2, temp2, temp3);
        a = temp1;
        b = temp2;
        if ((k >> i) & 1) {
            add_bigN(&temp1, a, b);
            a = b;
            b = temp1;
        }
    }

    return a;
}
static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
// static ssize_t fib_read(struct file *file,
//                        char *buf,
//                        size_t size,
//                        loff_t *offset)
//{
//    // return (ssize_t) fib_sequence(*offset);
//    return (ssize_t) fib_fast_doubling(*offset);
//    //return  fib_sequence(*offset);
//}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char __user *buf,
                        size_t size,
                        loff_t *offset)
{
    bigN fib_seq;

    kt = ktime_get();
    fib_sequence(*offset);
    kt = ktime_sub(ktime_get(), kt);
    long long tmp_kt_ns = ktime_to_ns(kt);

    kt = ktime_get();
    fib_seq = fib_sequence_fast_doubling(*offset);
    kt = ktime_sub(ktime_get(), kt);
    fib_seq.fib_fd_cost_time_ns = ktime_to_ns(kt);
    fib_seq.fib_cost_time_ns = tmp_kt_ns;

    if (copy_to_user((char __user *) buf, &fib_seq, size))
        return -EFAULT;

    return (ssize_t) sizeof(fib_seq);
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    cdev_init(fib_cdev, &fib_fops);
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
