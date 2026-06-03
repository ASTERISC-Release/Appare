#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <linux/skbuff.h>

#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>

#include <linux/seq_file.h>

MODULE_LICENSE("GPL");

#define NUMBER_OF_SYSCALLS 336
#define NUMBER_OF_SUBSYSTEMS 11
#define NUMBER_OF_FUNCTIONS 5000
#define NUMBER_OF_SENSITIVE_OBJECTS 248

extern int snprintf(char *buf, size_t size, const char *fmt, ...);

extern void deeplog_start_logging(void);
extern void deeplog_stop_logging(void);
extern int subsystem_call_count[22];
extern uint64_t sc_total_time;
extern unsigned long number_of_syscall_calls;
extern bool IS_OBJ_TRACKING_ENABLED;

extern int object_tracker[NUMBER_OF_SYSCALLS][NUMBER_OF_SENSITIVE_OBJECTS];

extern int object_allocations[NUMBER_OF_SYSCALLS][NUMBER_OF_SENSITIVE_OBJECTS];

extern int get_function_in_syscall(int syscall, int function);

int init_module(void)
{
    pr_info("starting obj profiling\n");
    IS_OBJ_TRACKING_ENABLED = true;
    return 0;
}

void dumpIntoFile(struct file *file, int data[NUMBER_OF_SYSCALLS][NUMBER_OF_SENSITIVE_OBJECTS]) {
    loff_t pos = 0;
    int bufLen;
    char buf[7];
    int total = 0;

    for (int i = 0; i < NUMBER_OF_SYSCALLS; i++) {
        int size = 0;
        pos += kernel_write(file, "{", 1, &pos);

        for (int k = 0; k < 7; k++) {
          buf[k] = '\0';
        }

        for (int j = 0; j < NUMBER_OF_SENSITIVE_OBJECTS; j++) {
          if (data[i][j] == 1) {
            size += 1;
            bufLen = snprintf(&buf[0], 7, "%u", j);
            pos += kernel_write(file, buf,  bufLen, &pos);
            pos += kernel_write(file, ", ", 2, &pos);
          }
        }
        pr_err("Objects in syscall %d: %d", i, size);
        total += size;
        pos += kernel_write(file, "}\n", 2, &pos);
    }
    pr_err("total functions profiled: %d", total);
}

static void print_allocations(void)
{
  int i, j;
  bool is_first;

  // debug
  for (i = 0; i < NUMBER_OF_SYSCALLS; i++) {
    for (j = 0; j < NUMBER_OF_SENSITIVE_OBJECTS; j++) {
      if (object_allocations[i][j] > 0) {
        pr_info("Syscall %d: Object %d: %d\n", i, j, object_allocations[i][j]);
      }
    }
  }

  for (i = 0; i < NUMBER_OF_SYSCALLS; i++) {
    is_first = true;
    for (j = 0; j < NUMBER_OF_SENSITIVE_OBJECTS; j++) {
      if (object_allocations[i][j] > 0) {
        if (is_first) {
          pr_info("Syscall %d: ", i);
          is_first = false;
        }
        pr_cont("(obj_id=%d: allocs=%d), ", j, object_allocations[i][j]);
      }
    }
  }
}

void cleanup_module(void)
{
    struct file *file;

    file = filp_open("/home/seclog/ObjectsInSyscallRaw", O_CREAT | O_RDWR, 777);
    dumpIntoFile(file, object_tracker);
    filp_close(file, 0);

  IS_OBJ_TRACKING_ENABLED = false;

  pr_info("printxxx\n");
  print_allocations();

    pr_info("Completed object Profiling!\n");
}

