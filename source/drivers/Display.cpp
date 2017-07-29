/*
The MIT License (MIT)

Copyright (c) 2016 British Broadcasting Corporation.
This software is provided by Lancaster University by arrangement with the BBC.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

/**
  * Class definition for an abstract display.
  *
  */
#include "Display.h"
#include "ErrorNo.h"

using namespace codal;

/**
 * Constructor.
 *
 * Create a software representation an abstract display.
 * The display is initially blank.
 *
 * @param id The id the display should use when sending events on the MessageBus. Defaults to DEVICE_ID_DISPLAY.
 */
Display::Display(int width, int height, uint16_t id) : image(width, height) 
{
    this->width = width;
    this->height = height;
    this->id = id;
}

/**
 * Returns the width of the display
 *
 * @return display width
 *
 */
int Display::getWidth() 
{
    return width;
}

/**
 * Returns the height of the display
 *
 * @return display height
 *
 */
int Display::getHeight() 
{
    return height;
}

/**
 * Configures the brightness of the display.
 *
 * @param b The brightness to set the brightness to, in the range 0 - 255.
 *
 * @return DEVICE_OK, or DEVICE_INVALID_PARAMETER
 */
int Display::setBrightness(int b)
{
    //sanitise the brightness level
    if(b < 0 || b > 255)
        return DEVICE_INVALID_PARAMETER;

    this->brightness = b;

    return DEVICE_OK;
}


/**
  * Fetches the current brightness of this display.
  *
  * @return the brightness of this display, in the range 0..255.
  */
int Display::getBrightness()
{
    return this->brightness;
}


/**
  * Enable the display.
  */
void Display::enable()
{
}

/**
  * Disable the display.
  */
void Display::disable()
{
}

/**
  * Captures the bitmap currently being rendered on the display.
  *
  * @return a MicroBitImage containing the captured data.
  */
Image Display::screenShot()
{
    return image.crop(0,0, width, height);
}


/**
  * Destructor.
  */
Display::~Display()
{
}
