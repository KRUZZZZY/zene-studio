#!/usr/bin/env python3
# make_stub_model.py - build a tiny 4-stem ONNX model for pipeline tests
#
# Copyright (c) 2026 Zene Studio contributors
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# The stub is a linear 4-way split: each output is the input multiplied by a
# scalar gain (drums 0.4, bass 0.3, other 0.2, vocals 0.1; sum = 1.0). That
# makes end-to-end results exactly checkable (sum of stems == mix) while
# exercising the real ONNX Runtime execution path, the real model contract
# (float32 [1,2,T] in, four float32 [1,2,T] outputs named drums/bass/other/
# vocals) and the real overlap-add segmentation.
#
# The file is assembled with a hand-rolled protobuf writer so the test fixture
# can be regenerated with the Python standard library alone (no onnx package
# needed). Run: python3 tools/make_stub_model.py tests/data/stub-4stem-linear.onnx
# and, for the single stacked-output layout the public HTDemucs exports use:
#      python3 tools/make_stub_model.py --stacked tests/data/stub-4stem-stacked.onnx
# and two broken layouts the loader must refuse (same gains, same element count):
#      python3 tools/make_stub_model.py --transposed tests/data/stub-4stem-transposed.onnx
#      python3 tools/make_stub_model.py --int64 tests/data/stub-4stem-int64.onnx

import struct
import sys

STEM_GAINS = (("drums", 0.4), ("bass", 0.3), ("other", 0.2), ("vocals", 0.1))

ELEM_TYPE_FLOAT = 1
ELEM_TYPE_INT64 = 7


def varint(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def tag(field, wire_type):
    return varint((field << 3) | wire_type)


def field_varint(field, value):
    return tag(field, 0) + varint(value)


def field_bytes(field, payload):
    return tag(field, 2) + varint(len(payload)) + payload


def field_string(field, text):
    return field_bytes(field, text.encode("utf-8"))


def field_float(field, value):
    return tag(field, 5) + struct.pack("<f", value)


def tensor_type(dim_param=None, dim_value=None):
    """TypeProto with tensor_type float32 and shape [1, 2, T]."""
    shape = b""
    for dim in (("d", 1), ("d", 2),
                (("p", dim_param) if dim_param else ("v", dim_value))):
        if dim[0] == "p":
            shape += field_bytes(1, field_string(2, dim[1]))
        else:
            shape += field_bytes(1, field_varint(1, dim[1]))
    tensor = field_varint(1, ELEM_TYPE_FLOAT) + field_bytes(2, shape)
    return field_bytes(1, tensor)


def value_info(name, dim_param=None, dim_value=None):
    return (field_string(1, name)
            + field_bytes(2, tensor_type(dim_param=dim_param, dim_value=dim_value)))


def initializer(name, gains):
    return (field_varint(1, len(gains))          # dims = [n]
            + field_varint(2, ELEM_TYPE_FLOAT)   # data_type
            + b"".join(field_float(4, g) for g in gains)  # float_data
            + field_string(8, name))


def node(op_type, inputs, output, name):
    return (b"".join(field_string(1, i) for i in inputs)
            + field_string(2, output)
            + field_string(3, name)
            + field_string(4, op_type))


def build_model(dim_param="T"):
    graph = b""
    for stem, gain in STEM_GAINS:
        graph += field_bytes(1, node("Mul", ["mix", f"gain_{stem}"], stem, f"mul_{stem}"))
    graph += field_string(2, "stub-4stem-linear")
    for stem, gain in STEM_GAINS:
        graph += field_bytes(5, initializer(f"gain_{stem}", [gain]))
    graph += field_bytes(11, value_info("mix", dim_param=dim_param))
    for stem, _ in STEM_GAINS:
        graph += field_bytes(12, value_info(stem, dim_param=dim_param))

    opset = field_varint(2, 17)
    model = (field_varint(1, 8)                      # ir_version
             + field_string(2, "lmms-stem-split-stub")  # producer_name
             + field_bytes(7, graph)                 # graph
             + field_bytes(8, opset))                # opset_import
    return model


def tensor_type_dims(dims, elem_type=ELEM_TYPE_FLOAT):
    """TypeProto of any rank (float32 unless told otherwise); a str entry is a symbolic dimension."""
    shape = b""
    for dim in dims:
        shape += field_bytes(1, field_string(2, dim) if isinstance(dim, str) else field_varint(1, dim))
    return field_bytes(1, field_varint(1, elem_type) + field_bytes(2, shape))


def attribute(name, value):
    """AttributeProto: an INTS attribute for a list, an INT otherwise."""
    if isinstance(value, list):
        return field_string(1, name) + field_varint(20, 7) + b"".join(field_varint(8, v) for v in value)
    return field_string(1, name) + field_varint(20, 2) + field_varint(3, value)


def node_with(op_type, inputs, output, name, attributes):
    return node(op_type, inputs, output, name) + b"".join(field_bytes(5, a) for a in attributes)


def build_stacked_model(dim_param="T"):
    """The layout the public HTDemucs exports ship (owner decision 14): ONE output
    `stems` of shape [1, 4, 2, T], the stems on axis 1 in contract order. It is
    one Mul: mix [1, 2, T] broadcasts against gains [1, 4, 1, 1] to [1, 4, 2, T],
    with the same gains as the four-output stub, so both stubs split alike."""
    gains = [gain for _, gain in STEM_GAINS]
    graph = field_bytes(1, node("Mul", ["mix", "gains"], "stems", "mul_stems"))
    graph += field_string(2, "stub-4stem-stacked")
    graph += field_bytes(5, b"".join(field_varint(1, d) for d in (1, len(gains), 1, 1))
                         + field_varint(2, ELEM_TYPE_FLOAT)
                         + b"".join(field_float(4, g) for g in gains)
                         + field_string(8, "gains"))
    graph += field_bytes(11, field_string(1, "mix") + field_bytes(2, tensor_type_dims([1, 2, dim_param])))
    graph += field_bytes(12, field_string(1, "stems")
                         + field_bytes(2, tensor_type_dims([1, len(gains), 2, dim_param])))
    return (field_varint(1, 8)
            + field_string(2, "lmms-stem-split-stub")
            + field_bytes(7, graph)
            + field_bytes(8, field_varint(2, 17)))


def build_layout_variant_model(kind, dim_param="T"):
    """Four-output stubs with the linear stub's gains, each built to be refused by the
    loader. `transposed` writes every stem as [1, T, 2]: the right element count with
    the channel axis in the wrong place. `int64` casts every stem to int64 in the
    [1, 2, T] layout: the right shape with the wrong element type."""
    graph = b""
    for stem, _ in STEM_GAINS:
        graph += field_bytes(1, node("Mul", ["mix", f"gain_{stem}"], f"{stem}_scaled", f"mul_{stem}"))
        if kind == "transposed":
            graph += field_bytes(1, node_with("Transpose", [f"{stem}_scaled"], stem, f"transpose_{stem}",
                                              [attribute("perm", [0, 2, 1])]))
        else:
            graph += field_bytes(1, node_with("Cast", [f"{stem}_scaled"], stem, f"cast_{stem}",
                                              [attribute("to", ELEM_TYPE_INT64)]))
    graph += field_string(2, f"stub-4stem-{kind}")
    for stem, gain in STEM_GAINS:
        graph += field_bytes(5, initializer(f"gain_{stem}", [gain]))
    graph += field_bytes(11, value_info("mix", dim_param=dim_param))
    for stem, _ in STEM_GAINS:
        if kind == "transposed":
            out_type = tensor_type_dims([1, dim_param, 2])
        else:
            out_type = tensor_type_dims([1, 2, dim_param], ELEM_TYPE_INT64)
        graph += field_bytes(12, field_string(1, stem) + field_bytes(2, out_type))
    return (field_varint(1, 8)
            + field_string(2, "lmms-stem-split-stub")
            + field_bytes(7, graph)
            + field_bytes(8, field_varint(2, 17)))


def main():
    args = sys.argv[1:]
    stacked = "--stacked" in args
    variant = next((a[2:] for a in args if a in ("--transposed", "--int64")), None)
    args = [a for a in args if a != "--stacked" and a not in ("--transposed", "--int64")]
    if len(args) != 1:
        print(__doc__)
        sys.exit(2)
    path = args[0]
    if stacked:
        model = build_stacked_model()
    elif variant:
        model = build_layout_variant_model(variant)
    else:
        model = build_model()
    with open(path, "wb") as fh:
        fh.write(model)
    print(f"wrote {path} ({len(model)} bytes)")


if __name__ == "__main__":
    main()
