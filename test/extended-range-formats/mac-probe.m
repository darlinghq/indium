// Copyright (C) 2026 Lubos Dolezel
//
// Prints what a Metal device does with the extended-range pixel formats, as "name value..." lines of plain
// numbers, for the cases the documentation leaves open and the ones gen-fixtures.py computes from the
// documented encoding. Run it on a Mac with a Metal device and compare (or replace the fixtures) with the output:
//
//     clang -fobjc-arc -framework Metal -framework Foundation -o xr-probe mac-probe.m && ./xr-probe
//
// sample <format> <code>             the float a shader reads from a texel whose four channels are <code>, as hex bits
// raw <format> <what> <bytes...>     the bytes getBytes returns after replaceRegion of bytes with a code that is
//                                    not part of the documented range or padding bits that are not zero
// render <format> <bits>             the codes (blue green red alpha) stored for a value a fragment shader returns
// clear <format> <r> <g> <b> <a>     the same for a clear color
// blend <format>                     the codes after SourceAlpha / OneMinusSourceAlpha blending of (1, 0, 2, 0.6) over
//                                    a clear color of (0.25, 0.5, 0.75, 1)
// Writes nothing but stdout. Prints "no Metal device" and exits when there is none.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdint.h>
#include <string.h>

static id<MTLDevice> device;
static id<MTLCommandQueue> queue;

static const MTLPixelFormat formats[4] = { MTLPixelFormatBGRA10_XR, MTLPixelFormatBGRA10_XR_sRGB, MTLPixelFormatBGR10_XR, MTLPixelFormatBGR10_XR_sRGB };
static const char* names[4] = { "BGRA10_XR", "BGRA10_XR_sRGB", "BGR10_XR", "BGR10_XR_sRGB" };
static const NSUInteger sizes[4] = { 8, 8, 4, 4 };

static id<MTLTexture> make(int f, NSUInteger width, MTLTextureUsage usage) {
	MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat: formats[f] width: width height: 1 mipmapped: NO];
	d.usage = usage;
	d.storageMode = MTLStorageModeManaged;
	return [device newTextureWithDescriptor: d];
}

static void put(int f, uint8_t* d, uint32_t b, uint32_t g, uint32_t r, uint32_t a) {
	if (sizes[f] == 8) {
		uint16_t c[4] = { (uint16_t)(b << 6), (uint16_t)(g << 6), (uint16_t)(r << 6), (uint16_t)(a << 6) };
		memcpy(d, c, 8);
	} else {
		uint32_t w = b | (g << 10) | (r << 20);
		memcpy(d, &w, 4);
	}
}

static void codesOf(int f, id<MTLTexture> t, NSUInteger count, uint32_t (*codes)[4]) {
	uint8_t* bytes = calloc(count, sizes[f]);
	[t getBytes: bytes bytesPerRow: count * sizes[f] fromRegion: MTLRegionMake2D(0, 0, count, 1) mipmapLevel: 0];
	for (NSUInteger i = 0; i < count; i++) {
		if (sizes[f] == 8) {
			uint16_t c[4]; memcpy(c, bytes + i * 8, 8);
			for (int k = 0; k < 4; k++) codes[i][k] = c[k] >> 6;
		} else {
			uint32_t w; memcpy(&w, bytes + i * 4, 4);
			codes[i][0] = w & 0x3ff; codes[i][1] = (w >> 10) & 0x3ff; codes[i][2] = (w >> 20) & 0x3ff; codes[i][3] = 0;
		}
	}
	free(bytes);
}

static void render(id<MTLTexture> target, MTLClearColor clear, id<MTLRenderPipelineState> pipeline) {
	MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
	pass.colorAttachments[0].texture = target;
	pass.colorAttachments[0].loadAction = MTLLoadActionClear;
	pass.colorAttachments[0].storeAction = MTLStoreActionStore;
	pass.colorAttachments[0].clearColor = clear;
	id<MTLCommandBuffer> commands = [queue commandBuffer];
	id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor: pass];
	if (pipeline) {
		[encoder setRenderPipelineState: pipeline];
		[encoder drawPrimitives: MTLPrimitiveTypeTriangle vertexStart: 0 vertexCount: 3];
	}
	[encoder endEncoding];
	[commands commit];
	[commands waitUntilCompleted];
}

int main(void) {
	@autoreleasepool {
		device = MTLCreateSystemDefaultDevice();
		if (device == nil) {
			printf("no Metal device\n");
			return 0;
		}
		queue = [device newCommandQueue];
		const uint32_t codes[] = { 0, 1, 2, 100, 255, 383, 384, 385, 510, 639, 893, 894, 895, 1000, 1021, 1022, 1023 };
		const NSUInteger codeCount = sizeof(codes) / sizeof(codes[0]);
		// values to render, as float bits: a few in range, the range ends, beyond it, NaN, and exact halves between codes
		const float values[] = { -100.0f, -1.0f, -0.752941f, -0.5271f, -0.001f, 0.0f, 0.2f, 0.5f, 1.0f, 1.25098f, 1.66894f, 2.0f, 100.0f,
			(500.5f - 384.0f) / 510.0f, (501.5f - 384.0f) / 510.0f, (1022.5f - 384.0f) / 510.0f, NAN };
		const NSUInteger valueCount = sizeof(values) / sizeof(values[0]);

		NSMutableString* source = [NSMutableString stringWithString: @"#include <metal_stdlib>\nusing namespace metal;\n"
			"kernel void readTexel(texture2d<float> tex [[texture(0)]], device float4* out [[buffer(0)]], uint tid [[threadgroup_position_in_grid]]) {\n"
			"	out[tid] = tex.read(uint2(tid, 0));\n}\n"
			"struct V { float4 position [[position]]; };\n"
			"vertex V fullscreen(uint vid [[vertex_id]]) {\n"
			"	float2 p[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) };\n"
			"	V v; v.position = float4(p[vid], 0, 1); return v;\n}\n"];
		[source appendFormat: @"constant uint fillBits[%lu] = {", (unsigned long)valueCount];
		for (NSUInteger i = 0; i < valueCount; i++) {
			uint32_t bits; memcpy(&bits, &values[i], 4);
			[source appendFormat: @"%s0x%08xu", i ? ", " : "", bits];
		}
		[source appendString: @"};\nfragment float4 fill(V in [[stage_in]]) {\n"
			"	float v = as_type<float>(fillBits[(uint)in.position.x]);\n	return float4(v, v, v, v);\n}\n"
			"fragment float4 blendSource(V in [[stage_in]]) { return float4(1.0, 0.0, 2.0, 0.6); }\n"];
		NSError* error = nil;
		id<MTLLibrary> library = [device newLibraryWithSource: source options: nil error: &error];
		if (library == nil) {
			printf("no library: %s\n", [[error description] UTF8String]);
			return 1;
		}
		id<MTLComputePipelineState> read = [device newComputePipelineStateWithFunction: [library newFunctionWithName: @"readTexel"] error: &error];

		for (int f = 0; f < 4; f++) {
			id<MTLTexture> t = make(f, codeCount, MTLTextureUsageShaderRead);
			if (t == nil) {
				printf("texture %s: none\n", names[f]);
				continue;
			}
			uint8_t bytes[17 * 8];
			for (NSUInteger i = 0; i < codeCount; i++)
				put(f, bytes + i * sizes[f], codes[i], codes[i], codes[i], codes[i]);
			[t replaceRegion: MTLRegionMake2D(0, 0, codeCount, 1) mipmapLevel: 0 withBytes: bytes bytesPerRow: codeCount * sizes[f]];
			id<MTLBuffer> out = [device newBufferWithLength: codeCount * 16 options: MTLResourceStorageModeShared];
			id<MTLCommandBuffer> commands = [queue commandBuffer];
			id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
			[encoder setComputePipelineState: read];
			[encoder setTexture: t atIndex: 0];
			[encoder setBuffer: out offset: 0 atIndex: 0];
			[encoder dispatchThreadgroups: MTLSizeMake(codeCount, 1, 1) threadsPerThreadgroup: MTLSizeMake(1, 1, 1)];
			[encoder endEncoding];
			[commands commit];
			[commands waitUntilCompleted];
			const uint32_t* bits = [out contents];
			for (NSUInteger i = 0; i < codeCount; i++)
				printf("sample %s %u %08x %08x %08x %08x\n", names[f], codes[i], bits[i * 4], bits[i * 4 + 1], bits[i * 4 + 2], bits[i * 4 + 3]);

			// raw bytes the documentation does not define: code 1023, alpha codes outside 384..894, padding bits
			struct { const char* what; uint32_t b, g, r, a; uint8_t padding; } raws[] = {
				{ "code1023", 1023, 1023, 1023, 600, 0 },
				{ "alpha0", 500, 500, 500, 0, 0 },
				{ "alpha383", 500, 500, 500, 383, 0 },
				{ "alpha895", 500, 500, 500, 895, 0 },
				{ "alpha1023", 500, 500, 500, 1023, 0 },
				{ "padding", 500, 600, 700, 600, 0x3f },
			};
			for (size_t i = 0; i < sizeof(raws) / sizeof(raws[0]); i++) {
				id<MTLTexture> one = make(f, 1, MTLTextureUsageShaderRead);
				uint8_t in[8] = { 0 }, back[8] = { 0 };
				put(f, in, raws[i].b, raws[i].g, raws[i].r, raws[i].a);
				if (raws[i].padding) {
					if (sizes[f] == 8) { in[0] |= 0x3f; in[2] |= 0x3f; in[4] |= 0x3f; in[6] |= 0x3f; } else in[3] |= 0xc0;
				}
				[one replaceRegion: MTLRegionMake2D(0, 0, 1, 1) mipmapLevel: 0 withBytes: in bytesPerRow: sizes[f]];
				[one getBytes: back bytesPerRow: sizes[f] fromRegion: MTLRegionMake2D(0, 0, 1, 1) mipmapLevel: 0];
				printf("raw %s %s", names[f], raws[i].what);
				for (NSUInteger k = 0; k < sizes[f]; k++) printf(" %02x", back[k]);
				printf("\n");
			}

			MTLTextureUsage usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
			MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
			d.vertexFunction = [library newFunctionWithName: @"fullscreen"];
			d.fragmentFunction = [library newFunctionWithName: @"fill"];
			d.colorAttachments[0].pixelFormat = formats[f];
			id<MTLRenderPipelineState> fill = [device newRenderPipelineStateWithDescriptor: d error: &error];
			id<MTLTexture> target = make(f, valueCount, usage);
			render(target, MTLClearColorMake(0, 0, 0, 0), fill);
			uint32_t (*result)[4] = calloc(valueCount, sizeof(*result));
			codesOf(f, target, valueCount, result);
			for (NSUInteger i = 0; i < valueCount; i++) {
				uint32_t bits; memcpy(&bits, &values[i], 4);
				printf("render %s %08x %u %u %u %u\n", names[f], bits, result[i][0], result[i][1], result[i][2], result[i][3]);
			}
			free(result);

			id<MTLTexture> cleared = make(f, 1, usage);
			render(cleared, MTLClearColorMake(1.5, -1.0, 0.5, 2.0), nil);
			uint32_t one[1][4];
			codesOf(f, cleared, 1, one);
			printf("clear %s 1.5 -1.0 0.5 2.0 %u %u %u %u\n", names[f], one[0][0], one[0][1], one[0][2], one[0][3]);

			d.fragmentFunction = [library newFunctionWithName: @"blendSource"];
			d.colorAttachments[0].blendingEnabled = YES;
			d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
			d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
			d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
			d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
			id<MTLRenderPipelineState> blend = [device newRenderPipelineStateWithDescriptor: d error: &error];
			id<MTLTexture> blended = make(f, 1, usage);
			render(blended, MTLClearColorMake(0.25, 0.5, 0.75, 1.0), blend);
			codesOf(f, blended, 1, one);
			printf("blend %s %u %u %u %u\n", names[f], one[0][0], one[0][1], one[0][2], one[0][3]);
		}
	}
	return 0;
}
