//-------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Copyright (c) 2014 Andrew Duncan
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------

#define _GNU_SOURCE

#include <getopt.h>
#include <math.h>
#include <png.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bcm_host.h"

//-----------------------------------------------------------------------

#ifndef ALIGN_TO_16
#define ALIGN_TO_16(x)  ((x + 15) & ~15)
#endif

//-----------------------------------------------------------------------

const char* program = NULL;

//-----------------------------------------------------------------------

int
main(
    int argc,
    char *argv[])
{
    int opt = 0;

    bool writeToStdout = false;
    char *pngName = "snapshot.png";
    int32_t requestedWidth = 0;
    int32_t requestedHeight = 0;
    uint32_t displayNumber = 0;
    int delay = 0;

    VC_IMAGE_TYPE_T imageType = VC_IMAGE_RGBA32;
    int8_t dmxBytesPerPixel  = 4;

    int result = 0;

    program = basename(argv[0]);

    //-------------------------------------------------------------------

    char *sopts = "d:D:Hh:p:w:s";

    struct option lopts[] =
    {
        { "delay", required_argument, NULL, 'd' },
        { "display", required_argument, NULL, 'D' },
        { "height", required_argument, NULL, 'h' },
        { "help", no_argument, NULL, 'H' },
        { "pngname", required_argument, NULL, 'p' },
        { "width", required_argument, NULL, 'w' },
        { "stdout", no_argument, NULL, 's' },
        { NULL, no_argument, NULL, 0 }
    };

    while ((opt = getopt_long(argc, argv, sopts, lopts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'd':

            delay = atoi(optarg);
            break;

        case 'D':

            displayNumber = atoi(optarg);
            break;

        case 'h':

            requestedHeight = atoi(optarg);
            break;

        case 'p':

            pngName = optarg;
            break;

        case 'w':

            requestedWidth = atoi(optarg);
            break;

        case 's':

            writeToStdout = true;
            break;

        case 'H':
        default:

            //-----------------------------------------------------------

            fprintf(stderr, "Usage: %s [--pngname name]", program);
            fprintf(stderr, " [--width <width>] [--height <height>]");
            fprintf(stderr, " [--delay <delay>] [--display <number>]");
            fprintf(stderr, " [--stdout] [--help]\n");

            fprintf(stderr, "\n");

            fprintf(stderr, "    --pngname - name of png file to create ");
            fprintf(stderr, "(default is %s)\n", pngName);

            fprintf(stderr, "    --height - image height ");
            fprintf(stderr, "(default is screen height)\n");

            fprintf(stderr, "    --width - image width ");
            fprintf(stderr, "(default is screen width)\n");

            fprintf(stderr, "    --delay - delay in seconds ");
            fprintf(stderr, "(default %d)\n", delay);

            fprintf(stderr, "    --display - Raspberry Pi display number ");
            fprintf(stderr, "(default %d)\n", displayNumber);

            fprintf(stderr, "    --stdout - write file to stdout\n");

            fprintf(stderr, "    --help - print this usage information\n");

            fprintf(stderr, "\n");

            //-----------------------------------------------------------

            if (opt == 'H')
            {
                exit(EXIT_SUCCESS);
            }
            else
            {
                exit(EXIT_FAILURE);
            }

            break;
        }
    }

    //-------------------------------------------------------------------

    bcm_host_init();

    //-------------------------------------------------------------------
    //
    // When the display is rotate (either 90 or 270 degrees) we need to
    // swap the width and height of the snapshot
    //

    char response[1024];
    int displayRotated = 0;

    if (vc_gencmd(response, sizeof(response), "get_config int") == 0)
    {
        vc_gencmd_number_property(response,
                                  "display_rotate",
                                  &displayRotated);
    }

    //-------------------------------------------------------------------

    if (delay)
    {
        sleep(delay);
    }

    //-------------------------------------------------------------------

    DISPMANX_DISPLAY_HANDLE_T displayHandle
        = vc_dispmanx_display_open(displayNumber);

    DISPMANX_MODEINFO_T modeInfo;
    result = vc_dispmanx_display_get_info(displayHandle, &modeInfo);

    if (result != 0)
    {
        fprintf(stderr, "%s: unable to get display information\n", program);
        exit(EXIT_FAILURE);
    }

    int32_t pngWidth = modeInfo.width;
    int32_t pngHeight = modeInfo.height;

    if (requestedWidth > 0)
    {
        pngWidth = requestedWidth;

        if (requestedHeight == 0)
        {
            double numerator = modeInfo.height * requestedWidth;
            double denominator = modeInfo.width;

            pngHeight = (int32_t)ceil(numerator / denominator);
        }
    }

    if (requestedHeight > 0)
    {
        pngHeight = requestedHeight;

        if (requestedWidth == 0)
        {
            double numerator = modeInfo.width * requestedHeight;
            double denominator = modeInfo.height;

            pngWidth = (int32_t)ceil(numerator / denominator);
        }
    }

    //-------------------------------------------------------------------
    // only need to check low bit of displayRotated (value of 1 or 3).
    // If the display is rotated either 90 or 270 degrees (value 1 or 3)
    // the width and height need to be transposed.

    int32_t dmxWidth = pngWidth;
    int32_t dmxHeight = pngHeight;

    if (displayRotated & 1)
    {
        dmxWidth = pngHeight;
        dmxHeight = pngWidth;
    }

    int32_t dmxPitch = dmxBytesPerPixel * ALIGN_TO_16(dmxWidth);

    void *dmxImagePtr = malloc(dmxPitch * dmxHeight);

    if (dmxImagePtr == NULL)
    {
        fprintf(stderr, "%s: unable to allocated image buffer\n", program);
        exit(EXIT_FAILURE);
    }

    //-------------------------------------------------------------------

    uint32_t vcImagePtr = 0;
    DISPMANX_RESOURCE_HANDLE_T resourceHandle;
    resourceHandle = vc_dispmanx_resource_create(imageType,
                                                 dmxWidth,
                                                 dmxHeight,
                                                 &vcImagePtr);

    result = vc_dispmanx_snapshot(displayHandle,
                                  resourceHandle,
                                  DISPMANX_NO_ROTATE);

    if (result != 0)
    {
        vc_dispmanx_resource_delete(resourceHandle);
        vc_dispmanx_display_close(displayHandle);

        fprintf(stderr, "%s: vc_dispmanx_snapshot() failed\n", program);
        exit(EXIT_FAILURE);
    }

    VC_RECT_T rect;
    result = vc_dispmanx_rect_set(&rect, 0, 0, dmxWidth, dmxHeight);

    if (result != 0)
    {
        vc_dispmanx_resource_delete(resourceHandle);
        vc_dispmanx_display_close(displayHandle);

        fprintf(stderr, "%s: vc_dispmanx_rect_set() failed\n", program);
        exit(EXIT_FAILURE);
    }

    result = vc_dispmanx_resource_read_data(resourceHandle,
                                            &rect,
                                            dmxImagePtr,
                                            dmxPitch);


    if (result != 0)
    {
        vc_dispmanx_resource_delete(resourceHandle);
        vc_dispmanx_display_close(displayHandle);

        fprintf(stderr,
                "%s: vc_dispmanx_resource_read_data() failed\n",
                program);

        exit(EXIT_FAILURE);
    }

    vc_dispmanx_resource_delete(resourceHandle);
    vc_dispmanx_display_close(displayHandle);

    //-------------------------------------------------------------------
    // Convert from RGBA (32 bit) to RGB (24 bit)

    int8_t pngBytesPerPixel = 3;
    int32_t pngPitch = pngBytesPerPixel * pngWidth;
    void *pngImagePtr = malloc(pngPitch * pngHeight);

    int32_t j = 0;
    for (j = 0 ; j < pngHeight ; j++)
    {
        int32_t dmxXoffset = 0;
        int32_t dmxYoffset = 0;

        switch (displayRotated & 3)
        {
        case 0: // 0 degrees

            if (displayRotated & 0x20000) // flip vertical
            {
                dmxYoffset = (dmxHeight - j) * dmxPitch;
            }
            else
            {
                dmxYoffset = j * dmxPitch;
            }

            break;

        case 1: // 90 degrees


            if (displayRotated & 0x20000) // flip vertical
            {
                dmxXoffset = j * dmxBytesPerPixel;
            }
            else
            {
                dmxXoffset = (dmxWidth - j) * dmxBytesPerPixel;
            }

            break;

        case 2: // 180 degrees

            if (displayRotated & 0x20000) // flip vertical
            {
                dmxYoffset = j * dmxPitch;
            }
            else
            {
                dmxYoffset = (dmxHeight - j) * dmxPitch;
            }

            break;

        case 3: // 270 degrees

            if (displayRotated & 0x20000) // flip vertical
            {
                dmxXoffset = (dmxWidth - j) * dmxBytesPerPixel;
            }
            else
            {
                dmxXoffset = j * dmxBytesPerPixel;
            }

            break;
        }

        int32_t i = 0;
        for (i = 0 ; i < pngWidth ; i++)
        {
            uint8_t *pngPixelPtr = pngImagePtr
                                 + (i * pngBytesPerPixel)
                                 + (j * pngPitch);

            switch (displayRotated & 3)
            {
            case 0: // 0 degrees

                if (displayRotated & 0x10000) // flip horizontal
                {
                    dmxXoffset = (dmxWidth - i) * dmxBytesPerPixel;
                }
                else
                {
                    dmxXoffset = i * dmxBytesPerPixel;
                }

                break;

            case 1: // 90 degrees

                if (displayRotated & 0x10000) // flip horizontal
                {
                    dmxYoffset = (dmxHeight - i) * dmxPitch;
                }
                else
                {
                    dmxYoffset = i * dmxPitch;
                }

                break;

            case 2: // 180 degrees

                if (displayRotated & 0x10000) // flip horizontal
                {
                    dmxXoffset = i * dmxBytesPerPixel;
                }
                else
                {
                    dmxXoffset = (dmxWidth - i) * dmxBytesPerPixel;
                }

                break;

            case 3: // 270 degrees

                if (displayRotated & 0x10000) // flip horizontal
                {
                    dmxYoffset = i * dmxPitch;
                }
                else
                {
                    dmxYoffset = (dmxHeight - i) * dmxPitch;
                }

                break;
            }

            uint8_t *dmxPixelPtr = dmxImagePtr + dmxXoffset + dmxYoffset;

            memcpy(pngPixelPtr, dmxPixelPtr, 3);
        }
    }

    free(dmxImagePtr);
    dmxImagePtr = NULL;

    //-------------------------------------------------------------------

    png_structp pngPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                                 NULL,
                                                 NULL,
                                                 NULL);

    if (pngPtr == NULL)
    {
        fprintf(stderr,
                "%s: unable to allocated PNG write structure\n",
                program);

        exit(EXIT_FAILURE);
    }

    png_infop infoPtr = png_create_info_struct(pngPtr);

    if (infoPtr == NULL)
    {
        fprintf(stderr,
                "%s: unable to allocated PNG info structure\n",
                program);

        exit(EXIT_FAILURE);
    }

    if (setjmp(png_jmpbuf(pngPtr)))
    {
        fprintf(stderr, "%s: unable to create PNG\n", program);
        exit(EXIT_FAILURE);
    }

    FILE *pngfp = NULL;

    if (writeToStdout)
    {
        pngfp = stdout;
    }
    else
    {
        pngfp = fopen(pngName, "wb");

        if (pngfp == NULL)
        {
            fprintf(stderr,
                    "%s: unable to create %s - %s\n",
                    program,
                    pngName,
                    strerror(errno));

            exit(EXIT_FAILURE);
        }
    }

    png_init_io(pngPtr, pngfp);

    int png_color_type = PNG_COLOR_TYPE_RGB;

    png_set_IHDR(
        pngPtr,
        infoPtr,
        pngWidth,
        pngHeight,
        8,
        png_color_type,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE,
        PNG_FILTER_TYPE_BASE);

    png_write_info(pngPtr, infoPtr);

    int y = 0;
    for (y = 0; y < pngHeight; y++)
    {
        png_write_row(pngPtr, pngImagePtr + (pngPitch * y));
    }

    png_write_end(pngPtr, NULL);
    png_destroy_write_struct(&pngPtr, &infoPtr);

    if (pngfp != stdout)
    {
        fclose(pngfp);
    }

    //-------------------------------------------------------------------

    free(pngImagePtr);
    pngImagePtr = NULL;

    return 0;
}

