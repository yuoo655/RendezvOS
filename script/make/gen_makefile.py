import sys
import os
from pathlib import Path

# Exclude uefi directory from regular build process
makefile_string="include $(SCRIPT_MAKE_DIR)/build.mk\n" \
            +   "-include ./Makefile.env\n" \
            +   "modules= $(shell find ./* -maxdepth 0 -type d | grep -v './uefi')\n\n"    \
            +   "all: init $(modules) ${OBJECTS}\n" \
            +   "\t@for mod in $(modules); do $(MAKE) -C $$mod all; done\n\n" \
            +   "-include ${BUILD}/*.d\n" \
            +   "${BUILD}/%.o: ./%.c $(modules)\n" \
            +   "\t@echo \"CC	\"$@\n" \
            +   "\t@$(CC) $(CFLAGS) -o $@ -c $< -MD -MF ${BUILD}/$*.d -MP\n"

# Original makefile string for non-arch directories
makefile_string_normal="include $(SCRIPT_MAKE_DIR)/build.mk\n" \
            +   "-include ./Makefile.env\n" \
            +   "modules= $(shell find ./* -maxdepth 0 -type d)\n\n"    \
            +   "all: init $(modules) ${OBJECTS}\n" \
            +   "\t@for mod in $(modules); do $(MAKE) -C $$mod all; done\n\n" \
            +   "-include ${BUILD}/*.d\n" \
            +   "${BUILD}/%.o: ./%.c $(modules)\n" \
            +   "\t@echo \"CC	\"$@\n" \
            +   "\t@$(CC) $(CFLAGS) -o $@ -c $< -MD -MF ${BUILD}/$*.d -MP\n"

def gen_makefile(target_dir,exclude_dir_list, is_arch_dir=False):
    target_dir = Path(target_dir)
    for item in target_dir.iterdir():
        if item.is_dir():
            path_string = f"{item}"
            if path_string in exclude_dir_list:
                continue
            makefile_file_path = os.path.join(path_string,"Makefile")
            makefile_file=open(makefile_file_path,"w")
            # Use special makefile string for arch directories to exclude uefi
            if is_arch_dir and item.name == "x86_64":
                makefile_file.write(makefile_string)
            else:
                makefile_file.write(makefile_string_normal)
            makefile_file.close()
            # Recursively traverse subdirectories  
            gen_makefile(item,exclude_dir_list, is_arch_dir)
        elif item.is_file():
            path_string = f"{item}"
            dir_path = os.path.dirname(path_string)
            file_name = os.path.basename(path_string)
            if file_name.endswith('.S'):
                real_file_name = file_name.split('.')[0]
                append_string = "\n${BUILD}/"+real_file_name+".o: ./"+real_file_name+".S $(modules)\n\t@echo \"CC	${BUILD}/boot.o\"\n\t@$(CC) $(CFLAGS) -o $@ -c $< -MD -MF $*.d -MP"
                makefile_file_path = os.path.join(dir_path,"Makefile")
                makefile_file=open(makefile_file_path,"a")
                makefile_file.write(append_string)
                makefile_file.close()

if __name__ =='__main__':
    print("GEN\tMakefile")
    arch_dir=sys.argv[1]
    gen_makefile(arch_dir, [], is_arch_dir=True)

    kernel_dir=sys.argv[2]
    gen_makefile(kernel_dir, [])

    modules_dir=sys.argv[3]
    exclude_user_dir = os.path.join(modules_dir,"user")
    gen_makefile(modules_dir,[exclude_user_dir])