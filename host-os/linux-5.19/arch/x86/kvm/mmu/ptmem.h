#ifndef __DEEPLOG_PTMEM_H__
#define __DEEPLOG_PTMEM_H__

#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include "common.h"
#include "metadata.h"

typedef struct vmem_range {
    unsigned long start;
    unsigned long end;
    unsigned long length;
} vmem_range_t;

#define MAX_RANGE   5

static vmem_range_t vmem_ranges[MAX_RANGE];
static int num_vmems = -1;

static int read_line(struct file *file, char *buf, size_t len, loff_t *foff)
{
    int bytes_read = 0;
    char c = 0;
    int res;

    while (bytes_read < len - 1) {
        res = kernel_read(file, &c, 1, foff);
        if (res < 1) /* end of file or error */
            break;
        buf[bytes_read++] = c;
        if (c == '\n')
            break;
    }
    buf[bytes_read] = '\0';
    return bytes_read;
}

static int read_vmem_ranges(const char *filename)
{
    struct file *file_meta;
    char buf[256] = {0};
    int i;
    loff_t fpos = 0;

    file_meta = filp_open(filename, O_RDONLY, 0);
    if (IS_ERR(file_meta)) {
        deeplog_log_error("Open vmem metadata file %s failed.", filename);
        return PTR_ERR(file_meta);
    }

    if (read_line(file_meta, buf, sizeof(buf), &fpos) > 0) {
        sscanf(buf, "%d", &num_vmems);
        deeplog_log_info("num_vmems: %d", num_vmems);
    }
    
    if (num_vmems < MAX_RANGE) {
        for(i = 0; i < num_vmems; i++) {
            if (read_line(file_meta, buf, sizeof(buf), &fpos) > 0) {
                sscanf(buf, "%lx, %lx", &vmem_ranges[i].start, &vmem_ranges[i].end);
                vmem_ranges[i].length = vmem_ranges[i].end - vmem_ranges[i].start;
                deeplog_log_info("vmem_ranges[%d]: start=0x%lx, end=0x%lx, length=0x%lx", 
                    i, vmem_ranges[i].start, vmem_ranges[i].end, vmem_ranges[i].length);
            }
        }
    }

    filp_close(file_meta, NULL);
    return 0;
}

static const char *GET_VMEM_PATH(int index) {
    if (index < 0 || index >= NR_LINUX_CODESECTIONS) {
        deeplog_log_error("Invalid index %d", index);
        return NULL;
    }
    return LINUX_VMEM_PATHS[index];
}

static unsigned long GET_VMEM_START(int index) {
    if (index < 0 || index >= NR_LINUX_CODESECTIONS) {
        deeplog_log_error("Invalid index %d", index);
        return 0;
    }
    if (num_vmems == -1) {
        if (read_vmem_ranges(VMLINUX_METADATA_PATH)) {
            deeplog_log_error("Read vmem ranges failed.");
            return 0;
        }
    }
    return vmem_ranges[index].start;
}

static unsigned long GET_VMEM_LEN(int index) {
    if (index < 0 || index >= NR_LINUX_CODESECTIONS) {
        deeplog_log_error("Invalid index %d", index);
        return 0;
    }
    if (num_vmems == -1) {
        if (read_vmem_ranges(VMLINUX_METADATA_PATH)) {
            deeplog_log_error("Read vmem ranges failed.");
            return 0;
        }
    }
    return vmem_ranges[index].length;
}
#endif