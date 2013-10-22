#include "chardev.h"

/* Globals localized to file (by use of static */
static int Major;		/* assigned to device driver */
static char msg[BUF_LEN];	/* a stored message */

static struct request *task_queue = NULL;		//head of linked_list for mode 5 & 6

//struct used to send requests to threads
static struct request {
	char fd;
	struct request *next;
};

static struct file_operations fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release
};

static int device_open(struct inode *inode, struct file *file)
{
	try_module_get(THIS_MODULE);
	return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
	module_put(THIS_MODULE);
	//deallocate the space for the head
	return 0;
}

/* Called when a process writes to dev file: echo "hi" > /dev/hello */
static ssize_t device_write(struct file *filp, const char *buff,
			    size_t len, loff_t * off)
{
	int copy_len = len > BUF_LEN ? BUF_LEN : len;
	unsigned long amnt_copied = 0;

	//at some point append the buff/msg to the list

	/* NOTE: copy_from_user returns the amount of bytes _not_ copied */
	amnt_copied = copy_from_user(msg, buff, copy_len);
	if (copy_len == amnt_copied)
		return -EINVAL;
	struct request *new_request = kmalloc(sizeof(struct request), GFP_KERNEL);
	new_request->fd = msg;
	new_request->next = task_queue;
	task_queue = new_request;
	//create linked list, add (int) msg to the linked list
	return copy_len - amnt_copied;
}

static ssize_t device_read(struct file *filp, char *buffer, size_t len,
			   loff_t * offset)
{
	unsigned long amnt_copied;
	int amnt_left = BUF_LEN - *offset;
	char *copy_position = msg + *offset;
	int copy_len = len > amnt_left ? amnt_left : len;
	struct request *temp;


	/* are we at the end of the buffer? */
	if (amnt_left <= 0)
		return 0;

	msg[0] = task_queue->fd;
	printk("the task_queue is %c, while msg[0] is %c\n", task_queue->fd, msg[0]);
	/* NOTE: copy_to_user returns the amount of bytes _not_ copied */
	amnt_copied = copy_to_user(buffer, copy_position, copy_len);
	if (copy_len == amnt_copied)
		return -EINVAL;

	temp = task_queue;
	task_queue = task_queue->next;
	kfree(temp);
	/* adjust the offset for this process */
	*offset += copy_len;

	return copy_len - amnt_copied;
}

int init_module(void)
{
	Major = register_chrdev(0, DEVICE_NAME, &fops);

	if (Major < 0) {
		printk(KERN_ALERT "Failed to register char device.\n");
		return Major;
	}

	memset(msg, '+', BUF_LEN);
	printk(KERN_INFO "chardev is assigned to major number %d.\n",
	       Major);

	return 0;
}
void cleanup_module(void)
{
	int ret = unregister_chrdev(Major, DEVICE_NAME);
	if (ret < 0)
		printk(KERN_ALERT "Error in unregister_chrdev: %d\n", ret);
}
