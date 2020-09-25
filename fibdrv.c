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

static inline void add_bigN(bigN *sum, const bigN x, const bigN y)
{
    // printk(KERN_EMERG "tmp1.upper = %llu, tmp1.lower = %llu.\n", x.upper,
    // x.lower); printk(KERN_EMERG "tmp2.upper = %llu, tmp2.lower = %llu.\n",
    // y.upper, y.lower);
    sum->upper = x.upper + y.upper;
    if (y.lower > ~x.lower)
        sum->upper++;
    sum->lower = x.lower + y.lower;
    // printk(KERN_EMERG "sum.upper = %lld, sum.lower = %lld.\n", sum->upper,
    // sum->lower);
}


// static long long fib_sequence(long long k)
//{
/* FIXME: use clz/ctz and fast algorithms to speed up */
/*    long long f[k + 2];

    f[0] = 0;
    f[1] = 1;

    for (int i = 2; i <= k; i++) {
        f[i] = f[i - 1] + f[i - 2];
    }

    return f[k];
}
*/
/*
static long long fib_fast_doubling(long long k)
{
    long long a = 0, b = 1;
    // long long temp1, temp2;
    for (int i = 31; i >= 0; i--) {
        long long temp1 = a * (2 * b - a);
        long long temp2 = b * b + a * a;
        long long temp3 = 1;
        a = temp1;
        b = temp2;
        if ((k & (temp3 << i)) > 0) {  // if current bit = 1
            temp1 = a + b;
            a = b;
            b = temp1;
        }
    }
    return a;
}
*/
static bigN fib_sequence(long long k)
{
    /*bigN f[k+2];

    f[0].lower = 0;
    f[0].upper = 0;
    f[1].lower = 1;
    f[1].upper = 0;

    for (int i = 2; i <= k; i++){
        add_bigN(&f[i], f[i-1], f[i-2]);
    }

    return f[k];*/

    bigN tmp1, tmp2, tmp3;
    tmp1.lower = 0;
    tmp1.upper = 0;
    tmp2.lower = 1;
    tmp2.upper = 0;

    printk(KERN_EMERG "k = %llu.\n", k);
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

// static bigN fib_sequence_fast_doubling(long long k) {}
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
    // char test_buf[1];
    // return (ssize_t) fib_sequence(*offset);
    // return (ssize_t) fib_fast_doubling(*offset);
    // buf = (char *)fib_sequence(*offset);

    printk(KERN_EMERG "call fib_sequence(offset = %llu) in kernel space\n",
           *offset);
    fib_seq = fib_sequence(*offset);
    // if(access_ok(VERIFY_WRITE, buf, sizeof(fib_seq))){
    // printk("buf access is ok");
    // copy_to_user(buf, &test_buf, sizeof(test_buf));
    //}
    printk(KERN_EMERG "call copy_to_user in kernel space\n");
    if (copy_to_user((char __user *) buf, &fib_seq, size)) {
        printk(KERN_EMERG "copy_to_user fail\n");
        return -EFAULT;
    }
    printk(KERN_INFO "copy_to_user success!\n");
    printk(KERN_EMERG " fib_seq.upper = %25llu,  lower = %25llu\n",
           fib_seq.upper, fib_seq.lower);
    printk(KERN_EMERG "~fib_seq.upper = %25llu, ~lower = %25llu\n",
           ~fib_seq.upper, ~fib_seq.lower);

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
