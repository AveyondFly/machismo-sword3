/*
 * Trampoline patcher for statically-linked functions in Mach-O binaries.
 *
 * Overwrites function entry points with 16-byte arm64 trampolines
 * that jump to native Linux .so implementations.
 *
 * Trampoline (16 bytes):
 *   ldr x16, #8       // load target address from literal pool
 *   br  x16            // branch to native function
 *   .quad <target>     // 8-byte native function address
 *
 * x16 (IP0) is the intra-procedure-call scratch register on arm64,
 * safe to clobber at function entry.
 */

#include "trampoline.h"
#include "macho_defs.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <inttypes.h>

/* arm64 trampoline island approach:
 *
 * Problem: inline 16-byte trampolines overflow into adjacent functions
 * when the patched function is smaller than 16 bytes (73 bgfx functions).
 *
 * Solution: write only a 4-byte B (branch) instruction at the function
 * entry, targeting a 16-byte island in a nearby executable pool.
 * The island contains the full indirect branch: ldr x16, #8; br x16; .quad addr
 *
 * B instruction range: ±128MB — pool must be within 128MB of __TEXT.
 */

#define TRAMPOLINE_LDR_X16  0x58000050  /* ldr x16, #8 */
#define TRAMPOLINE_BR_X16   0xd61f0200  /* br x16 */
#define ISLAND_SIZE         16

struct trampoline_island {
	uint32_t ldr_x16;      /* ldr x16, #8 */
	uint32_t br_x16;       /* br x16 */
	uint64_t target_addr;  /* native function address */
};

/* Island pool — executable memory near __TEXT */
#define ISLAND_POOL_SIZE (1024 * 1024)  /* 1MB, enough for ~65K islands */
static uint8_t* island_pool = NULL;
static size_t island_pool_size = 0;
static size_t island_pool_used = 0;

void trampoline_set_pool(void* base, size_t size)
{
	island_pool = (uint8_t*)base;
	island_pool_size = size;
	island_pool_used = 0;
	fprintf(stderr, "trampoline: island pool at %p (%zu KB)\n",
	        base, size / 1024);
}

static int init_island_pool(uintptr_t text_base, size_t text_size)
{
	(void)text_base; (void)text_size;
	/* Pool must be set up by caller via trampoline_set_pool() before
	 * trampoline_apply(). The loader preallocates it adjacent to __TEXT. */
	if (island_pool) return 0;
	fprintf(stderr, "trampoline: no island pool — call trampoline_set_pool() first\n");
	return -1;
}

/* Allocate an island and return its address, or 0 on failure */
static uintptr_t alloc_island(uintptr_t native_addr)
{
	if (!island_pool || island_pool_used + ISLAND_SIZE > island_pool_size)
		return 0;

	struct trampoline_island* island =
		(struct trampoline_island*)(island_pool + island_pool_used);
	island->ldr_x16 = TRAMPOLINE_LDR_X16;
	island->br_x16 = TRAMPOLINE_BR_X16;
	island->target_addr = native_addr;

	uintptr_t addr = (uintptr_t)island;
	island_pool_used += ISLAND_SIZE;
	return addr;
}

/* Shared return-0 stub island (allocated once, reused for all stubs) */
static uintptr_t stub_island_addr = 0;

static uintptr_t get_stub_island(void)
{
	if (stub_island_addr) return stub_island_addr;
	if (!island_pool || island_pool_used + ISLAND_SIZE > island_pool_size)
		return 0;

	uint32_t* code = (uint32_t*)(island_pool + island_pool_used);
	code[0] = 0x52800000;  /* mov w0, #0 */
	code[1] = 0xD65F03C0;  /* ret */
	code[2] = 0xD503201F;  /* nop */
	code[3] = 0xD503201F;  /* nop */

	stub_island_addr = (uintptr_t)code;
	island_pool_used += ISLAND_SIZE;
	return stub_island_addr;
}

/*
 * Try macOS ↔ Linux C++ mangling variants for the same function.
 * macOS uint64_t = unsigned long long (mangling 'y'),
 * Linux uint64_t = unsigned long (mangling 'm'). Both 8 bytes on aarch64.
 * Same for int64_t: macOS 'x' (long long), Linux 'l' (long).
 *
 * Can't do global replacement because 'y' appears inside name components
 * (e.g. "Memory" → "6MemoryE"). Instead, try all 2^n subsets of y-positions,
 * replacing each subset with 'm'. With n typically 1-3, this is fast.
 */
static void* try_mangling_variants(void* lib, const char* name)
{
	extern int machismo_verbose;
	size_t len = strlen(name);

	/* Collect positions of 'y' characters */
	int y_pos[16];
	int ny = 0;
	for (size_t i = 0; i < len && ny < 16; i++) {
		if (name[i] == 'y') y_pos[ny++] = i;
	}

	if (ny == 0) return NULL;

	char* buf = malloc(len + 1);
	if (!buf) return NULL;

	/* Try all non-empty subsets of y→m replacements */
	for (int mask = 1; mask < (1 << ny); mask++) {
		memcpy(buf, name, len + 1);
		for (int j = 0; j < ny; j++) {
			if (mask & (1 << j))
				buf[y_pos[j]] = 'm';
		}
		void* addr = dlsym(lib, buf);
		if (addr) {
			if (machismo_verbose)
				fprintf(stderr, "trampoline: mangling fallback: %s -> %s\n", name, buf);
			free(buf);
			return addr;
		}
	}

	/* Reverse direction: try subsets of m→y */
	int m_pos[16];
	int nm = 0;
	for (size_t i = 0; i < len && nm < 16; i++) {
		if (name[i] == 'm') m_pos[nm++] = i;
	}

	for (int mask = 1; mask < (1 << nm); mask++) {
		memcpy(buf, name, len + 1);
		for (int j = 0; j < nm; j++) {
			if (mask & (1 << j))
				buf[m_pos[j]] = 'y';
		}
		void* addr = dlsym(lib, buf);
		if (addr) {
			if (machismo_verbose)
				fprintf(stderr, "trampoline: mangling fallback: %s -> %s\n", name, buf);
			free(buf);
			return addr;
		}
	}

	free(buf);
	return NULL;
}

/*
 * Abort handler for un-trampolined Mach-O function calls.
 * Any call to a bgfx/SDL2 function still pointing to Mach-O code is a bug.
 */
static void __attribute__((noreturn)) trampoline_abort(const char* symbol_name)
{
	fprintf(stderr, "\nFATAL: un-trampolined Mach-O call to %s\n", symbol_name);
	abort();
}

/* Shared abort caller island: ldr x16, #8; br x16; .quad &trampoline_abort */
static uintptr_t abort_caller_addr = 0;

static uintptr_t get_abort_caller(void)
{
	if (abort_caller_addr) return abort_caller_addr;
	if (!island_pool || island_pool_used + ISLAND_SIZE > island_pool_size)
		return 0;

	struct trampoline_island* island =
		(struct trampoline_island*)(island_pool + island_pool_used);
	island->ldr_x16 = TRAMPOLINE_LDR_X16;
	island->br_x16 = TRAMPOLINE_BR_X16;
	island->target_addr = (uintptr_t)&trampoline_abort;

	abort_caller_addr = (uintptr_t)island;
	island_pool_used += ISLAND_SIZE;
	return abort_caller_addr;
}

/*
 * Allocate an abort trap island for a specific symbol.
 * Layout (16 bytes): ldr x0, #8; b <abort_caller>; .quad name_ptr
 * When called, x0 = symbol name pointer, then branches to shared
 * abort caller which calls trampoline_abort(x0).
 */
static uintptr_t make_abort_island(const char* symbol_name)
{
	uintptr_t caller = get_abort_caller();
	if (!caller) return 0;

	if (!island_pool || island_pool_used + ISLAND_SIZE > island_pool_size)
		return 0;

	uint32_t* code = (uint32_t*)(island_pool + island_pool_used);
	uintptr_t island_addr = (uintptr_t)code;

	/* ldr x0, #+8 — load name pointer from 8 bytes ahead */
	code[0] = 0x58000040;

	/* b <abort_caller> */
	int64_t offset = (int64_t)caller - (int64_t)(island_addr + 4);
	if (offset < -128*1024*1024 || offset >= 128*1024*1024 || (offset & 3)) {
		fprintf(stderr, "trampoline: abort caller too far from island\n");
		return 0;
	}
	uint32_t imm26 = (uint32_t)((offset >> 2) & 0x03FFFFFF);
	code[1] = 0x14000000 | imm26;

	/* .quad symbol_name — pointer to the name string (in Mach-O strtab, always valid) */
	*(uint64_t*)&code[2] = (uint64_t)symbol_name;

	island_pool_used += ISLAND_SIZE;
	return island_addr;
}

/* Convert a file offset to a memory address using segment mappings */
static void* fileoff_to_mem(void* mh, uintptr_t slide, uint32_t fileoff)
{
	struct mach_header_64* header = (struct mach_header_64*)mh;
	uint8_t* cmd_ptr = (uint8_t*)(header + 1);

	for (uint32_t i = 0; i < header->ncmds; i++) {
		struct load_command* lc = (struct load_command*)cmd_ptr;
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64* seg = (struct segment_command_64*)lc;
			if (fileoff >= seg->fileoff && fileoff < seg->fileoff + seg->filesize) {
				return (void*)(seg->vmaddr + slide + (fileoff - seg->fileoff));
			}
		}
		cmd_ptr += lc->cmdsize;
	}
	return NULL;
}

/* Make a range of __TEXT pages writable for patching.
 * Called once before writing all trampolines, then restored after. */
static long sys_page_size;
static uintptr_t text_patch_base;
static size_t text_patch_size;

static int make_text_writable(void* mh, uintptr_t slide)
{
	struct mach_header_64* header = (struct mach_header_64*)mh;
	uint8_t* cmd_ptr = (uint8_t*)(header + 1);

	sys_page_size = sysconf(_SC_PAGESIZE);
	if (sys_page_size <= 0) sys_page_size = 4096;

	/* Find __TEXT segment and make it writable */
	for (uint32_t i = 0; i < header->ncmds; i++) {
		struct load_command* lc = (struct load_command*)cmd_ptr;
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64* seg = (struct segment_command_64*)lc;
			if (strcmp(seg->segname, "__TEXT") == 0) {
				text_patch_base = (seg->vmaddr + slide) & ~(uintptr_t)(sys_page_size - 1);
				uintptr_t end = seg->vmaddr + slide + seg->vmsize;
				end = (end + sys_page_size - 1) & ~(uintptr_t)(sys_page_size - 1);
				text_patch_size = end - text_patch_base;

				if (mprotect((void*)text_patch_base, text_patch_size,
				             PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
					fprintf(stderr, "trampoline: mprotect __TEXT writable failed: %s\n",
							strerror(errno));
					return -1;
				}
				/* Allocate island pool near __TEXT */
				if (init_island_pool(text_patch_base, text_patch_size) < 0) {
					fprintf(stderr, "trampoline: failed to allocate island pool\n");
					return -1;
				}
				return 0;
			}
		}
		cmd_ptr += lc->cmdsize;
	}
	fprintf(stderr, "trampoline: __TEXT segment not found\n");
	return -1;
}

static void restore_text_protection(void)
{
	if (text_patch_base && text_patch_size) {
		mprotect((void*)text_patch_base, text_patch_size, PROT_READ | PROT_EXEC);
	}
}

/* Write a trampoline at the given address.
 * Writes a 4-byte B instruction to an island containing the full indirect branch. */
static int write_trampoline(uintptr_t func_addr, uintptr_t native_addr)
{
	uintptr_t island_addr = alloc_island(native_addr);
	if (!island_addr) {
		fprintf(stderr, "trampoline: island pool exhausted\n");
		return -1;
	}

	/* Compute B offset: (target - pc) / 4, must fit in 26-bit signed field */
	int64_t offset = (int64_t)island_addr - (int64_t)func_addr;
	if (offset < -128*1024*1024 || offset >= 128*1024*1024 || (offset & 3) != 0) {
		fprintf(stderr, "trampoline: island at %p too far from %p (offset %ld)\n",
		        (void*)island_addr, (void*)func_addr, (long)offset);
		return -1;
	}

	/* Encode: B imm26 — opcode 0x14000000 | (imm26 & 0x03FFFFFF) */
	uint32_t imm26 = (uint32_t)((offset >> 2) & 0x03FFFFFF);
	uint32_t b_insn = 0x14000000 | imm26;

	/* Write single 4-byte instruction at function entry */
	*(uint32_t*)func_addr = b_insn;

	/* Flush instruction cache for the patched site */
	__builtin___clear_cache((char*)func_addr, (char*)(func_addr + 4));

	return 0;
}

/* LC_FUNCTION_STARTS is a linkedit_data_command containing a sequence of
 * ULEB128 deltas from the beginning of __TEXT. */
#define LC_FUNCTION_STARTS_LOCAL 0x26

struct linkedit_data_command_local {
	uint32_t cmd;
	uint32_t cmdsize;
	uint32_t dataoff;
	uint32_t datasize;
};

struct address_hook {
	uintptr_t vmaddr;
	char* lib_path;
	char* symbol;
	uint8_t* expected;
	size_t expected_len;
	void* lib_handle;
	void* target;
	uintptr_t runtime_addr;
	uint32_t original_insn;
};

static void free_address_hooks(struct address_hook* hooks, size_t count,
                               int close_handles)
{
	if (!hooks) return;
	for (size_t i = 0; i < count; i++) {
		if (close_handles && hooks[i].lib_handle)
			dlclose(hooks[i].lib_handle);
		free(hooks[i].lib_path);
		free(hooks[i].symbol);
		free(hooks[i].expected);
	}
	free(hooks);
}

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	c = (char)tolower((unsigned char)c);
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

static int parse_expected_bytes(const char* text, uint8_t** bytes,
                                size_t* byte_count)
{
	size_t length = strlen(text);
	if (length < 8 || (length & 1) != 0)
		return -1;

	size_t count = length / 2;
	uint8_t* parsed = malloc(count);
	if (!parsed) return -1;

	for (size_t i = 0; i < count; i++) {
		int high = hex_nibble(text[i * 2]);
		int low = hex_nibble(text[i * 2 + 1]);
		if (high < 0 || low < 0) {
			free(parsed);
			return -1;
		}
		parsed[i] = (uint8_t)((high << 4) | low);
	}

	*bytes = parsed;
	*byte_count = count;
	return 0;
}

static int parse_address_hook_line(char* line, unsigned int line_number,
                                   struct address_hook* hook)
{
	char* comment = strchr(line, '#');
	if (comment) *comment = '\0';

	char* save = NULL;
	char* address = strtok_r(line, " \t\r\n", &save);
	if (!address) return 0;
	char* lib_path = strtok_r(NULL, " \t\r\n", &save);
	char* symbol = strtok_r(NULL, " \t\r\n", &save);
	char* expected = strtok_r(NULL, " \t\r\n", &save);
	char* extra = strtok_r(NULL, " \t\r\n", &save);
	if (!lib_path || !symbol || !expected || extra) {
		fprintf(stderr, "trampoline: address hooks line %u: expected 4 fields\n",
		        line_number);
		return -1;
	}

	char* end = NULL;
	errno = 0;
	unsigned long long parsed_address = strtoull(address, &end, 16);
	if (errno != 0 || end == address || *end != '\0' ||
	    parsed_address > (unsigned long long)UINTPTR_MAX) {
		fprintf(stderr, "trampoline: address hooks line %u: invalid vmaddr '%s'\n",
		        line_number, address);
		return -1;
	}

	hook->vmaddr = (uintptr_t)parsed_address;
	hook->lib_path = strdup(lib_path);
	hook->symbol = strdup(symbol);
	if (!hook->lib_path || !hook->symbol ||
	    parse_expected_bytes(expected, &hook->expected,
	                         &hook->expected_len) < 0) {
		fprintf(stderr,
		        "trampoline: address hooks line %u: invalid expected bytes "
		        "(use an even hex string of at least 4 bytes)\n",
		        line_number);
		return -1;
	}
	return 1;
}

static int load_address_hooks(const char* path, struct address_hook** result,
                              size_t* result_count)
{
	FILE* file = fopen(path, "r");
	if (!file) {
		fprintf(stderr, "trampoline: cannot open address hooks %s: %s\n",
		        path, strerror(errno));
		return -1;
	}

	struct address_hook* hooks = NULL;
	size_t count = 0;
	size_t capacity = 0;
	char* line = NULL;
	size_t line_capacity = 0;
	unsigned int line_number = 0;
	int status = 0;

	while (getline(&line, &line_capacity, file) >= 0) {
		line_number++;
		if (count == capacity) {
			size_t new_capacity = capacity ? capacity * 2 : 8;
			struct address_hook* grown =
				realloc(hooks, new_capacity * sizeof(*hooks));
			if (!grown) {
				status = -1;
				break;
			}
			hooks = grown;
			memset(hooks + capacity, 0,
			       (new_capacity - capacity) * sizeof(*hooks));
			capacity = new_capacity;
		}

		int parsed = parse_address_hook_line(line, line_number, &hooks[count]);
		if (parsed < 0) {
			status = -1;
			break;
		}
		if (parsed > 0) count++;
	}

	if (ferror(file)) {
		fprintf(stderr, "trampoline: error reading address hooks %s\n", path);
		status = -1;
	}
	free(line);
	fclose(file);

	if (status < 0) {
		/* Include the partially initialized current slot in cleanup. */
		free_address_hooks(hooks, count + (count < capacity), 1);
		return -1;
	}
	if (count == 0) {
		fprintf(stderr, "trampoline: address hooks %s contains no hooks\n", path);
		free(hooks);
		return -1;
	}

	*result = hooks;
	*result_count = count;
	return 0;
}

static void* file_range_to_mem(void* mh, uintptr_t slide,
                               uint32_t fileoff, uint32_t size)
{
	struct mach_header_64* header = (struct mach_header_64*)mh;
	uint8_t* cmd_ptr = (uint8_t*)(header + 1);
	uint64_t range_end = (uint64_t)fileoff + size;

	for (uint32_t i = 0; i < header->ncmds; i++) {
		struct load_command* lc = (struct load_command*)cmd_ptr;
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64* seg = (struct segment_command_64*)lc;
			uint64_t seg_end = seg->fileoff + seg->filesize;
			if (fileoff >= seg->fileoff && range_end <= seg_end)
				return (void*)(seg->vmaddr + slide +
				              ((uint64_t)fileoff - seg->fileoff));
		}
		cmd_ptr += lc->cmdsize;
	}
	return NULL;
}

static int decode_function_starts(void* mh, uintptr_t slide,
                                  uintptr_t** starts_out,
                                  size_t* count_out)
{
	struct mach_header_64* header = (struct mach_header_64*)mh;
	uint8_t* cmd_ptr = (uint8_t*)(header + 1);
	struct linkedit_data_command_local* starts_cmd = NULL;
	uintptr_t text_vmaddr = 0;

	for (uint32_t i = 0; i < header->ncmds; i++) {
		struct load_command* lc = (struct load_command*)cmd_ptr;
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64* seg = (struct segment_command_64*)lc;
			if (strncmp(seg->segname, "__TEXT", sizeof(seg->segname)) == 0)
				text_vmaddr = (uintptr_t)seg->vmaddr;
		} else if (lc->cmd == LC_FUNCTION_STARTS_LOCAL) {
			starts_cmd = (struct linkedit_data_command_local*)lc;
		}
		cmd_ptr += lc->cmdsize;
	}

	if (!text_vmaddr || !starts_cmd || starts_cmd->datasize == 0) {
		fprintf(stderr,
		        "trampoline: address hooks require __TEXT and LC_FUNCTION_STARTS\n");
		return -1;
	}

	const uint8_t* data = file_range_to_mem(mh, slide, starts_cmd->dataoff,
	                                        starts_cmd->datasize);
	if (!data) {
		fprintf(stderr, "trampoline: cannot locate LC_FUNCTION_STARTS data\n");
		return -1;
	}

	uintptr_t* starts = NULL;
	size_t count = 0;
	size_t capacity = 0;
	uintptr_t current = text_vmaddr;
	size_t offset = 0;

	while (offset < starts_cmd->datasize) {
		uint64_t delta = 0;
		unsigned int shift = 0;
		uint8_t byte;
		do {
			if (offset >= starts_cmd->datasize || shift >= 64) {
				fprintf(stderr, "trampoline: malformed LC_FUNCTION_STARTS\n");
				free(starts);
				return -1;
			}
			byte = data[offset++];
			delta |= (uint64_t)(byte & 0x7f) << shift;
			shift += 7;
		} while (byte & 0x80);

		if (delta == 0) break;
		if (delta > UINTPTR_MAX - current) {
			fprintf(stderr, "trampoline: LC_FUNCTION_STARTS address overflow\n");
			free(starts);
			return -1;
		}
		current += (uintptr_t)delta;

		if (count == capacity) {
			size_t new_capacity = capacity ? capacity * 2 : 64;
			uintptr_t* grown = realloc(starts, new_capacity * sizeof(*starts));
			if (!grown) {
				free(starts);
				return -1;
			}
			starts = grown;
			capacity = new_capacity;
		}
		starts[count++] = current;
	}

	if (count == 0) {
		fprintf(stderr, "trampoline: LC_FUNCTION_STARTS contains no functions\n");
		free(starts);
		return -1;
	}

	*starts_out = starts;
	*count_out = count;
	return 0;
}

static int executable_function_end(void* mh, uintptr_t vmaddr,
                                   const uintptr_t* starts, size_t start_count,
                                   uintptr_t* function_end)
{
	struct mach_header_64* header = (struct mach_header_64*)mh;
	uint8_t* cmd_ptr = (uint8_t*)(header + 1);
	uintptr_t section_end = 0;

	for (uint32_t i = 0; i < header->ncmds; i++) {
		struct load_command* lc = (struct load_command*)cmd_ptr;
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64* seg = (struct segment_command_64*)lc;
			if (strncmp(seg->segname, "__TEXT", sizeof(seg->segname)) == 0 &&
			    (seg->initprot & VM_PROT_EXECUTE)) {
				struct section_64* sections = (struct section_64*)(seg + 1);
				for (uint32_t j = 0; j < seg->nsects; j++) {
					struct section_64* section = &sections[j];
					uint64_t end = section->addr + section->size;
					int instructions =
						(section->flags & (S_ATTR_PURE_INSTRUCTIONS |
						                   S_ATTR_SOME_INSTRUCTIONS)) != 0;
					if (instructions && vmaddr >= section->addr &&
					    vmaddr < end && end <= UINTPTR_MAX) {
						section_end = (uintptr_t)end;
						break;
					}
				}
			}
		}
		cmd_ptr += lc->cmdsize;
	}

	if (!section_end) return -1;

	for (size_t i = 0; i < start_count; i++) {
		if (starts[i] != vmaddr) continue;
		uintptr_t end = section_end;
		if (i + 1 < start_count && starts[i + 1] > vmaddr &&
		    starts[i + 1] < section_end)
			end = starts[i + 1];
		*function_end = end;
		return 0;
	}
	return -1;
}

int trampoline_validate_function(void* mh, uintptr_t slide, uintptr_t vmaddr,
                                 const char* expected_hex)
{
	uint8_t* expected = NULL;
	size_t expected_len = 0;
	uintptr_t* starts = NULL;
	size_t start_count = 0;
	uintptr_t function_end = 0;
	int result = -1;

	if (!mh || !expected_hex ||
	    ((struct mach_header_64*)mh)->magic != MH_MAGIC_64) {
		fprintf(stderr, "trampoline: invalid entry validation request\n");
		return -1;
	}
	if (parse_expected_bytes(expected_hex, &expected, &expected_len) < 0) {
		fprintf(stderr,
		        "trampoline: entry expected bytes must be even hex (at least 4 bytes)\n");
		return -1;
	}
	if (decode_function_starts(mh, slide, &starts, &start_count) < 0)
		goto done;
	if (executable_function_end(mh, vmaddr, starts, start_count,
	                            &function_end) < 0) {
		fprintf(stderr,
		        "trampoline: entry 0x%" PRIxPTR
		        " is not an executable LC_FUNCTION_STARTS address\n",
		        vmaddr);
		goto done;
	}
	if (expected_len > function_end - vmaddr ||
	    vmaddr > UINTPTR_MAX - slide) {
		fprintf(stderr, "trampoline: entry validation range is invalid\n");
		goto done;
	}
	if (memcmp((void*)(vmaddr + slide), expected, expected_len) != 0) {
		fprintf(stderr,
		        "trampoline: entry bytes mismatch at 0x%" PRIxPTR "\n",
		        vmaddr);
		goto done;
	}
	fprintf(stderr,
	        "trampoline: validated entry 0x%" PRIxPTR " (%zu signature bytes)\n",
	        vmaddr, expected_len);
	result = 0;

done:
	free(starts);
	free(expected);
	return result;
}

int trampoline_patch_addresses(void* mh, uintptr_t slide,
                               const char* config_path)
{
	if (!mh || !config_path ||
	    ((struct mach_header_64*)mh)->magic != MH_MAGIC_64) {
		fprintf(stderr, "trampoline: invalid Mach-O or address hooks path\n");
		return -1;
	}

	struct address_hook* hooks = NULL;
	size_t hook_count = 0;
	uintptr_t* function_starts = NULL;
	size_t function_count = 0;
	int result = -1;

	if (load_address_hooks(config_path, &hooks, &hook_count) < 0)
		return -1;
	if (decode_function_starts(mh, slide, &function_starts, &function_count) < 0)
		goto done;

	for (size_t i = 0; i < hook_count; i++) {
		struct address_hook* hook = &hooks[i];
		uintptr_t function_end = 0;

		for (size_t j = 0; j < i; j++) {
			if (hooks[j].vmaddr == hook->vmaddr) {
				fprintf(stderr,
				        "trampoline: duplicate address hook at 0x%" PRIxPTR "\n",
				        hook->vmaddr);
				goto done;
			}
		}

		if (executable_function_end(mh, hook->vmaddr, function_starts,
		                            function_count, &function_end) < 0) {
			fprintf(stderr,
			        "trampoline: address 0x%" PRIxPTR
			        " is not a function start in executable __TEXT\n",
			        hook->vmaddr);
			goto done;
		}
		if (hook->expected_len > function_end - hook->vmaddr) {
			fprintf(stderr,
			        "trampoline: expected bytes at 0x%" PRIxPTR
			        " cross the function boundary\n", hook->vmaddr);
			goto done;
		}
		if (hook->vmaddr > UINTPTR_MAX - slide) {
			fprintf(stderr, "trampoline: runtime address overflow\n");
			goto done;
		}
		hook->runtime_addr = hook->vmaddr + slide;
		if (memcmp((void*)hook->runtime_addr, hook->expected,
		           hook->expected_len) != 0) {
			fprintf(stderr,
			        "trampoline: expected bytes mismatch at 0x%" PRIxPTR "\n",
			        hook->vmaddr);
			goto done;
		}
		memcpy(&hook->original_insn, (void*)hook->runtime_addr,
		       sizeof(hook->original_insn));

		hook->lib_handle = dlopen(hook->lib_path, RTLD_NOW | RTLD_GLOBAL);
		if (!hook->lib_handle) {
			fprintf(stderr, "trampoline: cannot load %s: %s\n",
			        hook->lib_path, dlerror());
			goto done;
		}
		dlerror();
		hook->target = dlsym(hook->lib_handle, hook->symbol);
		const char* symbol_error = dlerror();
		if (symbol_error || !hook->target) {
			fprintf(stderr, "trampoline: symbol %s not found in %s: %s\n",
			        hook->symbol, hook->lib_path,
			        symbol_error ? symbol_error : "null symbol address");
			goto done;
		}
	}

	if (!island_pool || island_pool_used > island_pool_size ||
	    hook_count > (island_pool_size - island_pool_used) / ISLAND_SIZE) {
		fprintf(stderr, "trampoline: address hooks exceed island pool capacity\n");
		goto done;
	}
	for (size_t i = 0; i < hook_count; i++) {
		uintptr_t island_addr = (uintptr_t)island_pool + island_pool_used +
		                        i * ISLAND_SIZE;
		int64_t offset = (int64_t)island_addr -
		                 (int64_t)hooks[i].runtime_addr;
		if (offset < -128 * 1024 * 1024 || offset >= 128 * 1024 * 1024 ||
		    (offset & 3) != 0) {
			fprintf(stderr,
			        "trampoline: island for 0x%" PRIxPTR " is out of branch range\n",
			        hooks[i].vmaddr);
			goto done;
		}
	}

	if (make_text_writable(mh, slide) < 0) {
		restore_text_protection();
		goto done;
	}

	size_t pool_start = island_pool_used;
	size_t patched = 0;
	for (; patched < hook_count; patched++) {
		if (write_trampoline(hooks[patched].runtime_addr,
		                     (uintptr_t)hooks[patched].target) < 0)
			break;
	}
	if (patched != hook_count) {
		for (size_t i = 0; i < patched; i++) {
			memcpy((void*)hooks[i].runtime_addr, &hooks[i].original_insn,
			       sizeof(hooks[i].original_insn));
			__builtin___clear_cache((char*)hooks[i].runtime_addr,
			                        (char*)(hooks[i].runtime_addr + 4));
		}
		island_pool_used = pool_start;
		restore_text_protection();
		fprintf(stderr,
		        "trampoline: address hook write failed; all writes rolled back\n");
		goto done;
	}

	restore_text_protection();
	fprintf(stderr, "trampoline: address hooks: %zu patched from %s\n",
	        hook_count, config_path);
	result = (int)hook_count;

done:
	free(function_starts);
	/* Successful patches need their libraries to remain loaded. */
	free_address_hooks(hooks, hook_count, result < 0);
	return result;
}

/* Check if a symbol name matches any of the given prefixes */
static int matches_prefix(const char* name, const char** prefixes, int num_prefixes)
{
	for (int i = 0; i < num_prefixes; i++) {
		if (strncmp(name, prefixes[i], strlen(prefixes[i])) == 0)
			return 1;
	}
	return 0;
}

/* Check if a symbol has an override, return target or NULL */
static void* find_override(const char* name, trampoline_override_t* overrides, int num_overrides)
{
	for (int i = 0; i < num_overrides; i++) {
		if (strcmp(name, overrides[i].symbol_name) == 0)
			return overrides[i].target;
	}
	return NULL;
}

int trampoline_patch_lib(void* mh, uintptr_t slide,
                         const char* lib_path,
                         const char** prefixes, int num_prefixes,
                         trampoline_override_t* overrides, int num_overrides)
{
	struct mach_header_64* header = (struct mach_header_64*)mh;
	uint8_t* cmd_ptr = (uint8_t*)(header + 1);

	/* Find LC_SYMTAB */
	struct symtab_command* symtab = NULL;
	for (uint32_t i = 0; i < header->ncmds; i++) {
		struct load_command* lc = (struct load_command*)cmd_ptr;
		if (lc->cmd == LC_SYMTAB) {
			symtab = (struct symtab_command*)lc;
			break;
		}
		cmd_ptr += lc->cmdsize;
	}

	if (!symtab) {
		fprintf(stderr, "trampoline: no LC_SYMTAB found\n");
		return -1;
	}

	/* STUB mode: redirect all matching symbols to a return-0 stub */
	int stub_mode = (lib_path && strcmp(lib_path, "STUB") == 0);

	/* Load the native library (skip in stub mode) */
	void* native_lib = NULL;
	if (!stub_mode) {
		native_lib = dlopen(lib_path, RTLD_LAZY | RTLD_GLOBAL);
		if (!native_lib) {
			fprintf(stderr, "trampoline: cannot load %s: %s\n", lib_path, dlerror());
			return -1;
		}
	}

	/* Get nlist array and string table from mapped memory */
	struct nlist_64* nlist_arr = (struct nlist_64*)fileoff_to_mem(mh, slide, symtab->symoff);
	char* strtab = (char*)fileoff_to_mem(mh, slide, symtab->stroff);

	if (!nlist_arr || !strtab) {
		fprintf(stderr, "trampoline: cannot locate symbol table in memory\n");
		return -1;
	}

	/* Make __TEXT writable for all patches at once */
	if (make_text_writable(mh, slide) < 0) {
		return -1;
	}

	int patched = 0;
	int mangling_fixed = 0;
	int trapped = 0;

	for (uint32_t i = 0; i < symtab->nsyms; i++) {
		struct nlist_64* sym = &nlist_arr[i];

		/* Skip non-global, non-defined symbols */
		if ((sym->n_type & N_TYPE) != N_SECT) continue;
		if (!(sym->n_type & N_EXT)) continue;
		if (sym->n_type & N_STAB) continue;

		/* Get symbol name */
		if (sym->n_strx >= symtab->strsize) continue;
		const char* name = strtab + sym->n_strx;

		/* Check prefix match */
		if (!matches_prefix(name, prefixes, num_prefixes)) continue;

		/* Compute address in mapped memory */
		uintptr_t func_addr = sym->n_value + slide;

		/* Only trampoline symbols in __TEXT — skip data symbols */
		if (func_addr < text_patch_base ||
		    func_addr >= text_patch_base + text_patch_size) {
			continue;
		}

		/* Check for override first */
		void* target = find_override(name, overrides, num_overrides);

		if (!target && stub_mode) {
			/* In stub mode, all symbols go to return-0 stub */
			target = (void*)get_stub_island();
		}

		if (!target && native_lib) {
			/* Strip leading underscore for dlsym lookup */
			const char* lookup_name = name;
			if (lookup_name[0] == '_') lookup_name++;

			target = dlsym(native_lib, lookup_name);

			/* Fallback: try macOS↔Linux C++ mangling variants */
			if (!target && strncmp(lookup_name, "_ZN", 3) == 0) {
				target = try_mangling_variants(native_lib, lookup_name);
				if (target) mangling_fixed++;
			}
		}

		if (!target && !stub_mode) {
			/* No native match — trap this function so we know immediately
			 * if Mach-O code tries to call it. */
			uintptr_t abort_island = make_abort_island(name);
			if (abort_island) {
				extern int machismo_verbose;
				if (machismo_verbose)
					fprintf(stderr, "trampoline: TRAPPED (no native match): %s\n", name);
				if (write_trampoline(func_addr, abort_island) == 0) {
					trapped++;
				}
			}
			continue;
		}

		if (!target) {
			/* STUB mode with no target — shouldn't happen, but skip */
			continue;
		}

		/* Write trampoline */
		if (write_trampoline(func_addr, (uintptr_t)target) == 0) {
			patched++;
		} else {
			fprintf(stderr, "trampoline: failed to patch %s at %p\n", name, (void*)func_addr);
		}
	}

	/* Restore __TEXT to read+execute */
	restore_text_protection();

	fprintf(stderr, "trampoline: %d patched, %d mangling-fixed, %d trapped from %s\n",
			patched, mangling_fixed, trapped, lib_path);

	/* Don't dlclose — the game needs the native library to stay loaded */
	return patched;
}

int trampoline_patch_overrides(void* mh, uintptr_t slide, void* override_handle,
                               int match_local)
{
	struct mach_header_64* header = (struct mach_header_64*)mh;
	uint8_t* cmd_ptr = (uint8_t*)(header + 1);

	/* Find LC_SYMTAB */
	struct symtab_command* symtab = NULL;
	for (uint32_t i = 0; i < header->ncmds; i++) {
		struct load_command* lc = (struct load_command*)cmd_ptr;
		if (lc->cmd == LC_SYMTAB) {
			symtab = (struct symtab_command*)lc;
			break;
		}
		cmd_ptr += lc->cmdsize;
	}

	if (!symtab) {
		fprintf(stderr, "trampoline: override: no LC_SYMTAB found\n");
		return -1;
	}

	struct nlist_64* nlist_arr = (struct nlist_64*)fileoff_to_mem(mh, slide, symtab->symoff);
	char* strtab = (char*)fileoff_to_mem(mh, slide, symtab->stroff);

	if (!nlist_arr || !strtab) {
		fprintf(stderr, "trampoline: override: cannot locate symbol table\n");
		return -1;
	}

	/* Make __TEXT writable (idempotent if already writable from a prior call) */
	if (make_text_writable(mh, slide) < 0)
		return -1;

	int patched = 0;

	for (uint32_t i = 0; i < symtab->nsyms; i++) {
		struct nlist_64* sym = &nlist_arr[i];

		if ((sym->n_type & N_TYPE) != N_SECT) continue;
		if (!match_local && !(sym->n_type & N_EXT)) continue;
		if (sym->n_type & N_STAB) continue;
		if (sym->n_strx >= symtab->strsize) continue;

		const char* name = strtab + sym->n_strx;
		uintptr_t func_addr = sym->n_value + slide;

		/* Only patch symbols in __TEXT */
		if (func_addr < text_patch_base ||
		    func_addr >= text_patch_base + text_patch_size)
			continue;

		/* Strip one leading underscore (Mach-O convention) for dlsym */
		const char* lookup_name = name;
		if (lookup_name[0] == '_') lookup_name++;

		void* target = dlsym(override_handle, lookup_name);
		if (!target) continue;

		if (write_trampoline(func_addr, (uintptr_t)target) == 0) {
			fprintf(stderr, "trampoline: override: %s -> %p\n", name, target);
			patched++;
		}
	}

	restore_text_protection();

	fprintf(stderr, "trampoline: override: %d functions replaced%s\n",
	        patched, match_local ? " (incl. local symbols)" : "");
	return patched;
}

/* Legacy env-var based API */
int trampoline_patch(void* mh, uintptr_t slide)
{
	const char* lib_path = getenv("MACHISMO_TRAMPOLINE_LIB");
	const char* prefix = getenv("MACHISMO_TRAMPOLINE_PREFIX");

	if (!lib_path) lib_path = "libSDL2-2.0.so.0";
	if (!prefix) prefix = "_SDL_";

	const char* prefixes[] = { prefix };
	return trampoline_patch_lib(mh, slide, lib_path, prefixes, 1, NULL, 0);
}

/*
 * Stale __DATA access detection.
 *
 * After all trampolines are applied, find library data symbols in __DATA
 * and guard pages that contain ONLY library symbols. Any access to a guarded
 * page means un-trampolined or inlined code is touching Mach-O library state
 * instead of native .so state — always a bug.
 */

/* Track guarded page ranges for SIGSEGV handler */
#define MAX_GUARDED_PAGES 4096
static struct {
	uintptr_t base;
	size_t size;
} guarded_pages[MAX_GUARDED_PAGES];
static int num_guarded_pages = 0;

/* Classifier for the unified crash handler (crash_handler.c, the sole
 * fatal-signal owner). Reports whether a faulting address lies in a __DATA page
 * we guarded against stale Mach-O access — un-trampolined or inlined code
 * touching Mach-O library state instead of the native .so. The crash handler
 * prints the stale-data diagnostic (async-signal-safe) and continues into the
 * unified cross-world backtrace, so this no longer installs a SIGSEGV handler
 * or aborts on its own. Safe to call from a signal context: pure array reads. */
int trampoline_is_guarded_fault(uintptr_t addr)
{
	for (int i = 0; i < num_guarded_pages; i++) {
		if (addr >= guarded_pages[i].base &&
		    addr < guarded_pages[i].base + guarded_pages[i].size)
			return 1;
	}
	return 0;
}

void trampoline_guard_stale_data(void* mh, uintptr_t slide,
                                  const char** prefixes, int num_prefixes)
{
	struct mach_header_64* header = (struct mach_header_64*)mh;
	uint8_t* cmd_ptr = (uint8_t*)(header + 1);

	long page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0) page_size = 4096;

	/* Find __DATA segment boundaries */
	uintptr_t data_base = 0, data_end = 0;
	for (uint32_t i = 0; i < header->ncmds; i++) {
		struct load_command* lc = (struct load_command*)cmd_ptr;
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64* seg = (struct segment_command_64*)lc;
			if (strcmp(seg->segname, "__DATA") == 0) {
				data_base = seg->vmaddr + slide;
				data_end = data_base + seg->vmsize;
				break;
			}
		}
		cmd_ptr += lc->cmdsize;
	}
	if (!data_base) return;

	size_t num_pages = (data_end - data_base + page_size - 1) / page_size;
	if (num_pages == 0) return;

	/* Bitmap: has_library[i] = page i has a library symbol
	 *         has_other[i]   = page i has a non-library symbol */
	uint8_t* has_library = calloc(num_pages, 1);
	uint8_t* has_other = calloc(num_pages, 1);
	if (!has_library || !has_other) {
		free(has_library);
		free(has_other);
		return;
	}

	/* Find LC_SYMTAB */
	struct symtab_command* symtab = NULL;
	cmd_ptr = (uint8_t*)(header + 1);
	for (uint32_t i = 0; i < header->ncmds; i++) {
		struct load_command* lc = (struct load_command*)cmd_ptr;
		if (lc->cmd == LC_SYMTAB) {
			symtab = (struct symtab_command*)lc;
			break;
		}
		cmd_ptr += lc->cmdsize;
	}
	if (!symtab) { free(has_library); free(has_other); return; }

	struct nlist_64* nlist_arr = (struct nlist_64*)fileoff_to_mem(mh, slide, symtab->symoff);
	char* strtab = (char*)fileoff_to_mem(mh, slide, symtab->stroff);
	if (!nlist_arr || !strtab) { free(has_library); free(has_other); return; }

	int lib_data_syms = 0;

	/* Walk all symbols, categorize pages */
	for (uint32_t i = 0; i < symtab->nsyms; i++) {
		struct nlist_64* sym = &nlist_arr[i];
		if ((sym->n_type & N_TYPE) != N_SECT) continue;
		if (sym->n_type & N_STAB) continue;
		if (sym->n_strx >= symtab->strsize) continue;

		uintptr_t addr = sym->n_value + slide;
		if (addr < data_base || addr >= data_end) continue;

		size_t page_idx = (addr - data_base) / page_size;
		if (page_idx >= num_pages) continue;

		const char* name = strtab + sym->n_strx;
		if (matches_prefix(name, prefixes, num_prefixes)) {
			has_library[page_idx] = 1;
			lib_data_syms++;
			extern int machismo_verbose;
			if (machismo_verbose)
				fprintf(stderr, "trampoline: stale data symbol: %s at %p\n",
				        name, (void*)addr);
		} else {
			has_other[page_idx] = 1;
		}
	}

	if (lib_data_syms == 0) {
		free(has_library);
		free(has_other);
		return;
	}

	/* Guard pages that have ONLY library symbols */
	int guarded = 0;
	int mixed = 0;
	for (size_t i = 0; i < num_pages; i++) {
		if (!has_library[i]) continue;
		if (has_other[i]) {
			mixed++;
			continue;
		}
		if (num_guarded_pages >= MAX_GUARDED_PAGES) break;

		uintptr_t page_addr = data_base + i * page_size;
		if (mprotect((void*)page_addr, page_size, PROT_NONE) == 0) {
			guarded_pages[num_guarded_pages].base = page_addr;
			guarded_pages[num_guarded_pages].size = page_size;
			num_guarded_pages++;
			guarded++;
		}
	}

	free(has_library);
	free(has_other);

	/* No SIGSEGV handler installed here: crash_handler.c is the single
	 * fatal-signal owner and queries trampoline_is_guarded_fault() to surface
	 * the stale-data diagnostic above as part of its unified backtrace. The
	 * pages stay PROT_NONE so a stale access still faults. */

	fprintf(stderr, "trampoline: __DATA guard: %d library data symbols, "
	        "%d pages guarded, %d mixed pages (cannot guard)\n",
	        lib_data_syms, guarded, mixed);
}
