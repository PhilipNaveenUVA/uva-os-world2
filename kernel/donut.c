/*
    Draw a rotating donut on text console or screen.
    dependency:
        delay
        fb (uart may work? depending on terminal program)
    CREDITS: see end of the file

    lab2: a multitasking version
    - support multi donuts (via xoff,yoff arguments)
    - global buffer -> multiple smaller buffers

*/

#include "debug.h"
#include "plat.h"
#include "utils.h"

#define PIXELSIZE 4 
typedef unsigned int PIXEL;

#include "fb.h"
static inline void setpixel(unsigned char *buf, int x, int y, int pit, PIXEL p) {
    assert(x >= 0 && y >= 0); // important guard
    *(PIXEL *)(buf + y * pit + x * PIXELSIZE) = p;
}

static const int xoff[] = {0,NN/2,0,NN/2};
static const int yoff[] = {0,0,NN/2,NN/2};
_Static_assert(N_DONUTS <= NELEM(xoff));

enum {K=4}; // donut scale factor, see code below
_Static_assert(80*K  <= NN/2); // columns
_Static_assert(22*K*2  <= NN/2); // rows

static char b[N_DONUTS][1760];        // text buffer (W 80 H 22?
static signed char z[N_DONUTS][1760]; // z buffer

void donut_canvas_init(void) {
    fb_fini();

    the_fb.width = NN;
    the_fb.height = NN;

    the_fb.vwidth = NN;
    the_fb.vheight = NN;

    if (fb_init() != 0)
        BUG();
}



static PIXEL int2rgb (int value); 

#define R(mul, shift, x, y)              \
    _ = x;                               \
    x -= mul * y >> shift;               \
    y += mul * _ >> shift;               \
    _ = (3145728 - x * x - y * y) >> 11; \
    x = x * _ >> 10;                     \
    y = y * _ >> 10;


void donut_pixel(int idx) {
    // Initial rotation angles and helper variable
    int rotA = 1024, rotB = 1024, cosA = 0, cosB = 0, _;
    
    // Animation control variables
    int timing_factor = 2;        // Controls animation speed
    int animation_frame = 0;      // Tracks current frame
    unsigned begin_sec, begin_msec;
    current_time(&begin_sec, &begin_msec);
    
    while (1) {
        // Clear buffers for next frame
        memset(b[idx], 0, 1760);    
        memset(z[idx], 127, 1760);  

        // Main rotation calculations
        int sinTheta = 0, cosTheta = 1024;
        for (int theta = 0; theta < 90; theta++) {
            int sinPhi = 0, cosPhi = 1024;
            
            for (int phi = 0; phi < 324; phi++) {
                // Torus parameters
                int innerR = 1, outerR = 2048, depthK = 5120 * 1024;

                // Calculate 3D coordinates and projections
                int p0 = innerR * cosTheta + outerR,
                    p1 = cosPhi * p0 >> 10,
                    p2 = cosA * sinTheta >> 10,
                    p3 = sinPhi * p0 >> 10,
                    p4 = innerR * p2 - (rotA * p3 >> 10),
                    p5 = rotA * sinTheta >> 10,
                    p6 = depthK + innerR * 1024 * p5 + cosA * p3,
                    p7 = cosTheta * sinPhi >> 10;

                // Screen coordinates
                int screenX = 25 + 30 * (cosB * p1 - rotB * p4) / p6,
                    screenY = 12 + 15 * (cosB * p4 + rotB * p1) / p6;

                // Calculate illumination
                int brightness = (((-cosA * p7 - cosB * ((-rotA * p7 >> 10) + p2) - 
                               cosPhi * (cosTheta * rotB >> 10)) >> 10) - p5);

                // Normalize brightness
                brightness = brightness < 0 ? 0 : brightness/5;
                brightness = brightness < 255 ? brightness : 255;

                // Buffer position
                int pos = screenX + 80 * screenY;
                signed char depth = (p6 - depthK) >> 15;

                // Update buffers if pixel is visible
                if (screenY > 0 && screenY < 22 && screenX > 0 && screenX < 80 && 
                    depth < z[idx][pos]) {
                    z[idx][pos] = depth;
                    b[idx][pos] = brightness;
                }

                R(5, 8, cosPhi, sinPhi)
            }
            R(9, 7, cosTheta, sinTheta)
        }

        // Update rotation angles based on frame
        if (animation_frame % timing_factor == 0) {
            R(5, 7, cosA, rotA);
            R(5, 8, cosB, rotB);
        }
        animation_frame++;

        // Render the frame
        int screen_offsetX = xoff[idx];
        int screen_offsetY = yoff[idx];
        int pixelY = 0, pixelX = 0;

        for (int bufferPos = 0; bufferPos < 1761; bufferPos++) {
            if (bufferPos % 80) {
                if (pixelX < 50) {
                    // Calculate screen positions with offset
                    int displayX = pixelX * K + screen_offsetX;
                    int displayY = pixelY * K * 2 + screen_offsetY;
                    
                    // Convert brightness to color and set pixels
                    PIXEL color = int2rgb(b[idx][bufferPos]);
                    setpixel(the_fb.fb, displayX, displayY, the_fb.pitch, color);
                    setpixel(the_fb.fb, displayX + 1, displayY, the_fb.pitch, color);
                    setpixel(the_fb.fb, displayX, displayY + 1, the_fb.pitch, color);
                    setpixel(the_fb.fb, displayX + 1, displayY + 1, the_fb.pitch, color);
                }
                pixelX++;
            } else {
                pixelY++;
                pixelX = 1;
            }
        }

        // Check exit condition
        unsigned current_sec, current_msec;
        current_time(&current_sec, &current_msec);
        unsigned time_elapsed = (current_sec - begin_sec) * 1000 + 
                              (current_msec - begin_msec);
        
        if (time_elapsed >= 3000 && idx == 0) {
            printf("Donut task idx %d exiting after %u ms\n", idx, time_elapsed);
            exit_process(0);
        }

        yield();
    }
}


static PIXEL int2rgb (int value) {
    int r,g,b;     
    if (value >= 0 && value <= 85) {
        r = 0;
        g = (value * 3);
        b = 0;
    } else if (value > 85 && value <= 170) {
        r = 255 - ((value - 85) * 3);
        g = 255;
        b = (value - 85) * 3;
    } else if (value > 170 && value <= 255) {
        r = 0;
        g = 255 - ((value - 170) * 3);
        b = 255;
    } else {
        r=g=b=0;
    }    
    return (r<<16)|(g<<8)|b; 
}

void donut(int idx) {
    donut_pixel(idx);
}

/**
 * Original author:
 * https://twitter.com/a1k0n
 * https://www.a1k0n.net/2021/01/13/optimizing-donut.html
 *
 * Change Logs:
 * Date           Author       Notes
 * 2006-09-15     Andy Sloane  First version
 * 2011-07-20     Andy Sloane  Second version
 * 2021-01-13     Andy Sloane  Third version
 * 2021-03-25     Meco Man     Port to RT-Thread RTOS
 *
 *
 *  js code for both canvas & text version
 *  https://www.a1k0n.net/js/donut.js
 *
 *  ported by FL
 * From the NJU OS project:
 * https://github.com/NJU-ProjectN/am-kernels/blob/master/kernels/demo/src/donut/donut.c
 *
 */