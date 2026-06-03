#include "driver.h"
#include <unistd.h>
#include <string>
#include <getopt.h>

bool block_mode = true;
std::string input_file = "";
std::string input_vmlinux_vmem = "";
std::string input_vmlinux_meta = "";
std::string output_file = "";

#define DEFAULT_PT_TRACE            "/home/chuqi/GitHub/deeplog-kern/host-os/outputs/pt.log"
#define DEFAULT_VMLINUX_VMEM        "/home/chuqi/GitHub/deeplog-kern/host-os/outputs/vmlinux_code.vmem"
#define DEFAULT_VMLINUX_META        "/home/chuqi/GitHub/deeplog-kern/host-os/outputs/vmlinux_code.metadata"
#define DEFAULT_OUTPUT_CSV          "/home/chuqi/GitHub/deeplog-kern/host-os/outputs/trace.csv"

void print_help(){
    std::cout << 
            "--input-ptrace[i] <fname>:           /path/to/input/pt.log (default is /home/chuqi/GitHub/deeplog-kern/host-os/outputs/pt.log)\n"
            "--input-vmlinux-vmem[v] <fname>:     /path/to/input/vmlinux_code.vmem (default is /home/chuqi/GitHub/deeplog-kern/host-os/outputs/vmlinux_code.vmem)\n"
            "--input-vmlinux-metadata[m] <fname>: /path/to/input/vmlinux_code.metadata (default is /home/chuqi/GitHub/deeplog-kern/host-os/outputs/vmlinux_code.metadata)\n"
            "--output-file[o] <fname>:            /path/to/output.csv (default is /home/chuqi/GitHub/deeplog-kern/host-os/outputs/trace.csv)\n"
            "--block-mode[b]:                     parse basic block\n"
            "--help[h]:                           show help\n";
    exit(1);
}

void proc_args(int argc, char ** argv){
    const char * const short_opts = "bi:o:h";
    const option long_opts[] = {
        {"block-mode", no_argument, nullptr, 'b'},
        {"input-ptrace", required_argument, nullptr, 'i'},
        {"input-vmlinux-vmem", required_argument, nullptr, 'v'},
        {"input-vmlinux-metadata", required_argument, nullptr, 'm'},
        {"output-file", required_argument, nullptr, 'o'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, no_argument, nullptr, 0}  
    };

    while (true)
    {
        const auto opt = getopt_long(argc, argv, short_opts, long_opts, nullptr);
        if (-1 == opt){
            break;
        }

        switch (opt)
        {
        case 'b':
            block_mode = true;
            break;
        case 'i':
            input_file = std::string(optarg);
            break;
        case 'o':
            output_file = std::string(optarg);
            break;
        case 'v':
            input_vmlinux_vmem = std::string(optarg);
            break;
        case 'm':
            input_vmlinux_meta = std::string(optarg);
            break;
        case 'h':
        case '?':
        default:
            print_help();
            break;
        }
    }

    /* set default paths */
    if (input_file.empty()){
        input_file = std::string(DEFAULT_PT_TRACE);
    }
    if (input_vmlinux_vmem.empty()){
        input_vmlinux_vmem = std::string(DEFAULT_VMLINUX_VMEM);
    }
    if (input_vmlinux_meta.empty()){
        input_vmlinux_meta = std::string(DEFAULT_VMLINUX_META);
    }
    if (output_file.empty()){
        output_file = std::string(DEFAULT_OUTPUT_CSV);
    }
}

void init_output_file(std::ofstream& f){
    if (!f.is_open()) {
		std::cerr << "Fail to open file:" << output_file << std::endl;
        return;
	}	
    f << "TYPE" << ", " 
      << "PID" << ", " 
      << "ADDR" << ", "
      << "JMP_TYPE" << ", "
    //   << "INSN_NUM" << ", "
      << "SYS_NUM" << ", "
      << "SYS_NAME" << ", "
      << "SYS_ADDR" << "\n";
}

void write_block(std::ofstream& f, uint64_t addr, uint16_t pid, enum pt_block_type type){
    if (!f.is_open()) {
		std::cerr << "Fail to open file:" << output_file << std::endl;
        return;
	}	
    std::string jmp_type;
    if (type == PT_TYPE_BLOCK){
        jmp_type = "boring";
    }
    else if (type == PT_TYPE_CALL){
        jmp_type = "call";
    }
    else if (type == PT_TYPE_RET){
        jmp_type = "return";
    }
    else{
        // do not print such syscall blocks
        jmp_type = "syscall";
        return;
    }
    f << "block" << ", " 
      << std::dec << pid << ", " 
      << std::hex << addr << ", " 
      << jmp_type << ", " 
    //   << std::dec << insn_num << ", "
      << "" << ", " 
      << "" << ", " 
      << "" << "\n";
}

void write_syscall(std::ofstream& f, uint64_t pid, uint64_t sys_addr, int sys_num, const char * sys_name, int index){
    if (!f.is_open()) {
		std::cerr << "Fail to open file:" << output_file << std::endl;
        return;
	}	
    std::string syscall = "syscall:" + std::to_string(index);
    f << syscall << ", "
      << std::dec << pid << ", "
      << "" << ", "
      << "" << ", "
    //   << "" << ", "
      << sys_num << ", "
      << sys_name << ", "
      << std::hex << sys_addr << "\n";
}

int main(int argc, char *argv[]) 
{
    proc_args(argc, argv);
    std::cout << "input PT trace file:" << input_file << std::endl 
              << "input vmlinux code vmem:" << input_vmlinux_vmem << std::endl
              << "input vmlinux metadata:" << input_vmlinux_meta << std::endl
              << "output trace file:" << output_file << std::endl;
	// read pt trace file
    ABORT(input_file.empty() || input_vmlinux_vmem.empty() || 
          input_vmlinux_meta.empty() || output_file.empty(),
           "./driver-csv --help");
    
    /* parse vmlinux code sections from the metadata file */
    unsigned int num_sections;
    unsigned long va_start, va_end;
    std::ifstream fmeta(input_vmlinux_meta);
    std::string line, vmem_path;
    if (!fmeta.is_open()) {
        std::cerr << "Fail to open file:" << input_vmlinux_meta << std::endl;
        return -1;
    }
    fmeta >> num_sections;
    fmeta.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    for (unsigned int i = 0; i < num_sections; i++){
        if (!std::getline(fmeta, line)) break;
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream iss(line);
        if (!(iss >> std::hex >> va_start >> va_end)) {
            std::cerr << "Parsing error on line " << i + 2 << std::endl;
            break;
        }

        vmem_path = input_vmlinux_vmem + std::to_string(i);
        std::cout << "Parsing " << std::hex 
                << va_start << " -- " << va_end
                << " to " << vmem_path << std::endl;
        int ret = mmap_vmlinux_xpage(vmem_path.c_str(), va_start, va_end);
        if (ret == -1) {
            std::cerr << "Fail to parse and mmap vmlinux" << std::endl;
            return -1;
        }
    }
    // return 0;
    
	FILE* trace = fopen(input_file.c_str(), "r");
	ABORT(!trace, "open %s failed", argv[1]);

	std::chrono::steady_clock sc;
	double t_disa = 0;

	// pt trace integrity check
	trace_integrity_check(trace);

	// read pt traces
	struct pt_logitem_header header;

	pt_blocks *blocks = new pt_blocks {
		.ptr = (pt_block_addr *) malloc(PT_RING_BUFFER_SIZE * sizeof(pt_block_addr)),
		.size = PT_RING_BUFFER_SIZE,
		.pos = 0,
	};

    // create output file
    std::ofstream fout(output_file, std::ios::out);
    init_output_file(fout);

    unsigned int total_n_inst = 0;
    unsigned int total_n_packet = 0;

	int syscall_index = 0;
	while (fread(&header, 1, sizeof(header), trace)) {
		// print kind 
        std::cout << "header size: " << header.size << std::endl;
        std::cout << "header kind: " << header.kind << std::endl;

        /* finish or invalid header ? */
        if (header.kind >= 10) {
            break;
        }
        // deal with xpage
		if (header.size == 0) {
			continue;
		}
		else {
			// undo the seek due to header read
			fseek(trace, -sizeof(header), SEEK_CUR);
		}

		// allocate memory to store the whole item
		void *item = malloc(header.size);
		ABORT(!item, "malloc for item failed");

		// read in
		size_t len = fread(item, 1, header.size, trace);
		ABORT(len != header.size, "unexpected trace ending. len 0x%lx, header.size 0x%lx.\n", 
                len, header.size);

		// parse pt trace
		auto ts_disa = sc.now();
		pt_parse(header.kind, item, blocks, &total_n_inst, &total_n_packet);
		auto te_disa = sc.now();
		t_disa += static_cast<std::chrono::duration<double>>(te_disa - ts_disa).count();

		for (uint64_t i = 0; i < blocks->pos; i++) {
        	uint64_t addr = blocks->ptr[i].addr;
        	enum pt_block_type type =  blocks->ptr[i].type;
			uint16_t pid = blocks->ptr[i].pid;
            // unsigned int insn_num = blocks->ptr[i].n_inst;

			// if (addr > 0x70000000000) {
			// 	if (type != PT_TYPE_SYSCALL) {
			// 		continue;
			// 	}
			// }
            
            if (block_mode) {
                write_block(fout, addr, pid, type);
            }

			if (type == PT_TYPE_SYSCALL) {
				int sid = blocks->ptr[i].sid;
				
				// continue if pt parser fail to infer the syscall id
				if (sid == -1) {
					// printf("%d syscall: %lx\n", syscall_index, addr);;
                    write_syscall(fout, pid, addr, sid, "unknown", syscall_index);
				}
				else {
					// printf("%d syscall: %lx %d %s\n", syscall_index, addr, sid, syscallid2name(sid));
                    write_syscall(fout, pid, addr, sid, syscallid2name(sid), syscall_index);
                }

				syscall_index += 1;
			}
		}
		free(item);	
	}

	std::cout << "Disassembly Runtime " << std::fixed << std::setprecision(3)
    << t_disa << " seconds" << std::endl;

	free(blocks->ptr);
    delete(blocks);
	fclose(trace);

	return 0;
}
