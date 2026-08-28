#!/usr/bin/env python3
"""Host-side generator and read-only runtime tests for the ObjC shim."""

from __future__ import annotations

import concurrent.futures
import ctypes
import json
import os
import runpy
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools" / "generate_objc_shim.py"
MANIFEST = ROOT / "configs" / "sword3" / "binary-manifest.json"
SHIM_C = ROOT / "src" / "shim" / "sword3_objc_shim.c"
SHIM_INCLUDE = ROOT / "src" / "shim"

EXPECTED_DATA = {
    "OBJC_CLASS_$_NSObject",
    "OBJC_METACLASS_$_NSObject",
    "_objc_empty_cache",
    "_objc_empty_vtable",
}

EXPECTED_FUNCTIONS = {
    "__objc_personality_v0",
    "class_addMethod",
    "class_addProperty",
    "class_addProtocol",
    "class_getInstanceMethod",
    "class_getInstanceVariable",
    "class_getName",
    "class_getSuperclass",
    "class_isMetaClass",
    "class_replaceMethod",
    "method_setImplementation",
    "objc_alloc",
    "objc_allocateClassPair",
    "objc_autoreleasePoolPop",
    "objc_autoreleasePoolPush",
    "objc_autoreleaseReturnValue",
    "objc_begin_catch",
    "objc_constructInstance",
    "objc_copyClassNamesForImage",
    "objc_destroyWeak",
    "objc_end_catch",
    "objc_enumerationMutation",
    "objc_getClass",
    "objc_getMetaClass",
    "objc_getProperty",
    "objc_getProtocol",
    "objc_getRequiredClass",
    "objc_initializeClassPair",
    "objc_loadWeakRetained",
    "objc_lookUpClass",
    "objc_msgSend",
    "objc_msgSendSuper2",
    "objc_readClassPair",
    "objc_registerClassPair",
    "objc_release",
    "objc_retain",
    "objc_retainAutorelease",
    "objc_retainAutoreleaseReturnValue",
    "objc_retainAutoreleasedReturnValue",
    "objc_setProperty_atomic",
    "objc_setProperty_atomic_copy",
    "objc_setProperty_nonatomic_copy",
    "objc_storeStrong",
    "objc_storeWeak",
    "objc_sync_enter",
    "objc_sync_exit",
    "object_getClass",
    "object_getIndexedIvars",
    "object_getIvar",
    "property_copyAttributeList",
    "protocol_getMethodDescription",
    "protocol_getName",
    "sel_getUid",
}

LOOKUP_FIXTURE = r"""
#include "sword3_objc_shim.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct normal_list {
    uint32_t entsize_and_flags;
    uint32_t count;
    struct sword3_objc_method entries[1];
};

struct small_list {
    uint32_t entsize_and_flags;
    uint32_t count;
    struct sword3_objc_relative_method entries[2];
};

struct one_small_list {
    uint32_t entsize_and_flags;
    uint32_t count;
    struct sword3_objc_relative_method entries[1];
};

struct test_object {
    sword3_objc_Class isa;
    void *indexed;
};

static void normal_imp(void) {}
static void small_imp(void) {}
static void small_second_imp(void) {}
static void small_indirect_imp(void) {}
static char small_name[] = "small:";
static char small_second_name[] = "smallSecond:";
static char small_types[] = "v@:@";
static sword3_objc_sel small_indirect_selref;
static struct small_list small;
static struct one_small_list small_indirect;

static int32_t relative_offset(const void *field, const void *target)
{
    uintptr_t from = (uintptr_t)field;
    uintptr_t to = (uintptr_t)target;

    if (to >= from) {
        uintptr_t difference = to - from;
        assert(difference <= INT32_MAX);
        return (int32_t)difference;
    } else {
        uintptr_t magnitude = from - to;
        assert(magnitude <= (uint64_t)INT32_MAX + 1);
        return (int32_t)-(int64_t)magnitude;
    }
}

int main(int argc, char **argv)
{
    sword3_objc_sel normal_selector = sel_registerName("normal:");
    sword3_objc_sel small_selector = sel_getUid("small:");
    sword3_objc_sel small_second_selector = sel_getUid("smallSecond:");
    sword3_objc_sel small_indirect_selector =
        sel_getUid("smallIndirect:");
    struct normal_list normal = {
        .entsize_and_flags = sizeof(struct sword3_objc_method),
        .count = 1,
        .entries = {{
            .name = normal_selector,
            .types = "v@:@",
            .imp = normal_imp,
        }},
    };
    struct sword3_objc_class_ro base_ro = {
        .instance_size = sizeof(struct test_object),
        .name = "Base",
        .base_methods = (const struct sword3_objc_method_list *)&normal,
    };
    struct sword3_objc_class_ro child_ro = {
        .instance_size = sizeof(struct test_object),
        .name = "Child",
        .base_methods = (const struct sword3_objc_method_list *)&small,
    };
    struct sword3_objc_class_ro indirect_ro = {
        .instance_size = sizeof(struct test_object),
        .name = "Indirect",
        .base_methods =
            (const struct sword3_objc_method_list *)&small_indirect,
    };
    struct sword3_objc_class base = {
        .data_bits = (uintptr_t)&base_ro,
    };
    struct sword3_objc_class indirect = {
        .superclass = &base,
        .data_bits = (uintptr_t)&indirect_ro,
    };
    struct sword3_objc_class child = {
        .superclass = &indirect,
        .data_bits = (uintptr_t)&child_ro | (uintptr_t)3,
    };
    struct sword3_objc_class_ro tiny_ro = {
        .instance_size = sizeof(void *) + 1,
        .name = "Tiny",
    };
    struct sword3_objc_class tiny = {
        .data_bits = (uintptr_t)&tiny_ro,
    };
    struct test_object object = {
        .isa = &child,
    };
    struct sword3_objc_method *method;
    struct sword3_objc_method *second_method;
    struct test_object *allocated;
    void *pool;
    void *weak = &object;

    small.entsize_and_flags = SWORD3_OBJC_METHOD_LIST_SMALL |
                              SWORD3_OBJC_METHOD_LIST_DIRECT_SELECTORS |
                              sizeof(struct sword3_objc_relative_method);
    small.count = 2;
    small.entries[0].name =
        relative_offset(&small.entries[0].name, small_name);
    small.entries[0].types =
        relative_offset(&small.entries[0].types, small_types);
    small.entries[0].imp =
        relative_offset(
            &small.entries[0].imp,
            (const void *)(uintptr_t)small_imp
        );
    small.entries[1].name =
        relative_offset(&small.entries[1].name, small_second_name);
    small.entries[1].types =
        relative_offset(&small.entries[1].types, small_types);
    small.entries[1].imp =
        relative_offset(
            &small.entries[1].imp,
            (const void *)(uintptr_t)small_second_imp
        );

    small_indirect.entsize_and_flags =
        SWORD3_OBJC_METHOD_LIST_SMALL |
        sizeof(struct sword3_objc_relative_method);
    small_indirect.count = 1;
    small_indirect_selref = small_indirect_selector;
    small_indirect.entries[0].name = relative_offset(
        &small_indirect.entries[0].name, &small_indirect_selref
    );
    small_indirect.entries[0].types = relative_offset(
        &small_indirect.entries[0].types, small_types
    );
    small_indirect.entries[0].imp = relative_offset(
        &small_indirect.entries[0].imp,
        (const void *)(uintptr_t)small_indirect_imp
    );

    assert(sel_registerName("normal:") == normal_selector);
    assert(strcmp(sel_getName(small_selector), "small:") == 0);
    assert(sel_registerName(NULL) == NULL);
    assert(object_getClass(NULL) == NULL);
    assert(object_getClass(&object) == &child);
    assert(class_getSuperclass(&child) == &indirect);
    assert(strcmp(class_getName(&child), "Child") == 0);
    assert(objc_getClass("Child") == &child);
    assert(objc_getClass("NSObject") != NULL);
    assert(class_isMetaClass(objc_getMetaClass("NSObject")));

    assert(sword3_objc_lookup_imp(&object, normal_selector, NULL) == normal_imp);
    assert(sword3_objc_lookup_imp(&object, small_selector, NULL) == small_imp);
    assert(sword3_objc_lookup_imp(
        &object, small_indirect_selector, NULL
    ) == small_indirect_imp);
    assert(sword3_objc_lookup_imp(&object, normal_selector, &base) == normal_imp);
    method = class_getInstanceMethod(&child, small_selector);
    assert(method != NULL && method->imp == small_imp);
    second_method =
        class_getInstanceMethod(&child, small_second_selector);
    assert(second_method != NULL && second_method->imp == small_second_imp);
    assert(method != second_method && method->imp == small_imp);
    assert(object_getIndexedIvars(&object) ==
           (unsigned char *)&object + sizeof(object));

    allocated = objc_alloc(&tiny);
    assert(allocated != NULL);
    assert(object_getIndexedIvars(allocated) ==
           (unsigned char *)allocated + 2 * sizeof(void *));
    free(allocated);

    objc_setProperty_atomic(
        &object, normal_selector, &base, offsetof(struct test_object, indexed)
    );
    assert(objc_getProperty(
        &object, normal_selector, offsetof(struct test_object, indexed), true
    ) == &base);
    assert(objc_getProperty(&object, normal_selector, -1, true) == NULL);
    assert(objc_getProperty(
        &object, normal_selector, sizeof(object), true
    ) == NULL);

    assert(objc_retain(NULL) == NULL);
    objc_release(NULL);
    assert(objc_storeWeak(&weak, NULL) == NULL && weak == NULL);
    assert(objc_loadWeakRetained(&weak) == NULL);
    objc_destroyWeak(NULL);
    objc_storeStrong(NULL, NULL);
    pool = objc_autoreleasePoolPush();
    assert(pool != NULL);
    objc_autoreleasePoolPop(pool);
    objc_autoreleasePoolPop(NULL);

    if (argc > 1 && strcmp(argv[1], "unknown") == 0)
        (void)sword3_objc_lookup_imp(
            &object, sel_registerName("missingSelector:"), NULL
        );
    return 0;
}
"""


class ObjCShimTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.compiler = shutil.which(os.environ.get("CC", "cc"))
        if cls.compiler is None:
            raise unittest.SkipTest("host C compiler is unavailable")
        cls.temp = tempfile.TemporaryDirectory()
        cls.temp_path = Path(cls.temp.name)
        cls.generated = cls.temp_path / "sword3_objc_imports.c"
        cls.library_path = cls.temp_path / "libsword3_objc_shim.so"
        cls.fixture_source = cls.temp_path / "lookup_fixture.c"
        cls.fixture_binary = cls.temp_path / "lookup_fixture"
        cls.fixture_source.write_text(LOOKUP_FIXTURE, encoding="utf-8")

        subprocess.run(
            [
                sys.executable,
                str(GENERATOR),
                "--manifest",
                str(MANIFEST),
                "--output",
                str(cls.generated),
            ],
            cwd=ROOT,
            check=True,
        )
        common_flags = [
            cls.compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(SHIM_INCLUDE),
        ]
        subprocess.run(
            [
                *common_flags,
                "-fPIC",
                "-shared",
                str(cls.generated),
                str(SHIM_C),
                "-o",
                str(cls.library_path),
            ],
            cwd=ROOT,
            check=True,
        )
        subprocess.run(
            [
                *common_flags,
                str(cls.fixture_source),
                str(SHIM_C),
                "-o",
                str(cls.fixture_binary),
            ],
            cwd=ROOT,
            check=True,
        )
        cls.library = ctypes.CDLL(str(cls.library_path))

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temp.cleanup()

    def test_manifest_import_set_and_determinism(self) -> None:
        generator = runpy.run_path(str(GENERATOR), run_name="objc_generator")
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        imports = generator["read_imports"](manifest)
        self.assertEqual(set(imports.functions), EXPECTED_FUNCTIONS)
        self.assertEqual(set(imports.data), EXPECTED_DATA)

        second = self.temp_path / "second.c"
        subprocess.run(
            [
                sys.executable,
                str(GENERATOR),
                "--manifest",
                str(MANIFEST),
                "--output",
                str(second),
            ],
            check=True,
        )
        self.assertEqual(self.generated.read_bytes(), second.read_bytes())

    def test_generated_wrappers_are_per_symbol(self) -> None:
        generated = self.generated.read_text(encoding="utf-8")
        self.assertIn("unsupported libobjc import: class_addMethod", generated)
        self.assertIn(
            'sword3_objc_unsupported_symbol("objc_getProtocol")', generated
        )
        self.assertNotIn("unsupported libobjc import: objc_retain", generated)
        self.assertNotIn("unsupported libobjc import: objc_msgSend", generated)
        self.assertNotIn("OBJC_CLASS_$_NSObject", generated)

    def test_selector_intern_is_thread_safe(self) -> None:
        register = self.library.sel_registerName
        register.argtypes = [ctypes.c_char_p]
        register.restype = ctypes.c_void_p

        def intern(_: int) -> int:
            return int(register(b"threadedSelector:"))

        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as executor:
            pointers = set(executor.map(intern, range(4096)))
        self.assertEqual(len(pointers), 1)

    def test_read_only_metadata_and_nil_operations(self) -> None:
        subprocess.run([str(self.fixture_binary)], check=True)

    def test_unknown_selector_prints_name_and_aborts(self) -> None:
        completed = subprocess.run(
            [str(self.fixture_binary), "unknown"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, -signal.SIGABRT)
        self.assertIn("unrecognized selector: missingSelector:", completed.stderr)

    def test_generated_unsupported_wrapper_is_fail_fast(self) -> None:
        script = (
            "import ctypes,resource;"
            "resource.setrlimit(resource.RLIMIT_CORE,(0,0));"
            f"ctypes.CDLL({str(self.library_path)!r}).class_addMethod()"
        )
        completed = subprocess.run(
            [sys.executable, "-c", script],
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, -signal.SIGABRT)
        self.assertIn("unsupported runtime symbol: class_addMethod", completed.stderr)

    def test_objc_personality_explicitly_rejects_apple_exceptions(self) -> None:
        script = (
            "import ctypes,resource;"
            "resource.setrlimit(resource.RLIMIT_CORE,(0,0));"
            f"getattr(ctypes.CDLL({str(self.library_path)!r}),"
            "'__objc_personality_v0')(1,0,0,None,None)"
        )
        completed = subprocess.run(
            [sys.executable, "-c", script],
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, -signal.SIGABRT)
        self.assertIn("Apple exception ABI is unsupported", completed.stderr)


if __name__ == "__main__":
    unittest.main()
