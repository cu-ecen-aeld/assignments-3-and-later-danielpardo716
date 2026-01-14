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
#include "aesd_ioctl.h"
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
    struct aesd_dev* dev = filp->private_data;
    mutex_lock(&dev->mutex);

    // Find the entry corresponding to f_pos
    size_t entry_offset_byte;
    struct aesd_buffer_entry* entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, (size_t)*f_pos, &entry_offset_byte);
    if (entry != NULL)
    {
        PDEBUG("Found entry \"%s\" at offset %lld", entry->buffptr, *f_pos);

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
    struct aesd_dev* dev = filp->private_data;
    mutex_lock(&dev->mutex);

    // Append data to working_entry
    dev->working_entry.buffptr = krealloc(dev->working_entry.buffptr, dev->working_entry.size + count, GFP_KERNEL);
    if (dev->working_entry.buffptr == NULL)
    {
        mutex_unlock(&dev->mutex);
        return -ENOMEM;
    }
    if (copy_from_user((void*)&dev->working_entry.buffptr[dev->working_entry.size], buf, count) != 0)
    {
        kfree(dev->working_entry.buffptr);
        mutex_unlock(&dev->mutex);
        return -EFAULT;
    }
    dev->working_entry.size += count;
    *f_pos += count;
    retval = count;

    // Check for newline characters and add to circular buffer
    if (dev->working_entry.buffptr[dev->working_entry.size - 1] == '\n')
    {
        PDEBUG("Adding entry \"%s\" to circular buffer", dev->working_entry.buffptr);

        // Add the working entry to the circular buffer
        const char* overwritten_entry = aesd_circular_buffer_add_entry(&dev->buffer, &dev->working_entry);
        if (overwritten_entry != NULL)
        {
            PDEBUG("Overwritten entry: \"%s\"", overwritten_entry);
            kfree((void*)overwritten_entry);
        }
        
        // Reset working_entry
        dev->working_entry.buffptr = NULL;
        dev->working_entry.size = 0;
    }

    mutex_unlock(&dev->mutex);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev* dev = filp->private_data;
    mutex_lock(&dev->mutex);
    loff_t buffer_size = (loff_t)aesd_circular_buffer_get_total_size(&dev->buffer);
    PDEBUG("llseek to offset %lld whence %d buffer_size %lld", offset, whence, buffer_size);
    mutex_unlock(&dev->mutex);
    return fixed_size_llseek(filp, offset, whence, buffer_size);
}

/**
 * @brief Adjust the file offset parameter of @param filp based on the location specified by
 * @param write_cmd (the zero referenced command to locate)
 * and @param write_cmd_offset (the zero referenced offset into the command).
 * @return 0 if successful, negative if the error occurred:
 *  -ERESTARTSYS if mutex could not be obtained.
 *  -EINVAL if the write_cmd or write_cmd_offset was out of range.
 */
static long aesd_adjust_file_offset(struct file* filp, unsigned int write_cmd, unsigned int write_cmd_offset)
{
    // Obtain the mutex
    struct aesd_dev* dev = filp->private_data;
    if (mutex_lock_interruptible(&dev->mutex) != 0)
    {
        return -ERESTARTSYS;
    }

    // Check for valid write_cmd and write_cmd_offset values
    if ((write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) ||       // Out of range
        (dev->buffer.entry[write_cmd].buffptr == NULL) ||               // Haven't written this command yet
        (write_cmd_offset >= dev->buffer.entry[write_cmd].size))        // Offset beyond end of command
    {
        return -EINVAL;
    }

    // Calculate the start offset to write_cmd
    // Add the length of each command between the out_offs and write_cmd
    size_t entry_index = dev->buffer.out_offs;
    size_t current_cmd = 0;
    size_t new_fpos = 0;
    while (current_cmd < write_cmd)
    {
        new_fpos += dev->buffer.entry[entry_index].size;
        entry_index = (entry_index == (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1)) ? 0 : (entry_index + 1);
        ++current_cmd;
    }
    // Add the offset within the command
    new_fpos += write_cmd_offset;

    mutex_unlock(&dev->mutex);
    
    // Update the file position
    filp->f_pos = new_fpos;
    return 0;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long retval = 0;
    switch (cmd)
    {
        case AESDCHAR_IOCSEEKTO:
        {
            struct aesd_seekto seekto;
            retval = (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) == 0)
                ? aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset)
                : -EFAULT;
            break;
        }
        default:
        {
            retval = -EINVAL;
            break;
        }
    }
    return retval;
}

struct file_operations aesd_fops = {
    .owner =            THIS_MODULE,
    .read =             aesd_read,
    .write =            aesd_write,
    .open =             aesd_open,
    .release =          aesd_release,
    .llseek =           aesd_llseek,
    .unlocked_ioctl =   aesd_unlocked_ioctl,
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
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
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
    struct aesd_buffer_entry *entry;
    uint8_t index;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index)
    {
        if (entry->buffptr != NULL)
        {
            kfree(entry->buffptr);
        }
    }
    kfree(aesd_device.working_entry.buffptr);

    mutex_destroy(&aesd_device.mutex);
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
