#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 2) return 2;
        NSString *expected = [NSString stringWithUTF8String:argv[1]];
        NSArray *devices = MTLCopyAllDevices();
        BOOL found = NO;
        for (id<MTLDevice> device in devices) {
            printf("available: %s\n", [[device name] UTF8String]);
            found |= [[device name] isEqualToString:expected];
        }
        if (!found) { puts("SKIP expected device unavailable"); return 2; }
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        printf("default: %s\n", [[device name] UTF8String]);
        if (![[device name] isEqualToString:expected]) { puts("FAIL selected another device"); return 1; }
        puts("PASS selected available expected device");
        [devices release];
        return 0;
    }
}
