#!/usr/bin/env python3
# make_stub_model.py - build a tiny 4-stem ONNX model for pipeline tests
#
# Copyright (c) 2026 LMMS Developers
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

import struct
import sys

STEM_GAINS = (("drums", 0.4), ("bass", 0.3), ("other", 0.2), ("vocals", 0.1))

ELEM_TYPE_FLOAT = 1


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


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    path = sys.argv[1]
    with open(path, "wb") as fh:
        fh.write(build_model())
    print(f"wrote {path} ({len(build_model())} bytes)")


if __name__ == "__main__":
    main()
