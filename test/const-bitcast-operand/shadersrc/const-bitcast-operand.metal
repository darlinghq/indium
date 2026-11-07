#include <metal_stdlib>
using namespace metal;

/// A fixture for a bitcast whose operand is a constant.
///
/// Every other fixture's bitcasts re-type a pointer, so the operand is a value
/// the translator has already emitted a type for. A constant is emitted without
/// one: declareConstantScalar and its siblings never call setResultType, so
/// there is no recorded type to look up and the bitcast's source type has to be
/// recovered from the instruction's own LLVM type instead.
///
/// Nothing in a Metal source produces this. It is here because it is the only
/// AIR shape that reaches the branch, and a branch that is never taken is a
/// branch nobody can tell is wrong.
///
/// The kernel does real work as well, so the fixture is a readback and not just
/// a translation: the module has to not only survive the branch, it has to
/// produce a pipeline that writes what the Metal below says it should.

kernel void constant_bitcast_operand(device float4* dst [[buffer(0)]],
                                     constant float4& src [[buffer(1)]])
{
    dst[0] = src;
}
