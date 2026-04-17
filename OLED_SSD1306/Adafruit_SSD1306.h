/*********************************************************************
This is a library for our Monochrome OLEDs based on SSD1306 drivers

  Pick one up today in the adafruit shop!
  ------> http://www.adafruit.com/category/63_98

These displays use SPI to communicate, 4 or 5 pins are required to
interface

Adafruit invests time and resources providing this open source code,
please support Adafruit and open-source hardware by purchasing
products from Adafruit!

Written by Limor Fried/Ladyada  for Adafruit Industries.
BSD license, check license.txt for more information
All text above, and the splash screen must be included in any redistribution
*********************************************************************/

/*
 *  Modified by Neal Horman 7/14/2012 for use in mbed
 *
 *  Further modified by Istvan Cserny, January 31, 2022
 *  so that it could be used for the SH1106 I2C display as well
 *  The memory mode for the SSD1306 I2C display was changed to
 *  Page addressing mode (0x10) and the virtual void sendDisplayBuffer()
 *  function has been rewritten accordingly.
 *
 *
 *
 */

#ifndef _ADAFRUIT_SSD1306_H_
#define _ADAFRUIT_SSD1306_H_

#include "mbed.h"
#include "Adafruit_GFX.h"

#include <vector>
#include <algorithm>

// A DigitalOut sub-class that provides a constructed default state
class DigitalOut2 : public DigitalOut
{
public:
    DigitalOut2(PinName pin, bool active = false) : DigitalOut(pin)
    {
        write(active);
    };
    DigitalOut2& operator= (int value)
    {
        write(value);
        return *this;
    };
    DigitalOut2& operator= (DigitalOut2& rhs)
    {
        write(rhs.read());
        return *this;
    };
    operator int()
    {
        return read();
    };
};

#define SSD1306_EXTERNALVCC 0x1
#define SSD1306_SWITCHCAPVCC 0x2

/** The pure base class for the SSD1306 display driver.
 *
 * You should derive from this for a new transport interface type,
 * such as the SPI and I2C drivers.
 */
class Adafruit_SSD1306 : public Adafruit_GFX
{
public:
    Adafruit_SSD1306(PinName RST, uint8_t rawHeight = 32, uint8_t rawWidth = 128)
        : Adafruit_GFX(rawWidth,rawHeight)
        , rst(RST,false)
    {
        buffer.resize(rawHeight * rawWidth / 8);
    };

    void begin(uint8_t switchvcc = SSD1306_SWITCHCAPVCC);

    // These must be implemented in the derived transport driver
    virtual void command(uint8_t c) = 0;
    virtual void data(uint8_t c) = 0;
    virtual void drawPixel(int16_t x, int16_t y, uint16_t color);

    /// Clear the display buffer
    void clearDisplay(void);
    virtual void invertDisplay(bool i);

    /// Cause the display to be updated with the buffer content.
    void display();
    /// Fill the buffer with the AdaFruit splash screen.
    virtual void splash();

protected:
    virtual void sendDisplayBuffer() = 0;
    DigitalOut2 rst;

    // the memory buffer for the LCD
    std::vector<uint8_t> buffer;
};


/** This is the SPI SSD1306 display driver transport class
 *
 */
class Adafruit_SSD1306_Spi : public Adafruit_SSD1306
{
public:
    /** Create a SSD1306 SPI transport display driver instance with the specified DC, RST, and CS pins, as well as the display dimentions
     *
     * Required parameters
     * @param spi - a reference to an initialized SPI object
     * @param DC (Data/Command) pin name
     * @param RST (Reset) pin name
     * @param CS (Chip Select) pin name
     *
     * Optional parameters
     * @param rawHeight - the vertical number of pixels for the display, defaults to 32
     * @param rawWidth - the horizonal number of pixels for the display, defaults to 128
     */
    Adafruit_SSD1306_Spi(SPI &spi, PinName DC, PinName RST, PinName CS, uint8_t rawHieght = 32, uint8_t rawWidth = 128)
        : Adafruit_SSD1306(RST, rawHieght, rawWidth)
        , cs(CS,true)
        , dc(DC,false)
        , mspi(spi)
    {
        begin();
        splash();
        display();
    };

    virtual void command(uint8_t c)
    {
        cs = 1;
        dc = 0;
        cs = 0;
        mspi.write(c);
        cs = 1;
    };

    virtual void data(uint8_t c)
    {
        cs = 1;
        dc = 1;
        cs = 0;
        mspi.write(c);
        cs = 1;
    };

protected:
    virtual void sendDisplayBuffer()
    {
        cs = 1;
        dc = 1;
        cs = 0;

        for(uint16_t i=0, q=buffer.size(); i<q; i++)
            mspi.write(buffer[i]);

        if(height() == 32) {
            for(uint16_t i=0, q=buffer.size(); i<q; i++)
                mspi.write(0);
        }

        cs = 1;
    };

    DigitalOut2 cs, dc;
    SPI &mspi;
};

/** This is the I2C SSD1306 display driver transport class
 *
 */
class Adafruit_SSD1306_I2c : public Adafruit_SSD1306
{
public:
#define SSD_I2C_ADDRESS     0x78
    /** Create a SSD1306 I2C transport display driver instance with the specified RST pin name, the I2C address, as well as the display dimensions
     *
     * Required parameters
     * @param i2c - A reference to an initialized I2C object
     * @param RST - The Reset pin name
     *
     * Optional parameters
     * @param i2cAddress - The i2c address of the display
     * @param rawHeight - The vertical number of pixels for the display, defaults to 32
     * @param rawWidth - The horizonal number of pixels for the display, defaults to 128
     */
    Adafruit_SSD1306_I2c(I2C &i2c, PinName RST, uint8_t i2cAddress = SSD_I2C_ADDRESS, uint8_t rawHeight = 32, uint8_t rawWidth = 128)
        : Adafruit_SSD1306(RST, rawHeight, rawWidth)
        , mi2c(i2c)
        , mi2cAddress(i2cAddress)
    {
        begin();
        command(0x20);       // SSD1306_MEMORYMODE
        command(0x10);       // Page addressing mode (compatible with SG1106)
        splash();
        display();
    };

    virtual void command(uint8_t c)
    {
        char buff[2];
        buff[0] = 0; // Command Mode
        buff[1] = c;
        mi2c.write(mi2cAddress, buff, sizeof(buff));
    }

    virtual void data(uint8_t c)
    {
        char buff[2];
        buff[0] = 0x40; // Data Mode
        buff[1] = c;
        mi2c.write(mi2cAddress, buff, sizeof(buff));
    };

protected:
    virtual void sendDisplayBuffer()
    {

        char buff[256];
        char cmd[4] = {0, 0xB0, 0x00, 0x10};
        for (uint8_t m = 0; m < _rawHeight/8; m++) {
            buff[0] = 0x40;
            cmd[1] = 0xB0 + m;
            for(int i=0; i<128; i++) {
                buff[i+1]= buffer[m*128+i];
            }
            mi2c.write(mi2cAddress, cmd, 4);
            mi2c.write(mi2cAddress, buff, 129);
        }
    };

    I2C &mi2c;
    uint8_t mi2cAddress;
};

/** This is the I2C SH1106 display driver transport class
 *
 */
class Adafruit_SH1106_I2c : public Adafruit_SSD1306
{
public:
#define SSD_I2C_ADDRESS     0x78
    /** Create a SSD1306 I2C transport display driver instance with the specified RST pin name, the I2C address, as well as the display dimensions
     *
     * Required parameters
     * @param i2c - A reference to an initialized I2C object
     * @param RST - The Reset pin name
     *
     * Optional parameters
     * @param i2cAddress - The i2c address of the display
     * @param rawHeight - The vertical number of pixels for the display, defaults to 32
     * @param rawWidth - The horizonal number of pixels for the display, defaults to 128
     */
    Adafruit_SH1106_I2c(I2C &i2c, PinName RST, uint8_t i2cAddress = SSD_I2C_ADDRESS, uint8_t rawHeight = 64, uint8_t rawWidth = 128)
        : Adafruit_SSD1306(RST, rawHeight, rawWidth)
        , mi2c(i2c)
        , mi2cAddress(i2cAddress)
    {
        begin2();
        splash();
        display();
    };

    virtual void command(uint8_t c)
    {
        char buff[2];
        buff[0] = 0; // Command Mode
        buff[1] = c;
        mi2c.write(mi2cAddress, buff, sizeof(buff));
    }

    virtual void data(uint8_t c)
    {
        char buff[2];
        buff[0] = 0x40; // Data Mode
        buff[1] = c;
        mi2c.write(mi2cAddress, buff, sizeof(buff));
    };

protected:
    void begin2(void)
    {
        command(0xAE); //--display off
        command(0xA1); //--set segment re-map 127 to 0
        command(0xC8); //--Set COM Out Scan Dir 63 to 0
        command(0x32); //--set pump voltage value to 8.0V (SH1106 only)
        command(0x40); //--set start line address
        command(0x81); //--set contrast control register
        command(0x80); // POR value = 80
        command(0xA4); //--set normal mode (A5: test mode)
        command(0xA6); //--set normal display (i.e non-inverted mode)
        command(0xA8); //--set multiplex ratio(1 to 64)
        command(0x3F); //
        command(0xAD); //--set DC-DC mode
        command(0x8B); // DC-DC converter ON
        command(0xD3); //--set display offset
        command(0x00); // no offset
        command(0xD5); //--set display clock divide ratio/oscillator frequency
        command(0x50); // set frequency and divide ratio
        command(0xD9); //--set dis-charge/pre-charge period
        command(0x22); //
        command(0xDA); //--set com pins hardware configuration
        command(0x12);
        command(0xDB); //--set vcomh
        command(0x35); // SH1106: 0x35, SSD1306: 0x20 0.77xVcc
        command(0xAF); //--turn on SSD1306 panel
    };

    virtual void sendDisplayBuffer()
    {

        char buff[256];
        char cmd[4] = {0, 0xB0, 0x02, 0x10};
        for (uint8_t m = 0; m < _rawHeight/8; m++) {
            buff[0] = 0x40;
            cmd[1] = 0xB0 + m;
            for(int i=0; i<128; i++) {
                buff[i+1]= buffer[m*128+i];
            }
            mi2c.write(mi2cAddress, cmd, 4);
            mi2c.write(mi2cAddress, buff, 129);
        }
    };

    I2C &mi2c;
    uint8_t mi2cAddress;
};



#endif
