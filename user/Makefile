
TOOLPREFIX := riscv64-unknown-elf-

CC := $(TOOLPREFIX)gcc
AS := $(TOOLPREFIX)gas
LD := $(TOOLPREFIX)ld
OBJCOPY := $(TOOLPREFIX)objcopy
OBJDUMP := $(TOOLPREFIX)objdump

CFLAGS := -Wall -Werror -O2 -fno-omit-frame-pointer -ggdb
CFLAGS += -ffreestanding -fno-common -nostdlib -nostdinc -mno-relax
CFLAGS += -march=rv64g -mabi=lp64 -mcmodel=medany -Ilib -static

ULIB 		:= $(subst lib/,,$(wildcard lib/*.c))
ULIB_OBJS 	:= $(addprefix build/, $(addsuffix .o, $(basename $(ULIB))))
USYS_OBJ	:= build/usys.o

UPROG_SRCS 	:= $(subst src/,,$(wildcard src/*.c))
UPROGS 		:= $(addprefix build/, $(basename $(UPROG_SRCS)))
UPROGS_STRIPPED := $(addprefix build/stripped/, $(basename $(UPROG_SRCS)))

all: $(UPROGS) $(UPROGS_STRIPPED)
	@echo
	@echo User programs:
	@ls build/stripped/

build/usys.o : lib/usys.pl ../os/syscall_ids.h
	@mkdir -p $(@D)
	perl lib/usys.pl > lib/usys.S
	$(CC) $(CFLAGS) -c lib/usys.S -o $@

$(ULIB_OBJS): build/%.o : lib/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(UPROGS): build/% : src/%.c $(ULIB_OBJS) $(USYS_OBJ)
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@.o
	$(LD) -T lib/user.ld -o $@ $@.o $(ULIB_OBJS) $(USYS_OBJ)
	$(OBJDUMP) -S $@ > $@.asm

$(UPROGS_STRIPPED): build/stripped/% : build/%
	@mkdir -p build/stripped
	$(OBJCOPY) --strip-unneeded $? $@


clean:
	@rm -rf lib/usys.S
	@rm -rf build/
