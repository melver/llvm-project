=========================
LLVM PC Sections Metadata
=========================

.. contents::
   :local:

Introduction
============

PC Sections Metadata can be attached to instructions and functions, for which
addresses, viz. program counters (PCs), are to be emitted in specially encoded
binary sections. Metadata is assigned as an ``MDNode`` of the ``MD_pcsections``
kind; the following section describes the metadata format.

Metadata Format
===============

An arbitrary number of interleaved ``MDString`` and constant operators can be
added, where a new ``MDString`` always denotes a section name, followed by an
arbitrary number of auxiliary constant data encoded along the PC of the
instruction or function. The first operator must be a ``MDString`` denoting the
first section.

.. code-block:: none

  !0 = metadata !{
    metadata !"<section#1>"
    [ , iXX <aux-consts#1> ... ]
    [ metadata !"<section#2">
      [ , iXX <aux-consts#2> ... ]
      ... ]
  }

The occurrence of "section#1", "section#2", ..., "section#N" in the metadata
causes the backend to emit the PC for the associated instruction or function to
all named sections. For each emitted PC in a section #N, the constants
"aux-consts#N" will be emitted after the PC.

Binary Encoding
===============

*Instructions* result in emitting a single PC, and *functions* result in
emission of the start of the function and a 32-bit size. This is followed by
the auxiliary constants that followed the respective section name in the
``MD_pcsections`` metadata.

To avoid relocations in the final binary, each PC address stored at ``entry``
is a relative relocation, computed as ``pc - entry``. To decode, a user has to
compute ``entry + *entry``.

The size of each entry depends on the code model. With large and medium sized
code models, the entry size matches pointer size. For any smaller code model
the entry size is just 32 bits.
