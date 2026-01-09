/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

#define WORKING_ENTRY_INITIAL_SIZE 256

MODULE_AUTHOR("Daniel Pardo");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

    // Set filp->private_data to point to the aesd_dev structure
    filp->private_data = container_of(inode->i_cdev, struct aesd_dev, cdev);
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

/**
 * @brief Read function for the AESD char driver.
 * @param filp A pointer to the file structure.
 * @param buf A pointer to the user buffer to read data into.
 * @param count The number of bytes to read.
 * @param f_pos A pointer to the file position offset.
 * @return The number of bytes read, or a negative error code (-ERESTARTSYS, -EINTR, -EFAULT).
 */
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    struct aesd_dev* dev = filp->private_data;
    mutex_lock(&dev->mutex);

    // Find the entry corresponding to f_pos
    size_t entry_offset_byte;
    struct aesd_buffer_entry* entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, (size_t)*f_pos, &entry_offset_byte);
    if (entry != NULL)
    {
        PDEBUG("Found entry \"%s\" at f_pos %lld", entry->buffptr, *f_pos);

        // Determine number of bytes to copy
        size_t bytes_to_copy = min(count, entry->size);

        // Copy to user buffer
        if (copy_to_user(buf, entry->buffptr, bytes_to_copy) != 0)
        {
            retval = -EFAULT;
        }
        else
        {
            // Update f_pos by the number of bytes read
            *f_pos += bytes_to_copy;
            retval = bytes_to_copy;
            kfree(entry->buffptr);
        }
    }

    mutex_unlock(&dev->mutex);
    return retval;
}

/**
 * @brief Write function for the AESD char driver.
 * @param filp A pointer to the file structure.
 * @param buf A pointer to the user buffer containing data to write.
 * @param count The number of bytes to write.
 * @param f_pos A pointer to the file position offset. Note, this is ignored for now.
 * @return The number of bytes written, or a negative error code (-EFAULT, -ENOMEM).
 */
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    
    struct aesd_dev* dev = filp->private_data;
    mutex_lock(&dev->mutex);

    // Append data to working_entry
    dev->working_entry.buffptr = krealloc(dev->working_entry.buffptr, dev->working_entry.size + count, GFP_KERNEL);
    if (dev->working_entry.buffptr == NULL)
    {
        mutex_unlock(&dev->mutex);
        return -ENOMEM;
    }
    if (copy_from_user(&dev->working_entry.buffptr[dev->working_entry.size], buf, count) != 0)
    {
        kfree(dev->working_entry.buffptr);
        mutex_unlock(&dev->mutex);
        return -EFAULT;
    }
    dev->working_entry.size += count;
    retval = count;

    // Check for newline characters and add to circular buffer
    if (dev->working_entry.buffptr[dev->working_entry.size - 1] == '\n')
    {
        PDEBUG("Adding entry \"%s\" to circular buffer", dev->working_entry.buffptr);

        // Add the working entry to the circular buffer
        aesd_circular_buffer_add_entry(&dev->buffer, &dev->working_entry);
        
        // Reset working_entry
        dev->working_entry.buffptr = NULL;
        dev->working_entry.size = 0;
    }

    mutex_unlock(&dev->mutex);
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    // Initialize the AESD specific portion of the device
    aesd_circular_buffer_init(&aesd_device.buffer);
    memset(&aesd_device.working_entry, 0, sizeof(struct aesd_buffer_entry));
    mutex_init(&aesd_device.mutex);

    result = aesd_setup_cdev(&aesd_device);
    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    // Free any allocated buffer entries
    kfree(aesd_device.working_entry.buffptr);
    
    // Free circular buffer entries
    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; ++i)
    {
        kfree(aesd_device.buffer.entry[i].buffptr);
    }

    // Destroy mutex
    mutex_destroy(&aesd_device.mutex);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
