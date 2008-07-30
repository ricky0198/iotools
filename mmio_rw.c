/*
 Copyright 2008 Google Inc.

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*
 * Quick access to physical memory, requires /dev/mem
 * Tim Hockin <thockin@google.com>
 */
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include "commands.h"

struct mmap_info {
	int fd;
	volatile void *mem;
	int off;
	size_t pgsize;
	uint64_t addr;
};

/* open /dev/mem and mmap the address specified in mmap_addr. return 1 on
 * success, 0 on failure. */
static int
open_mapping(struct mmap_info *mmap_addr, int flags)
{
	int prot;

	/* align addr */
	mmap_addr->pgsize = getpagesize();
	mmap_addr->off = mmap_addr->addr & (mmap_addr->pgsize - 1);
	mmap_addr->addr &= ~ ((uint64_t)mmap_addr->pgsize - 1);

	mmap_addr->fd = open("/dev/mem", flags|O_SYNC);
	if (mmap_addr->fd < 0) {
		fprintf(stderr, "open(/dev/mem): %s\n", strerror(errno));
		return -1;
	}

	prot = 0;
	if ((flags&O_ACCMODE) == O_RDWR) {
		prot = PROT_READ | PROT_WRITE;
	} else if ((flags & O_ACCMODE) == O_RDONLY) {
		prot = PROT_READ;
	} else if  ((flags & O_ACCMODE) == O_WRONLY) {
		prot = PROT_WRITE;
	}

	mmap_addr->mem = mmap(NULL, mmap_addr->pgsize*2, prot, MAP_SHARED,
	           mmap_addr->fd, mmap_addr->addr);

	if (!mmap_addr->mem || mmap_addr->mem == MAP_FAILED) {
		fprintf(stderr, "mmap(/dev/mem): %s\n", strerror(errno));
		close(mmap_addr->fd);
		return -1;
	}

	return 0;
}

static void
close_mapping(struct mmap_info *mmap_addr)
{
	munmap((void *)mmap_addr->mem, mmap_addr->pgsize*2);
	close(mmap_addr->fd);
}

static int
mmio_read_x(int argc, const char *argv[], const struct cmd_info *info)
{
	int ret;
	data_store data;
	struct mmap_info mmap_addr;

	mmap_addr.addr = strtoull(argv[1], NULL, 0);

	if (open_mapping(&mmap_addr, O_RDONLY) < 0) {
		return -1;
	}

	ret = 0;

	#define DO_READ(mem_, off_, size_)\
		data.u ##size_ = *(typeof(data.u ##size_) *)(mem_ + off_); \
		fprintf(stdout, "0x%0*x\n", (int)sizeof(data.u ##size_)*2, \
		        data.u ##size_)

	switch (get_command_size(info)) {
	case SIZE8:
		DO_READ(mmap_addr.mem, mmap_addr.off, 8);
		break;
	case SIZE16:
		DO_READ(mmap_addr.mem, mmap_addr.off, 16);
		break;
	case SIZE32:
		DO_READ(mmap_addr.mem, mmap_addr.off, 32);
		break;
	default:
		fprintf(stderr, "invalid mmio_read parameter\n");
		ret = -1;
	}

	close_mapping(&mmap_addr);

	return ret;
}

static int
mmio_write_x(int argc, const char *argv[], const struct cmd_info *info)
{
	int ret;
	unsigned long ldata;
	data_store data;
	struct mmap_info mmap_addr;

	mmap_addr.addr = strtoull(argv[1], NULL, 0);
	ldata = strtoul(argv[2], NULL, 0);

	if (open_mapping(&mmap_addr, O_RDWR) < 0) {
		return -1;
	}

	ret = 0;

	#define DO_WRITE(mem_, off_, size_)\
		data.u ##size_ = (typeof(data.u ##size_))ldata;\
		*(typeof(data.u ##size_) *)(mem_+off_) = data.u ##size_

	switch (get_command_size(info)) {
	case SIZE8:
		DO_WRITE(mmap_addr.mem, mmap_addr.off, 8);
		break;
	case SIZE16:
		DO_WRITE(mmap_addr.mem, mmap_addr.off, 16);
		break;
	case SIZE32:
		DO_WRITE(mmap_addr.mem, mmap_addr.off, 32);
		break;
	default:
		fprintf(stderr, "invalid mmio_write parameter\n");
		ret = -1;
	}

	close_mapping(&mmap_addr);

	return ret;
}

MAKE_PREREQ_PARAMS_FIXED_ARGS(rd_params, 2, "<addr>", 0);
MAKE_PREREQ_PARAMS_FIXED_ARGS(wr_params, 3, "<addr> <value>", 0);

#define MAKE_MMIO_READ_CMD(size_) \
	MAKE_CMD_WITH_PARAMS(mmio_read ##size_, &mmio_read_x, &size ##size_, \
	                     &rd_params)
#define MAKE_MMIO_WRITE_CMD(size_) \
	MAKE_CMD_WITH_PARAMS(mmio_write ##size_, &mmio_write_x, &size ##size_, \
	                     &wr_params)
#define MAKE_MMIO_RW_CMD_PAIR(size_) \
	MAKE_MMIO_READ_CMD(size_), \
	MAKE_MMIO_WRITE_CMD(size_)

static const struct cmd_info mmio_cmds[] = {
	MAKE_MMIO_RW_CMD_PAIR(8),
	MAKE_MMIO_RW_CMD_PAIR(16),
	MAKE_MMIO_RW_CMD_PAIR(32),
};

MAKE_CMD_GROUP(MMIO, "commands to access memory mapped address spaces",
               mmio_cmds);
REGISTER_CMD_GROUP(MMIO);