/* 原生矢量绘制，无第三方依赖。用法：render_icon SIZE OUTPUT.png */
#import <AppKit/AppKit.h>
static void rounded(NSRect rect,CGFloat radius,NSColor *color) {
    [color setFill];[[NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius] fill];
}
static void board(CGFloat x,CGFloat y,CGFloat angle,BOOL front) {
    [NSGraphicsContext saveGraphicsState];NSAffineTransform *transform=[NSAffineTransform transform];
    [transform translateXBy:x yBy:y];[transform rotateByDegrees:angle];[transform concat];
    rounded(NSMakeRect(-155,-208,310,416),34,[NSColor colorWithCalibratedWhite:1 alpha:front?1:.75]);
    rounded(NSMakeRect(-78,176,156,58),20,[NSColor colorWithCalibratedRed:.12 green:.36 blue:.77 alpha:1]);
    rounded(NSMakeRect(-52,191,104,19),9,[NSColor colorWithCalibratedWhite:1 alpha:.88]);
    NSColor *ink=[NSColor colorWithCalibratedRed:.26 green:.51 blue:.84 alpha:front?.8:.5];
    for(int i=0;i<3;i++) rounded(NSMakeRect(-105,65-i*65,i==2?128:210,20),10,ink);
    [NSGraphicsContext restoreGraphicsState];
}
static void arrow(CGFloat x,CGFloat y,BOOL right) {
    NSBezierPath *path=[NSBezierPath bezierPath];path.lineWidth=29;
    path.lineCapStyle=NSLineCapStyleRound;path.lineJoinStyle=NSLineJoinStyleRound;
    CGFloat sign=right?1:-1;
    [path moveToPoint:NSMakePoint(x-sign*65,y)];[path lineToPoint:NSMakePoint(x+sign*65,y)];
    [path moveToPoint:NSMakePoint(x+sign*28,y+37)];[path lineToPoint:NSMakePoint(x+sign*65,y)];
    [path lineToPoint:NSMakePoint(x+sign*28,y-37)];[[NSColor whiteColor] setStroke];[path stroke];
}
int main(int argc,const char **argv) { @autoreleasepool {
    if(argc!=3) return 1;int size=atoi(argv[1]);if(size<16 || size>1024) return 1;
    NSBitmapImageRep *bitmap=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:size pixelsHigh:size
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    [NSGraphicsContext saveGraphicsState];[NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap]];
    NSAffineTransform *scale=[NSAffineTransform transform];[scale scaleBy:(CGFloat)size/1024];[scale concat];
    NSBezierPath *background=[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(64,64,896,896) xRadius:200 yRadius:200];
    NSGradient *gradient=[[NSGradient alloc] initWithStartingColor:[NSColor colorWithCalibratedRed:.06 green:.27 blue:.66 alpha:1]
        endingColor:[NSColor colorWithCalibratedRed:.16 green:.62 blue:.91 alpha:1]];
    [gradient drawInBezierPath:background angle:65];
    board(370,554,12,NO);board(633,475,-10,YES);
    rounded(NSMakeRect(396,212,242,186),56,[NSColor colorWithCalibratedRed:.04 green:.28 blue:.65 alpha:1]);
    arrow(513,337,YES);arrow(513,273,NO);
    [NSGraphicsContext restoreGraphicsState];
    NSData *png=[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return [png writeToFile:[NSString stringWithUTF8String:argv[2]] atomically:YES]?0:1;
} }
