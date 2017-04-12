#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/types.h>

#define eeprom_addr 0x37
#define eeprom_size_addr 0x00
#define minor_num 246

enum {START, WRITE_ST, READ_ST, READ_ADDR, WRITE_ADDR} state;
enum com {WRITE_OP = 35, READ_OP, SIZE_OPER};

struct i2c_dev {
	struct i2c_client *client;
	dev_t dev;
	struct cdev cdev;
	int state;
	unsigned char size;
	char addr;
};

static int eof_flag;

static struct i2c_dev eeprom_dev;

static int eeprom_open(struct inode *inode, struct file *f)
{
	eof_flag = 0;
	return 0;
}

static int eeprom_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t eeprom_read(struct file *file,
				char *bf,
				size_t length,
				loff_t *offset)
{
	char *temp = kmalloc(length, GFP_KERNEL);
	unsigned int j = 0;
	char i;

	if (temp == NULL)
		return -1;

	switch (eeprom_dev.state) {
	case READ_ST:
	i = eeprom_dev.addr;
	while ((i < eeprom_dev.size) && (j < length)) {
		temp[j] = i2c_smbus_read_byte_data(eeprom_dev.client, i);
		i++;
		j++;
	}
	copy_to_user(bf, temp, j);
	eeprom_dev.state = START;
	kfree(temp);
	return j;

	default:
		kfree(temp);
		return 0;
	}
}

static ssize_t eeprom_write(struct file *file,
				const char *bf,
				size_t length,
				loff_t *offset)
{
	char *temp = kmalloc(length, GFP_KERNEL);
	unsigned int j = 0;
	char i;

	if (temp == NULL)
		return -1;

	// копируем данные с пользователя
	copy_from_user(temp, bf, length);

	switch (eeprom_dev.state) {
	case READ_ADDR:
		if (temp[0] >= eeprom_dev.size)
		return -1;
		eeprom_dev.addr = temp[0];
		eeprom_dev.state = READ_ST;
		kfree(temp);
		return 1;

	case WRITE_ADDR:
		if ((temp[0] >= eeprom_dev.size)
		|| (temp[0] == eeprom_size_addr))
		return -1;
		eeprom_dev.addr = temp[0];
		eeprom_dev.state = WRITE_ST;
		kfree(temp);
		return 1;

	case WRITE_ST:
	i = eeprom_dev.addr;
	while ((i < eeprom_dev.size) && (j < length)) {
		i2c_smbus_write_byte_data(eeprom_dev.client, i, temp[j]);
		i++;
		j++;
	}
	eeprom_dev.state = START;
	kfree(temp);
	return j;

	default:
		kfree(temp);
		return 0;
	}
}

static long eeprom_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case WRITE_OP:
		eeprom_dev.state = WRITE_ADDR;
		break;
	case READ_OP:
		eeprom_dev.state = READ_ADDR;
		break;
	case SIZE_OPER:
		copy_to_user((char *)arg, &eeprom_dev.size, sizeof(char));
		break;
	default:
		return -1;
	}
	return 0;
}

static const struct file_operations eeprom_ops = {
	.owner		= THIS_MODULE,
	.read		= eeprom_read,
	.write		= eeprom_write,
	.open		= eeprom_open,
	.release	= eeprom_release,
	.unlocked_ioctl = eeprom_ioctl
};

static int i2c_master_eeprom_probe
(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;

	eeprom_dev.dev = MKDEV(minor_num, 0);
	ret = register_chrdev_region((eeprom_dev.dev), 1, "i2c_eep");
	//ret = alloc_chrdev_region(&(eeprom_dev.dev), 0, 1, "i2c_eep");
	if (ret < 0) {
		pr_alert("EEPROM: Failed to get a major number\n");
		return -1;
	}
	pr_info("EEPROM: major %d and minor %d\n",
	MAJOR(eeprom_dev.dev), MINOR(eeprom_dev.dev));

	//инициализируем cdev
	cdev_init(&(eeprom_dev.cdev), &eeprom_ops);
	eeprom_dev.cdev.owner = THIS_MODULE;
	ret = cdev_add(&eeprom_dev.cdev, eeprom_dev.dev, 1);
	if (ret < 0) {
		pr_alert("EEPROM: Failed to register cdev\n");
		unregister_chrdev_region(eeprom_dev.dev, 1);
		cdev_del(&eeprom_dev.cdev);
		return -1;
	}

	eeprom_dev.size = (unsigned long)i2c_smbus_read_byte_data(client, 0x00);
	pr_info("EEPROM size = %d", eeprom_dev.size);
	eeprom_dev.client = client;
	eeprom_dev.state = START;

	return 0;
}

static int i2c_master_eeprom_remove(struct i2c_client *client)
{
	cdev_del(&eeprom_dev.cdev);
	unregister_chrdev_region(eeprom_dev.dev, 1);
	return 0;
}

static const struct i2c_device_id i2c_eeprom_id[] = {
	{ "my_eeprom", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c_eeprom_id);

static struct i2c_driver i2c_master_eeprom_driver = {
	.driver = {
		.name = "eeprom_master",
	},
	.probe = i2c_master_eeprom_probe,
	.remove = i2c_master_eeprom_remove,
	.id_table = i2c_eeprom_id,
};
module_i2c_driver(i2c_master_eeprom_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C EEPROM master driver");
MODULE_AUTHOR("Sergey Samokhvalov/Ilya Vedmanov");
