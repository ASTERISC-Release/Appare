import angr
from angr import Project
from cle.backends.elf.regions import ELFSection

from collections import defaultdict
from typing import Dict, List, Tuple, Optional, TYPE_CHECKING
from pathlib import Path
import os
import logging
import pickle

from angr.analyses.cfg import CFGFast
from angr.knowledge_plugins.cfg import CFGNode
from angr.codenode import BlockNode
from angr.knowledge_plugins.functions import Function

# first      pip install pygraphviz
#            pip install graphviz
# If ^ doesn't work, then try
#            sudo apt-get install -y graphviz-dev
import pygraphviz as pgv
import networkx as nx

VMLINUX = "vmlinux-5.13.3"
DEFAULT_VMLINUX_DIR = Path(__file__).parent.parent/ "cve-collection" / "vmlinuxs"
DEFAUL_VMLINUX_FILE = DEFAULT_VMLINUX_DIR / VMLINUX

DEFAULT_OUTPUT_DIR = Path(__file__).parent / "outputs"

DEFAULT_REBASE_PATH = DEFAULT_OUTPUT_DIR / "vmlinux_code.rebase"
DEFAULT_VMLINUX_META_PATH = DEFAULT_OUTPUT_DIR / "vmlinux_code.metadata"

DEFAULT_METADATA_FILE = Path(__file__).parent / "kvm" / "mmu" / "metadata.h"

# Rebase the vmlinux virtual layout for kASLR
# By default, we should disable kASLR.
KERN_REBASE = 0

# KO_REGIONS
# this is to ad-hocly add all dynamic regions
KO_REGIONS = [(0xffffffffa0000000, 0xffffffffa0a844c0)]

log = logging.getLogger(__name__)
log.setLevel(logging.DEBUG)

def output_dir() -> Path:
    if not DEFAULT_OUTPUT_DIR.exists():
            DEFAULT_OUTPUT_DIR.mkdir()
    return DEFAULT_OUTPUT_DIR


class Loader:
    """
    Top class of binary loader
    """
    def __init__(self, path: [Path, str], dbgsym_path: Optional[str]=None, load_external=False):
        optional_kwargs = {}

        self.path = path
        self.name = os.path.basename(path)

        if dbgsym_path is not None:
            optional_kwargs["debug_symbols"] = str(dbgsym_path)

        if load_external:
            self._load_external_libraries()
        self.project: Project = self._load_binary(load_external=load_external, optional_args=optional_kwargs)   # angr project for binary file

    def _output_dir(self) -> Path:
        if not DEFAULT_OUTPUT_DIR.exists():
            DEFAULT_OUTPUT_DIR.mkdir()
        return DEFAULT_OUTPUT_DIR

    def _load_external_libraries(self):
        raise NotImplementedError()

    def _load_binary(self, *optional_args, **optional_kwargs) -> Project:
        raise NotImplementedError()

    def _disasm_binary(self):
        """
        generate disasm file for binary
        """
        raise NotImplementedError()

    def load_int(self, addr: int, size: int) -> int:
        """
        Load the content of memory at the specified address
        """
        val = self.project.loader.memory.load(addr=addr, n=size)
        return int.from_bytes(val, "little")

    def load_byte(self, addr: int, size: int) -> bytes:
        """
        Load the content of memory as raw bytes at the specified address
        """
        return self.project.loader.memory.load(addr=addr, n=size)

    def load_string(self, addr: int, max_size: int = 1000) -> str:
        """
        Load the string at the specified address
        """
        idx, string = 0, ""
        while idx < max_size:
            last_chr = self.load_int(addr + idx, 1)
            if last_chr == 0:
                break
            string += chr(last_chr)
            idx += 1
        else:
            log.warning(f'String "{string}..." exceeded the max size {max_size}')
        return string

    def get_section_info(self, sec_name: str) -> Dict:
        raise NotImplementedError()

class ELFLoader(Loader):
    """
    Loader class for ELF format binaries
    """
    def __init__(self, path: [Path, str], dbgsym_path: Optional[str]=None, load_external=False):
        super().__init__(path, dbgsym_path, load_external)
        self._describe_sections()
        self._disasm_binary()

    def _load_external_libraries(self):
        """
        Get external libraries info
        """
        external_lib_path = self._output_dir().joinpath(self.name + "_libs.txt")
        os.system(f"ldd {self.path} > {external_lib_path}")

        # parse externals
        with open(external_lib_path, "r") as f:
            for line in f.readlines():
                if line.find("linux-vdso") >= 0 or line.find("ld") >= 0:
                    continue
                libname, libpaths = line.split("=>")
                libname = libname.strip()
                if libname.find("linux-vdso") >= 0 or libname.find("ld") >= 0:
                    continue
                libpath = libpaths.split()[0]
                os.system(f"cp {libpath} {os.path.join(str(self._output_dir()), libname)}")

    def _load_binary(self, base_addr: Optional[int]=None, optional_args: Optional[Dict]=None, auto_load_libs=False,
                     **kwargs):
        """
        generate angr project for binary
        """
        main_opts = {}
        if base_addr is not None:
            main_opts["base_addr"] = base_addr

        load_options = {"main_opts": main_opts}
        # set load options
        if optional_args is not None:
            optional_args: Dict
            for k, v in optional_args.items():
                if k == "debug_symbols":
                    main_opts["debug_symbols"] = v
                elif k == "main_opts" and isinstance(v, dict):
                    main_opts.update(v)
                elif k == "main_opts" and not isinstance(v, dict):
                    pass
                else:
                    load_options[k] = v

        binary = self.path
        log.info(f"Loading binary file {binary}...")

        # load lib option
        # load_external_libs = kwargs.pop("load_external", None)
        # if load_external_libs:
        #     lib_dir = self.arginfo.libs_output_path
        #     lib_files = []
        #     for file in Path(lib_dir).iterdir():
        #         if file.is_file():
        #             lib_files.append(file.stem)
        #             log.info(f"External library found at {str(file)}")
            # p = Project(binary, main_opts=main_opts, auto_load_libs=auto_load_libs, force_load_libs=lib_files,
            #             lib_opts=None, ld_path=[lib_dir], use_system_libs=False)
        # else:
        # load binary
        p = Project(binary, main_opts=main_opts, auto_load_libs=auto_load_libs, load_options=load_options)
        return p

    def _disasm_binary(self):
        """
        generate disasm file for binary
        """
        disasm_outpath = self._output_dir().joinpath(self.name + "_asm.txt")
        log.info(f"Generating binary disassembly file to {disasm_outpath}")
        os.system(f"objdump -d {self.path} > {disasm_outpath}")

    def get_section_info(self, sec_name: str) -> Dict:
        """
        Get ELF format binary section info
        :param sec_name: a valid section name like .interp .plt .plt.got .text ...
        :return:
        """
        try:
            section: ELFSection = self.project.loader.main_object.sections_map[sec_name]
        except KeyError:
            log.error(f"Section {sec_name} is not a valid section in binary {self.arginfo.binary_name}.")
            return dict()

        section_info = {
            "offset": section.offset,
            "base": self.project.loader.main_object.mapped_base,
            "size": section.memsize,
            "vaddr_start": section.vaddr,
            "vaddr_end": section.vaddr + section.memsize
        }
        return section_info
    
    def _describe_sections(self):
        """
        Describe sections in binary
        """
        for sec_name, sec in self.project.loader.main_object.sections_map.items():
            if sec.is_executable:
                log.info(f"Section <{sec_name}> at <{hex(sec.vaddr)}> with size <{hex(sec.memsize)}>, EXECUTABLE!")
            else:
                log.info(f"Section <{sec_name}> at <{hex(sec.vaddr)}> with size <{hex(sec.memsize)}>")
        
class CFGUtil:
    """
    Class for CFG reconstruction and static analysis
    """
    def __init__(self, project: angr.Project, auto_save=True, load_local=True):
        self.proj: angr.Project = project
        self._cfg = self._load_local(auto_save) if load_local else self._fcfg(auto_save)
    
    def _save(self, cfg):
        log.info(f"Saving CFG and knowledge_base...")
        cfgmodel_path = str(output_dir().joinpath(f"{VMLINUX}.cfgmodel"))
        kb_path = str(output_dir().joinpath(f"{VMLINUX}.kb"))
        with open(cfgmodel_path, "wb") as f:
            f.write(pickle.dumps(cfg, -1))
            log.info(f"CFG model saved at {cfgmodel_path}!")
        with open(kb_path, "wb") as f:
            f.write(pickle.dumps(cfg.kb, -1))
            log.info(f"knowledge_base saved at {kb_path}!")

    def _load_local(self, with_save=True):
        """
        Load CFG and knowledge_base from local file
        :return:
        """
        # load cfg model
        cfgmodel_path = str(output_dir().joinpath(f"{VMLINUX}.cfgmodel"))
        log.info("Loading CFG and KB from local file...")
        try:
            f = open(cfgmodel_path, "rb")
            ncfg = pickle.loads(f.read())
            f.close()
        except FileNotFoundError:
            log.warning(f"CFG model path {cfgmodel_path} not found. Recover from local failed.")
            log.info(f"Trying to re-reconstruct CFG instead.")
            ncfg = self._fcfg(with_save)
            # return ncfg

        # load kb
        kb_path = str(output_dir().joinpath(f"{VMLINUX}.kb"))
        try:
            f = open(kb_path, "rb")
            kb = pickle.loads(f.read())
            f.close()
        except FileNotFoundError:
            log.warning(f"Knowledge_base path {kb_path} not found. Recover from local failed.")
            log.info(f"Trying to re-reconstruct CFG instead.")
            ncfg = self._fcfg(with_save)
            # return ncfg

        # recover kb for project and CFG
        ncfg.kb = kb
        self.proj.kb = kb
        return ncfg

    def _fcfg(self, with_save=True) -> CFGFast:
        cfg = self.proj.analyses.CFGFast(normalize=True, data_references=True,)
        log.info(f"Successfully constructed CFG.")
        if with_save:
            self._save(cfg)
        return cfg


class V:
    """
    Virtualization.
    """
    def __init__(self, p: angr.Project):
        self.proj = p

    def draw_function_graph(self, function: Function, graph=None):
        """
        Draw the graph and save it to a PNG file.
        """
        import matplotlib.pyplot as pyplot  # pylint: disable=import-error
        from networkx.drawing.nx_agraph import graphviz_layout  # pylint: disable=import-error

        def node(n: BlockNode):
            blk = self.proj.factory.block(n.addr, n.size)
            addr = hex(n.addr)
            insn_s = ""
            for insn in blk.capstone.insns:
                insn_desp = "%#x:\t%s\t%s" % (insn.address, insn.mnemonic, insn.op_str)
                if insn.mnemonic == "call":
                    tar = function.get_call_target(n.addr)
                    if isinstance(tar, int):
                        try:
                            tar_name = self.proj.kb.functions[tar].name
                        except KeyError:
                            tar_name = "???"
                        insn_s = (insn_s + insn_desp + f"({tar_name})" + "\n")
                    else:
                        insn_s = (insn_s + insn_desp + '\n')
                else:
                    insn_s = (insn_s + insn_desp + '\n')
            sym = function.name
            return "<" + addr + " " + sym + ">" + "\n" + insn_s

        tmp_graph = nx.DiGraph()

        grh = graph if graph is not None else function.graph

        for from_block, to_block in grh.edges():
            node_a, node_b = node(from_block), node(to_block)
            tmp_graph.add_edge(node_a, node_b)
        if not len(tmp_graph.edges):
            for n in grh.nodes:
                tmp_graph.add_node(node(n))
        # pos = graphviz_layout(tmp_graph, prog='fdp')   # pylint: disable=no-member
        drop = os.path.join(output_dir(), "%s" % function.name)
        nx.drawing.nx_agraph.write_dot(tmp_graph, drop + '.dot')
        G = pgv.AGraph(drop + '.dot')
        G.draw(drop + '.pdf', prog='dot')
        os.system(f"rm {drop}.dot")


############################################
# metadata.h auto-generation
############################################
        
def format_string(nr_indent, indent, string):
    return nr_indent * indent + string + "\n"

def __metadata_head() -> str:
    head = ""
    indent = "    "
    nr_indent = 0
    head += format_string(nr_indent, indent, "#ifndef __DEEPLOG_VMLINUX_METADATA_H__")
    head += format_string(nr_indent, indent, "#define __DEEPLOG_VMLINUX_METADATA_H__")
    head += format_string(nr_indent, indent, "/*")
    head += format_string(nr_indent, indent, " * This file is auto-generated by vmlinux_code_loader.py: python3 host-os/vmlinux_code_loader.py")
    head += format_string(nr_indent, indent, " */")
    head += format_string(nr_indent, indent, "#include \"libpt.h\"")
    head += format_string(nr_indent, indent, "")
    return head

def __metadata_sections(loader: ELFLoader, sec_descs) -> str:
    macro = ""
    indent = "    "
    nr_indent = 0
    nr_secs = len(sec_descs)
    macro += format_string(nr_indent, indent, f"#define NR_LINUX_CODESECTIONS {nr_secs}")
    macro += "\n"
    macro += format_string(nr_indent, indent, f"#define VMLINUX_METADATA_PATH \"{DEFAULT_VMLINUX_META_PATH}\"")
    macro += "\n"

    vmem_paths = format_string(nr_indent, indent, f"static const char *LINUX_VMEM_PATHS[NR_LINUX_CODESECTIONS] = {{")
    vmem_starts = format_string(nr_indent, indent, f"static const unsigned long LINUX_VMEM_STARTS[{nr_secs}] = {{")
    vmem_lens = format_string(nr_indent, indent, f"static const unsigned long LINUX_VMEM_LENS[{nr_secs}] = {{")
    
    nr_indent += 1

    for sec in sec_descs:
        id = sec["id"]
        name = sec["name"]
        va_start = sec["va_start"]
        va_end = sec["va_end"]
        length = va_end - va_start

        fname = str(loader._output_dir().joinpath(f"vmlinux_code.vmem{id}"))
        vmem_paths += format_string(nr_indent, indent, f"\"{fname}\",")
        vmem_starts += format_string(nr_indent, indent, f"{hex(va_start)},")
        vmem_lens += format_string(nr_indent, indent, f"{hex(length)},")
    
    nr_indent -= 1

    vmem_paths += format_string(nr_indent, indent, "};")
    vmem_starts += format_string(nr_indent, indent, "};")
    vmem_lens += format_string(nr_indent, indent, "};")

    macro += (vmem_paths + "\n")
    # no need for now
    # macro += (vmem_starts + "\n")
    # macro += (vmem_lens + "\n")

    # pt_rebase
    # macro += format_string(nr_indent, indent, f"static unsigned long pt_rebase = 0xef;")
    # macro += "\n"

    # get_pt_rebase()
#     rebase_function = """
# static unsigned long get_pt_rebase(void)
# {
#     static struct file *rebase_file = NULL;
#     char buf[16] = {0};
#     int bytes_read, ret;
#     static loff_t foff = 0;
#     /* already fetched, return. */
#     if (pt_rebase != 0xef)
#         return pt_rebase;
#     /* get rebase from the file */
#     rebase_file = filp_open(LINUX_REBASE_PATH, O_RDWR, 0);
#     if(IS_ERR(rebase_file)) {
#         deeplog_log_error("open rebase file %s failed.\n",LINUX_REBASE_PATH);
#         return 0;
#     }
#     /* read rebase from the file */
#     bytes_read = kernel_read(rebase_file, buf, sizeof(buf), &foff);
#     if (bytes_read > 0) {
#         if ((ret = kstrtoul(buf, 16, &pt_rebase)) != 0) {
#             printk(KERN_ALERT "Error converting string buf=%s to unsigned long ret=%d, pt_rebase=0x%lx.\n",buf, ret, pt_rebase);
#             filp_close(rebase_file, NULL);
#             return -1;
#         }
#         filp_close(rebase_file, NULL);
#         return pt_rebase;
#     } else {
#         deeplog_log_error("read rebase file %s failed. bytes: %d\n", LINUX_REBASE_PATH, bytes_read);
#         filp_close(rebase_file, NULL);
#         return 0;
#     }
# }
# """

#     macro += format_string(nr_indent, indent, rebase_function)

    # functions
    # macro += format_string(nr_indent, indent, f"#define GET_VMEM_PATH(i) (LINUX_VMEM_PATHS[i])")
    # macro += format_string(nr_indent, indent, f"#define GET_VMEM_START(i) (LINUX_VMEM_STARTS[i] + get_pt_rebase())")
    # macro += format_string(nr_indent, indent, f"#define GET_VMEM_LEN(i) (LINUX_VMEM_LENS[i])")

    return macro


############################################
# PT trace parser
############################################
class Parser():
    def __init__(self, proj, cfg, 
                    fpath=str(DEFAULT_OUTPUT_DIR.joinpath("trace.csv"))):
        self.proj = proj
        self.cfg = cfg
        self.fpath = fpath
    
    # def parse(self):
        


if __name__ == "__main__":
    loader = ELFLoader(DEFAUL_VMLINUX_FILE)
    project = loader.project

    # we aggressively load all sections from .text //to .exit.text
    i = 0
    sec_descs = []
    # iterate all exectuable sections
    for name, sec in loader.project.loader.main_object.sections_map.items():
        if sec.is_executable:
            va_start = sec.vaddr
            va_end = sec.vaddr + sec.memsize

            va_start = (va_start >> 12) << 12 # lets page align for later mmap
            if i > 0:
                log.info(f"last end: {hex(sec_descs[i-1]['va_end'])}, this start: {hex(va_start)}")
            if not i or (i > 0 and sec_descs[i-1]["va_end"] < va_start):
                desc = {
                    "va_start": va_start, # lets page align for later mmap
                    "va_end": va_end,
                    "id": i,
                    "name": name,
                    "dummy": False,
                }
                sec_descs.append(desc)
                i += 1
            # should merge
            else:
                sec_descs[i-1]["va_end"] = va_end
    
    # add KO_Section
    if len(KO_REGIONS) > 0:
        for (start, end) in KO_REGIONS:
            va_start = start
            va_end = end
            va_start = (va_start >> 12) << 12
            desc = {
                "va_start": va_start,
                "va_end": va_end,
                "id": i,
                "name": "KO_REGION",
                "dummy": True,
            }
            sec_descs.append(desc)
            i += 1

    # import IPython; IPython.embed()
    
    va_metadata_path = loader._output_dir().joinpath("vmlinux_code.metadata")
    va_rebase_path = DEFAULT_REBASE_PATH
    f_meta = open(va_metadata_path, "w")
    with open(va_rebase_path, "w") as f:
        f.write(str(hex(KERN_REBASE)) + "\n")
    f_meta.write(str(len(sec_descs)) + "\n")
    
    for desc in sec_descs:
        va_start = desc["va_start"]
        va_end = desc["va_end"]
        id = desc["id"]
        name = desc["name"]
        length = va_end - va_start
        
        if KERN_REBASE == 0:
            log.info(f"Load executable section <{name}>: [<{hex(va_start)}>, <{hex(va_end)}>) to vmlinux_code.vmem{id}")
        elif KERN_REBASE != 0 and not desc["dummy"]:
            log.info(f"Load executable section <{name}>: [<{hex(va_start)}>, <{hex(va_end)}>) --rebase--> [<{hex(va_start + KERN_REBASE)}>, <{hex(va_end + KERN_REBASE)}>) to vmlinux_code.vmem{id}")
        else:
            log.info(f"Load dynamic dummy KO section: [<{hex(va_start)}>, <{hex(va_end)}>) to vmlinux_code.vmem{id}")
        # write bytes
        va_path = loader._output_dir().joinpath(f"vmlinux_code.vmem{id}")
        with open(va_path, "wb") as f:
            if not desc["dummy"]:
                bytes = loader.load_byte(va_start, length)
                if len(bytes) != length:
                    raise Exception("Failed to load all bytes from vmlinux!")
            else:
                bytes = b""
            f.write(bytes)
        
        # write metadata
        meta_start = (va_start + KERN_REBASE) if not desc["dummy"] else va_start
        meta_end = (va_end + KERN_REBASE) if not desc["dummy"] else va_end
        f_meta.write(f"{hex(meta_start)}, {hex(meta_end)}\n")
    
    f_meta.close()
    
    log.info(f"{VMLINUX} code virtual memory metadata written to {va_metadata_path}.")
    log.info(f"{VMLINUX} code kASLR rebase {hex(KERN_REBASE)} written to {va_rebase_path}.")

    # generate metadata.h for KVM
    f_meta = open(DEFAULT_METADATA_FILE, "w")
    metadata = ""
    metadata += __metadata_head()
    metadata += __metadata_sections(loader, sec_descs)
    metadata += "\n"
    metadata += "#endif\n"
    f_meta.write(metadata)
    f_meta.close()

    # create CFG 
    # CFGUtil(project)

    # virtualization
    # v = V(project)

    import IPython; IPython.embed()    
