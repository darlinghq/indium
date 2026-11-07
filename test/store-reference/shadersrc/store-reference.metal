/*
Copyright © 2020 Apple Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <metal_stdlib>
using namespace metal;

/// A fixture for stores through a re-typed pointer, the mirror of the loads
/// test/lighting's vertex stage performs.
///
/// A Metal matrix is an array of 16-byte-padded columns, so writing one column
/// writes four floats wide even though the column is a float3. metalfe reaches
/// a column with a getelementptr that either stops at the matrix or descends
/// into it, and then re-types the address as a pointer to the padded column
/// with "bitcast ptr to ptr". Nothing in that pointer records the column, so a
/// translator that trusts the pointer's declared pointee stores a 16-byte
/// column through a pointer to something else entirely.

struct Uniforms {
    float4 color;
    float3x3 basis;
};

kernel void write_columns(device Uniforms& dst [[buffer(0)]],
                          constant Uniforms& src [[buffer(1)]])
{
    dst.color = src.color;

    dst.basis[0] = src.basis[0];
    dst.basis[1] = src.basis[1];
    dst.basis[2] = src.basis[2];
}
