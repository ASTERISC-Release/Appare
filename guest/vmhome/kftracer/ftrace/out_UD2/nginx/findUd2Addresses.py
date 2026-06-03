import sys

# profile_file = "redis_profile.csv"
# profile_file = "nginx_profile.csv"
profile_file = sys.argv[1]

page_size = 0x1000

class Page:
    def __init__(self, address, functionRange):
        self.base = ((address >> 12) << 12)
        self.functionRange = functionRange
        self.ud2Range = []
        self.numberOfFunctions = 0
        self.referenceFunction = ''

    def nextPage(self):
        return self.base + page_size

    def getLastAddress(self):
        return self.functionRange[-1]

    def calculateNumberOfFunctions(self):
        self.numberOfFunctions = int(len(self.functionRange) / 2)

    def reduceFunctionRange(self):
        i = 1
        while i < len(self.functionRange) - 1:
            if (self.functionRange[i] == self.functionRange[i+1]):
                self.functionRange = self.functionRange[:i] + self.functionRange[i+2:]
                i = 0
            i += 1

    def calculateUd2Range(self):
        if (len(self.functionRange) == 0):
            return
        # If we dont start at the beginning of the page ud2 it
        if (self.functionRange[0] != self.base):
            self.ud2Range.append(self.base)
            self.ud2Range.append(self.functionRange[0])

        for i in range(1, len(self.functionRange)):
            if (self.functionRange[i] == self.nextPage()):
                break
            '''
            if (i % 2 == 0):
                self.ud2Range.append(self.functionRange[i] - 2)
            else:
                self.ud2Range.append(self.functionRange[i])
            '''
            self.ud2Range.append(self.functionRange[i])

        # If we dont stop at the end of the page ud2 it
        if (self.functionRange[len(self.functionRange)-1] != self.nextPage()):
            # self.ud2Range.append(self.nextPage() - 2)
            self.ud2Range.append(self.nextPage())

def generateSyscallFunction(syscall):
    with open('syscalls.csv', 'r') as fp:
        line = fp.readline()
        while line != '':
            if (int(line.split(',')[0]) == syscall):
                return '__x64_' + line.split(',')[1][:-1]
            line = fp.readline()

        print("MISSING SYSCALL: " + str(syscall))
        exit(1)

# This function will make sure certain functions that SHOULD be there are there in case they are missed during profiling
def addDefaultFunctions(functions, syscall_index):
    defaultFunctions = [
        'do_syscall_x64', 
        'do_syscall_64', 
        '__find_get_block', 
        'entry_SYSCALL_64', 
        'asm_exc_page_fault', 
        'log_fn', 
        'log_sub', 
        'check_is_logging', 
        'check_is_tracked_proc', 
        'set_current_proc', 
        'deeplog_alloc', 
        'deeplog_free', 
        'addValueToArray', 
        'get_time', 
        '__memset', 
        '__memcpy', 
        '__fput',
        '____fput',
        'fpregs_assert_state_consistent',
        'memset_erms', 
        'memset_orig', 
        'memcpy_erms', 
        'memcpy_orig', 
        '__memmove', 
        'exit_sc', 
        'exit_to_user_mode_prepare', 
        'exit_to_user_mode_loop',
        'task_work_run',
        'strstr',
        'strlen',
        'strcmp',
        'strncmp',
        'jbd2__journal_start',
        '__iowrite64_copy',
        '__x64_sys_io_submit',
        'copy_user_enhanced_fast_string',
        'error_entry',
        'error_return',
        'syscall_exit_to_user_mode',
        'nf_hook_slow',
        '__crc32c_le_base',
        '__get_user_4',
        '__put_user_4',
        '__put_user_8',
        '__put_user_nocheck_4',
        '__audit_reusename',
        '__audit_getname',
        'audit_alloc_name',
        'kmalloc_trace',
        '__audit_inode',
        'audit_comparator',
        'audit_copy_inode',
        'audit_tree_lookup',
        'audit_tree_match',
        'security_inode_getsecid',
        'get_vfs_caps_from_disk',
        'selinux_inode_getsecid',
    ]
    defaultFunctions.append(generateSyscallFunction(syscall_index))
    for function in defaultFunctions:
        if function not in functions:
            functions.append(function)

    return functions


def generateSyscallProfile():
    ret = []
    file = open(profile_file, 'r')
    # Remove headers
    file.readline()

    line = file.readline()
    while line != '':
        pipeSeperatedFunctions = line.split(',')[1]
        functions = pipeSeperatedFunctions.split('|')
        if (len(functions) > 11):
            functions[len(functions)-1] = functions[len(functions)-1][:-1]
            functions = addDefaultFunctions(functions, int(line.split(',')[0]))
            ret.append(functions)
        else:
            ret.append([])
        line = file.readline()

    return ret

def getFunction(line):
    return line.split(' ')[2][:-1]

def getAddress(line):
    return int(line.split(' ')[0], 16)

def getType(line):
    return line.split(' ')[1][0]

syscall_profile = generateSyscallProfile()

kall_path = 'kallsyms'

text_end = None
static_end = None
# syms = []  # (addr,name)
with open(kall_path) as ks:
    for ln in ks:
        parts = ln.split()
        if len(parts) < 3:
            continue
        addr = int(parts[0], 16)
        typ = parts[1]
        name = parts[2]
        if name == '__static_call_text_end':
            static_end = addr
        if name == '__start_rodata':
            text_end = addr
            break
        # if typ in ('T', 't'):
        #     syms.append((addr, name))
if text_end is None or static_end is None:
    print('Error: required symbols not found', file=sys.stderr)
    sys.exit(1)

# Find every page with a function in it and record the ranges of those functions
syscall_pages = []
for syscall in syscall_profile:
    if (len(syscall) == 0):
        syscall_pages.append([])
        continue
    pages = []
    page = Page(0, [])
    start_address = 0
    end_address = 0

    kallsyms = open(kall_path, 'r')
    kallsyms_line = kallsyms.readline()
    while kallsyms_line != '':
        function = getFunction(kallsyms_line)
        address = getAddress(kallsyms_line)
        symType = getType(kallsyms_line)
        if (symType != 't' and symType != 'T'):
            kallsyms_line = kallsyms.readline()
            continue
        # if (address >= 0xffffffff82400000):
        if (address >= text_end):
            kallsyms_line = kallsyms.readline()
            continue;

        if (address >= page.nextPage()):
            # perfrom maths for this page
            page.calculateNumberOfFunctions()
            page.reduceFunctionRange()
            page.calculateUd2Range()
            pages.append(page)

            # quickly check for multi page functions (___bpf_prof_run)
            # Basically this is a sliding window, we look for the end of the 'next page' and see if it is still within
            # our current functions range

            if (page.numberOfFunctions > 0 and page.getLastAddress() == page.nextPage()):
                next_base = page.nextPage()
                next_tail = next_base + page_size
                # while (page.nextPage() + (page_size * (pages_added + 1)) < address):
                while (next_tail < address):
                    print(f'adding full page at address {hex(next_base)}')
                    full_page = Page(next_base, [])
                    full_page.functionRange.append(next_base)
                    full_page.functionRange.append(next_tail)
                    full_page.calculateNumberOfFunctions()
                    full_page.reduceFunctionRange()
                    full_page.calculateUd2Range()
                    pages.append(full_page)
                    next_base = next_tail
                    next_tail += page_size

            # take out the next page
            page = Page(address, [])
            page.referenceFunction = function

            last_page = pages[len(pages)-1]
            if (page.base != address and len(last_page.functionRange) > 1):
                last_address = last_page.functionRange[len(last_page.functionRange)-1]
                if (last_address == page.base):
                    page.functionRange.append(page.base)
                    page.functionRange.append(address)

        if (function in syscall):
            page.functionRange.append(address)
            start_address = address
            # THIS WILL STOP THE LAME DOUBLE DEFINITIONS
            while (address == start_address):
                kallsyms_line = kallsyms.readline()
                address = getAddress(kallsyms_line)
            page.functionRange.append(min(address, page.nextPage()))
        else:
            kallsyms_line = kallsyms.readline()

    kallsyms.close()
    pages.append(page)
    pages = pages[1:]
    syscall_pages.append(pages)


fd_code = open('kvm_data/whole_page_code.txt', 'w')
fd_func = open('kvm_data/whole_page_func.txt', 'w')

for syscall_page in syscall_pages:
    for i, page in enumerate(syscall_page):
        if page.numberOfFunctions == 0:
            fd_code.write(hex(page.base))
            fd_code.write(',')
            fd_func.write(page.referenceFunction)
            fd_func.write(',')
    fd_code.write("\n")
    fd_func.write("\n")

fd_code.close()
fd_func.close()

fd = open('kvm_data/ud2_sections.txt', 'w')

for syscall_page in syscall_pages:
    for i, page in enumerate(syscall_page):
        for j, ud2range in enumerate(page.ud2Range):
            fd.write(hex(ud2range))
            if (i + 1 != len(syscall_page) or j + 1 != len(page.ud2Range)):
                fd.write(",")
    fd.write("\n")

fd.close()
