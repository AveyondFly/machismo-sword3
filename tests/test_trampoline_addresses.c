#include "config.h"
#include "macho_defs.h"
#include "trampoline.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int machismo_verbose = 0;

#define TEST_VMADDR 0x100000000ULL
#define TEST_MAP_SIZE 0x4000
#define TEST_FUNCTION_OFFSET 0x1000
#define TEST_POOL_OFFSET 0x2000

struct linkedit_data_command_fixture {
	uint32_t cmd;
	uint32_t cmdsize;
	uint32_t dataoff;
	uint32_t datasize;
};

struct fixture {
	uint8_t* mapping;
	void* mh;
	uintptr_t slide;
	uint8_t original[8];
};

static struct fixture make_fixture(void)
{
	struct fixture fixture = {0};
	fixture.mapping = mmap(NULL, TEST_MAP_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
	                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(fixture.mapping != MAP_FAILED);
	fixture.mh = fixture.mapping;
	fixture.slide = (uintptr_t)fixture.mapping - (uintptr_t)TEST_VMADDR;

	struct mach_header_64* header = (struct mach_header_64*)fixture.mapping;
	header->magic = MH_MAGIC_64;
	header->cputype = CPU_TYPE_ARM64;
	header->filetype = MH_EXECUTE;
	header->ncmds = 2;

	struct segment_command_64* text =
		(struct segment_command_64*)(header + 1);
	text->cmd = LC_SEGMENT_64;
	text->cmdsize = sizeof(*text) + sizeof(struct section_64);
	memcpy(text->segname, "__TEXT", 6);
	text->vmaddr = TEST_VMADDR;
	text->vmsize = TEST_MAP_SIZE;
	text->fileoff = 0;
	text->filesize = TEST_MAP_SIZE;
	text->maxprot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
	text->initprot = VM_PROT_READ | VM_PROT_EXECUTE;
	text->nsects = 1;

	struct section_64* section = (struct section_64*)(text + 1);
	memcpy(section->sectname, "__text", 6);
	memcpy(section->segname, "__TEXT", 6);
	section->addr = TEST_VMADDR + TEST_FUNCTION_OFFSET;
	section->size = 0x100;
	section->offset = TEST_FUNCTION_OFFSET;
	section->flags = S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;

	struct linkedit_data_command_fixture* starts =
		(struct linkedit_data_command_fixture*)((uint8_t*)text + text->cmdsize);
	starts->cmd = 0x26; /* LC_FUNCTION_STARTS */
	starts->cmdsize = sizeof(*starts);
	starts->dataoff = 0x800;
	starts->datasize = 4;
	fixture.mapping[0x800] = 0x80; /* ULEB128(0x1000) */
	fixture.mapping[0x801] = 0x20;
	fixture.mapping[0x802] = 0x10; /* second function is 16 bytes later */
	fixture.mapping[0x803] = 0x00; /* terminator */

	header->sizeofcmds = text->cmdsize + starts->cmdsize;

	const uint8_t original[] = {
		0x40, 0x05, 0x80, 0x52, /* mov w0, #42 */
		0xc0, 0x03, 0x5f, 0xd6  /* ret */
	};
	memcpy(fixture.original, original, sizeof(original));
	memcpy(fixture.mapping + TEST_FUNCTION_OFFSET, original, sizeof(original));
	memcpy(fixture.mapping + TEST_FUNCTION_OFFSET + 0x10,
	       original, sizeof(original));

	trampoline_set_pool(fixture.mapping + TEST_POOL_OFFSET, 0x1000);
	return fixture;
}

static void destroy_fixture(struct fixture* fixture)
{
	assert(munmap(fixture->mapping, TEST_MAP_SIZE) == 0);
}

static char* write_temp_file(const char* contents)
{
	char* path = strdup("/tmp/machismo-address-hooks-XXXXXX");
	assert(path);
	int fd = mkstemp(path);
	assert(fd >= 0);
	size_t length = strlen(contents);
	assert(write(fd, contents, length) == (ssize_t)length);
	assert(close(fd) == 0);
	return path;
}

static int run_config(const char* contents, struct fixture* fixture)
{
	char* path = write_temp_file(contents);
	int result = trampoline_patch_addresses(fixture->mh, fixture->slide, path);
	assert(unlink(path) == 0);
	free(path);
	return result;
}

static int run_hook(const char* library, const char* symbol,
                    uintptr_t vmaddr, const char* bytes,
                    struct fixture* fixture)
{
	char config[1024];
	int length = snprintf(config, sizeof(config),
	                      "# pinned test hook\n0x%lx %s %s %s # inline comment\n",
	                      (unsigned long)vmaddr, library, symbol, bytes);
	assert(length > 0 && (size_t)length < sizeof(config));
	return run_config(config, fixture);
}

static void test_config_fields(void)
{
	char* path = write_temp_file(
		"[general]\n"
		"address_hooks = configs/sword3/address-hooks.conf\n"
		"entry_override = 0x100001234\n"
		"entry_expected = 40058052c0035fd6\n"
		"entry_prepare_lib = ./libsword3_host.so\n"
		"entry_prepare_symbol = sword3_host_prepare\n"
		"audit_only = true\n");
	machismo_config_t config;
	assert(config_load(path, &config) == 0);
	assert(strcmp(config.address_hooks, "configs/sword3/address-hooks.conf") == 0);
	assert(config.has_entry_override == 1);
	assert(config.entry_override == (uintptr_t)0x100001234ULL);
	assert(strcmp(config.entry_expected, "40058052c0035fd6") == 0);
	assert(strcmp(config.entry_prepare_lib, "./libsword3_host.so") == 0);
	assert(strcmp(config.entry_prepare_symbol, "sword3_host_prepare") == 0);
	assert(config.audit_only == 1);
	config_free(&config);
	assert(unlink(path) == 0);
	free(path);
}

static void test_valid_hook(const char* library)
{
	struct fixture fixture = make_fixture();
	assert(trampoline_validate_function(
		fixture.mh, fixture.slide,
		TEST_VMADDR + TEST_FUNCTION_OFFSET,
		"40058052c0035fd6") == 0);
	assert(run_hook(library, "fake_lib_func",
	                TEST_VMADDR + TEST_FUNCTION_OFFSET,
	                "40058052c0035fd6", &fixture) == 1);
	uint32_t instruction;
	memcpy(&instruction, fixture.mapping + TEST_FUNCTION_OFFSET,
	       sizeof(instruction));
	assert((instruction & 0xfc000000U) == 0x14000000U);
	destroy_fixture(&fixture);
}

static void test_bytes_mismatch(const char* library)
{
	struct fixture fixture = make_fixture();
	assert(trampoline_validate_function(
		fixture.mh, fixture.slide,
		TEST_VMADDR + TEST_FUNCTION_OFFSET,
		"00000000") == -1);
	assert(run_hook(library, "fake_lib_func",
	                TEST_VMADDR + TEST_FUNCTION_OFFSET,
	                "00000000", &fixture) == -1);
	assert(memcmp(fixture.mapping + TEST_FUNCTION_OFFSET,
	              fixture.original, sizeof(fixture.original)) == 0);
	destroy_fixture(&fixture);
}

static void test_out_of_range(const char* library)
{
	struct fixture fixture = make_fixture();
	assert(run_hook(library, "fake_lib_func",
	                TEST_VMADDR + 0x800,
	                "80200000", &fixture) == -1);
	assert(memcmp(fixture.mapping + TEST_FUNCTION_OFFSET,
	              fixture.original, sizeof(fixture.original)) == 0);
	destroy_fixture(&fixture);
}

static void test_missing_symbol(const char* library)
{
	struct fixture fixture = make_fixture();
	char config[2048];
	int length = snprintf(
		config, sizeof(config),
		"0x%lx %s fake_lib_func 40058052\n"
		"0x%lx %s symbol_that_does_not_exist 40058052\n",
		(unsigned long)(TEST_VMADDR + TEST_FUNCTION_OFFSET), library,
		(unsigned long)(TEST_VMADDR + TEST_FUNCTION_OFFSET + 0x10), library);
	assert(length > 0 && (size_t)length < sizeof(config));
	assert(run_config(config, &fixture) == -1);
	assert(memcmp(fixture.mapping + TEST_FUNCTION_OFFSET,
	              fixture.original, sizeof(fixture.original)) == 0);
	assert(memcmp(fixture.mapping + TEST_FUNCTION_OFFSET + 0x10,
	              fixture.original, sizeof(fixture.original)) == 0);
	destroy_fixture(&fixture);
}

int main(int argc, char** argv)
{
	assert(argc == 2);
	test_config_fields();
	test_valid_hook(argv[1]);
	test_bytes_mismatch(argv[1]);
	test_out_of_range(argv[1]);
	test_missing_symbol(argv[1]);
	puts("address trampoline tests passed");
	return 0;
}
