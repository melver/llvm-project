==========================================
Allocation-Partition Hints Instrumentation
==========================================

.. contents::
   :local:

Introduction
============

AllocPartition provides allocation partitioning hints to enable allocator-level
heap organization strategies. Clang assigns mode-dependent partition ID hints
to allocation calls, but makes no isolation guarantees; the runtime behavior
depends entirely on the implementation of a compatible memory allocator.

Possible allocator strategies include:

* **Security Hardening**: Placing allocations into separate, isolated heap
  regions. For example, separating pointer-containing types from raw data can
  mitigate exploits that rely on overflowing a primitive buffer to corrupt
  object metadata.

* **Memory Layout Optimization**: Grouping related allocations to improve data
  locality and cache utilization.

* **Custom Allocation Policies**: Applying different management strategies to
  different partitions.

Usage
=====

To enable the instrumentation, compile your code with the
``-fsanitize=alloc-partition`` flag:

.. code-block:: console

    % clang++ -fsanitize=alloc-partition example.cc

The instrumentation transforms allocation calls to include a partition ID. For
example:

.. code-block:: c

    // Original:
    ptr = malloc(size);

    // Instrumented:
    ptr = __alloc_partition_malloc(size, partition_id);

In addition, it is typically recommended to configure the following:

* ``-fsanitize-alloc-partition-max=<N>``
    Configures the maximum number of partitions. The default value is 64.

    .. code-block:: console

        % clang++ -fsanitize=alloc-partition -fsanitize-alloc-partition-max=512 example.cc

Partition Assignment Mode
=========================

The default mode is:

* *TypeHashPointerSplit* (mode=3): This mode assigns a partition ID based on
  the hash of the allocated type's name, where the top half ID-space is
  reserved for types that contain pointers and the bottom half for types that
  do not contain pointers.

Other partition ID assignment modes are supported, but they may be subject to
change or removal. These may (experimentally) be selected with ``-mllvm
-alloc-partition-mode=<mode>``:

* *TypeHash* (mode=2): This mode assigns a partition ID based on the hash of
  the allocated type's name.

* *Random* (mode=1): This mode assigns a statically-determined random partition
  ID to each allocation site.

* *Increment* (mode=0): This mode assigns a simple, incrementally increasing
  partition ID to each allocation site.

Runtime Interface
=================

A compatible runtime must be provided that implements the partitioned
allocation functions. The instrumentation generates calls to functions that
take a final ``uint64_t partition_id`` argument.

.. code-block:: c

    // C standard library functions
    void *__alloc_partition_malloc(size_t size, uint64_t partition_id);
    void *__alloc_partition_calloc(size_t count, size_t size, uint64_t partition_id);
    void *__alloc_partition_realloc(void *ptr, size_t size, uint64_t partition_id);
    // ...

    // C++ operators (mangled names)
    // operator new(size_t, uint64_t)
    void *__alloc_partition_Znwm(size_t size, uint64_t partition_id);
    // operator new[](size_t, uint64_t)
    void *__alloc_partition_Znam(size_t size, uint64_t partition_id);
    // ... other variants like nothrow, etc., are also instrumented.

Fast ABI
--------

An alternative ABI can be enabled with ``-fsanitize-alloc-partition-fast-abi``,
which encodes the partition ID hint in the allocation function name.

.. code-block:: c

    void *__alloc_partition_0_malloc(size_t size);
    void *__alloc_partition_1_malloc(size_t size);
    void *__alloc_partition_2_malloc(size_t size);
    ...
    void *__alloc_partition_0_Znwm(size_t size);
    void *__alloc_partition_1_Znwm(size_t size);
    void *__alloc_partition_2_Znwm(size_t size);
    ...

This ABI provides a more efficient alternative where
``-fsanitize-alloc-partition-max`` is small.

Instrumenting Non-Standard Allocation Functions
-----------------------------------------------

By default, AllocPartition only instruments standard library allocation
functions. This simplifies adoption, as a compatible allocator only needs to
provide partitioned variants for a well-defined set of standard functions.

To extend partitioning to custom allocation functions, enable broader coverage
with ``-fsanitize-alloc-partition-extended``. Such functions require being
marked with the `malloc
<https://clang.llvm.org/docs/AttributeReference.html#malloc>`_ or `alloc_size
<https://clang.llvm.org/docs/AttributeReference.html#alloc-size>`_ attributes
(or a combination).

For example:

.. code-block:: c

    void *custom_malloc(size_t size) __attribute__((malloc));
    void *my_malloc(size_t size) __attribute__((alloc_size(1)));

    // Original:
    ptr1 = custom_malloc(size);
    ptr2 = my_malloc(size);

    // Instrumented:
    ptr1 = __alloc_partition_custom_malloc(size, partition_id);
    ptr2 = __alloc_partition_my_malloc(size, partition_id);

Disabling Instrumentation
=========================

To exclude specific functions from instrumentation, you can use the
``no_sanitize("alloc-partition")`` attribute:

.. code-block:: c

    __attribute__((no_sanitize("alloc-partition")))
    void* custom_allocator(size_t size) {
        return malloc(size);  // Uses original malloc
    }

Note: Independent of any given allocator support, the instrumentation aims to
remain performance neutral. As such, ``no_sanitize("alloc-partition")``
functions may be inlined into instrumented functions and vice-versa. If
correctness is affected, such functions should explicitly be marked
``noinline``.

The ``__attribute__((disable_sanitizer_instrumentation))`` is also supported to
disable this and other sanitizer instrumentations.

Suppressions File (Ignorelist)
==============================

AllocPartition respects the ``src`` and ``fun`` entity types in the
:doc:`SanitizerSpecialCaseList`, which can be used to omit specified source
files or functions from instrumentation.

.. code-block:: bash

    # Exclude specific source files
    src:third_party/allocator.c
    # Exclude function name patterns
    fun:*custom_malloc*
    fun:LowLevel::*

.. code-block:: console

    % clang++ -fsanitize=alloc-partition -fsanitize-ignorelist=my_ignorelist.txt example.cc

Conditional Compilation with ``__has_feature(alloc_partition)``
===============================================================

In some cases, one may need to execute different code depending on whether
AllocPartition is enabled. The
:ref:`\_\_has\_feature <langext-__has_feature-__has_extension>` macro can be
used for this purpose.

.. code-block:: c

    #if defined(__has_feature) && __has_feature(alloc_partition)
    // Code specific to AllocPartition builds
    #endif
