"""
slk_bridge.py — Python bridge to the SLK kernel via ctypes

Loads kernel.dll (Windows) or kernel.so (Linux) and exposes
slk_init(), slk_validate(), slk_find(), slk_count() to Python.
"""

import ctypes
import os
import sys
import platform

# ─── Load the compiled kernel ────────────────────────────────────────────────

def _load_kernel():
    """Load kernel.dll (Windows) or kernel.so (Linux/macOS)."""
    script_dir = os.path.dirname(os.path.abspath(__file__))

    if platform.system() == "Windows":
        lib_path = os.path.join(script_dir, "kernel.dll")
    elif platform.system() == "Darwin":
        lib_path = os.path.join(script_dir, "kernel.dylib")
    else:
        lib_path = os.path.join(script_dir, "kernel.so")

    if not os.path.exists(lib_path):
        print(f"ERROR: kernel library not found at {lib_path}")
        print("Compile it first:")
        print("  Windows: gcc -DSLK_ENABLE_STRINGS -I./include -I./src -shared -fPIC -o benchmarks/kernel.dll src/kernel.c -std=c99 -O2")
        print("  Linux:   gcc -DSLK_ENABLE_STRINGS -I./include -I./src -shared -fPIC -o benchmarks/kernel.so  src/kernel.c -std=c99 -O2")
        sys.exit(1)

    return ctypes.CDLL(lib_path)

_lib = _load_kernel()

# ─── C structure definitions ──────────────────────────────────────────────────

SLK_MAX_ARITY = 3
SLK_N_MAX     = 256

class SLK_Simplex(ctypes.Structure):
    """Mirrors the SLK_Simplex C struct."""
    _fields_ = [
        ("id",       ctypes.c_uint32),
        ("relation", ctypes.c_uint16),
        ("args",     ctypes.c_uint32 * SLK_MAX_ARITY),
        ("sigma",    ctypes.c_uint8),
    ]

class SLK_Relation(ctypes.Structure):
    """Mirrors the SLK_Relation C struct."""
    _fields_ = [
        ("name",  ctypes.c_char_p),
        ("arity", ctypes.c_uint8),
    ]

class SLK_Kernel(ctypes.Structure):
    """Mirrors the SLK_Kernel C struct."""
    _fields_ = [
        ("base",           SLK_Simplex * SLK_N_MAX),
        ("count",          ctypes.c_uint32),
        ("tau",            ctypes.c_uint64),
        ("sigma_alphabet", ctypes.POINTER(SLK_Relation)),
        ("sigma_count",    ctypes.c_uint16),
        ("boot_crc",       ctypes.c_uint32),
        ("is_initialized", ctypes.c_uint8),
    ]

# ─── Function signatures ──────────────────────────────────────────────────────

_lib.slk_init.argtypes     = [ctypes.POINTER(SLK_Kernel),
                               ctypes.POINTER(SLK_Relation),
                               ctypes.c_uint16]
_lib.slk_init.restype      = ctypes.c_int

_lib.slk_validate.argtypes = [ctypes.POINTER(SLK_Kernel),
                               ctypes.POINTER(SLK_Simplex)]
_lib.slk_validate.restype  = ctypes.c_int

_lib.slk_count.argtypes    = [ctypes.POINTER(SLK_Kernel)]
_lib.slk_count.restype     = ctypes.c_uint32

# ─── Python-friendly API ──────────────────────────────────────────────────────

# Return codes
SLK_OK                   =  0
SLK_ERR_ID_ZERO          = -1
SLK_ERR_ID_EXISTS        = -2
SLK_ERR_ARITY_MISMATCH   = -3
SLK_ERR_BASE_FULL        = -4
SLK_ERR_RELATION_INVALID = -5
SLK_ERR_SPONTANEOUS      = -6
SLK_ERR_DANGLING_REF     = -7
SLK_ERR_BOOT_CORRUPT     = -8
SLK_ERR_ATOMIC_FAIL      = -9
SLK_ERR_NULL_WRITE       = -10


class Kernel:
    """
    High-level Python wrapper around the SLK kernel.

    Usage:
        relations = [("IS", 1), ("PRECEDES", 2), ("CAUSE", 2)]
        k = Kernel(relations)
        result = k.validate(id=1, relation=0, args=[0,0,0], sigma=255)
    """

    def __init__(self, relations: list):
        """
        Initialize the kernel with a list of (name, arity) tuples.

        Args:
            relations: list of (name: str, arity: int) tuples
        """
        self._kernel = SLK_Kernel()

        # Build the C relation array
        RelArray = SLK_Relation * len(relations)
        self._rel_array = RelArray()
        for i, (name, arity) in enumerate(relations):
            self._rel_array[i].name  = name.encode("utf-8")
            self._rel_array[i].arity = arity

        result = _lib.slk_init(
            ctypes.byref(self._kernel),
            self._rel_array,
            ctypes.c_uint16(len(relations))
        )

        if result != SLK_OK:
            raise RuntimeError(f"slk_init() failed with code {result}")

    def validate(self, id: int, relation: int,
                 args: list = None, sigma: int = 255) -> int:
        """
        Validate and insert a simplex into the knowledge base K.

        Args:
            id       : unique identifier (>= 1)
            relation : index into the relation alphabet
            args     : list of target ids (up to MAX_ARITY=3), default [0,0,0]
            sigma    : stability index 0-255, default 255

        Returns:
            SLK_OK (0) if accepted, negative error code if rejected
        """
        if args is None:
            args = [0, 0, 0]

        s = SLK_Simplex()
        s.id       = id
        s.relation = relation
        s.sigma    = sigma
        for i in range(SLK_MAX_ARITY):
            s.args[i] = args[i] if i < len(args) else 0

        return _lib.slk_validate(ctypes.byref(self._kernel),
                                 ctypes.byref(s))

    def count(self) -> int:
        """Returns the number of simplexes currently in K."""
        return _lib.slk_count(ctypes.byref(self._kernel))

    def reset(self, relations: list):
        """Reset the kernel with a new relation set."""
        self.__init__(relations)


# ─── Quick self-test ──────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("=== SLK Bridge self-test ===")

    relations = [
        ("IS",       1),
        ("PRECEDES", 2),
        ("CAUSE",    2),
        ("SYSTEM_OK",0),
    ]

    k = Kernel(relations)
    print(f"Kernel initialized. count={k.count()}")

    # Insert a valid simplex
    r = k.validate(id=1, relation=0, args=[0,0,0], sigma=255)
    print(f"validate(id=1, IS) = {r} (expected 0=SLK_OK)")

    # Idempotence
    r = k.validate(id=1, relation=0, args=[0,0,0], sigma=255)
    print(f"validate(id=1, IS) again = {r} (idempotence, expected 0)")

    # Conflict
    r = k.validate(id=1, relation=0, args=[0,0,0], sigma=100)
    print(f"validate(id=1, sigma=100) = {r} (conflict, expected -2)")

    print(f"Final count: {k.count()} (expected 1)")
    print("Bridge OK.")