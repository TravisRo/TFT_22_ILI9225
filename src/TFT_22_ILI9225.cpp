#include "TFT_22_ILI9225.h"
#ifndef ARDUINO_STM32_FEATHER
    #include "pins_arduino.h"
    #ifndef RASPI
        #include "wiring_private.h"
    #endif
#endif
#include <limits.h>
#ifdef __AVR__
  #include <avr/pgmspace.h>
#elif defined(ESP8266) || defined(ESP32)
  #include <pgmspace.h>
#endif

// Many (but maybe not all) non-AVR board installs define macros
// for compatibility with existing PROGMEM-reading AVR code.
// Do our own checks and defines here for good measure...

#ifndef pgm_read_byte
 #define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#endif
#ifndef pgm_read_word
 #define pgm_read_word(addr) (*(const unsigned short *)(addr))
#endif
#ifndef pgm_read_dword
 #define pgm_read_dword(addr) (*(const unsigned long *)(addr))
#endif

// Pointers are a peculiar case...typically 16-bit on AVR boards,
// 32 bits elsewhere.  Try to accommodate both...

#if !defined(__INT_MAX__) || (__INT_MAX__ > 0xFFFF)
 #define pgm_read_pointer(addr) ((void *)pgm_read_dword(addr))
#else
 #define pgm_read_pointer(addr) ((void *)pgm_read_word(addr))
#endif

// Control pins

#ifdef USE_FAST_PINIO
    #define SPI_DC_HIGH()           *dcport |=  dcpinmask
    #define SPI_DC_LOW()            *dcport &= ~dcpinmask
    #define SPI_CS_HIGH()           *csport |=  cspinmask
    #define SPI_CS_LOW()            *csport &= ~cspinmask
#else
    #define SPI_DC_HIGH()           digitalWrite(_rs, HIGH)
    #define SPI_DC_LOW()            digitalWrite(_rs, LOW)
    #define SPI_CS_HIGH()           digitalWrite(_cs, HIGH)
    #define SPI_CS_LOW()            digitalWrite(_cs, LOW)
#endif

// Software SPI Macros

#ifdef USE_FAST_PINIO
    #define SSPI_MOSI_HIGH()        *mosiport |=  mosipinmask
    #define SSPI_MOSI_LOW()         *mosiport &= ~mosipinmask
    #define SSPI_SCK_HIGH()         *clkport |=  clkpinmask
    #define SSPI_SCK_LOW()          *clkport &= ~clkpinmask
#else
    #define SSPI_MOSI_HIGH()        digitalWrite(_sdi, HIGH)
    #define SSPI_MOSI_LOW()         digitalWrite(_sdi, LOW)
    #define SSPI_SCK_HIGH()         digitalWrite(_clk, HIGH)
    #define SSPI_SCK_LOW()          digitalWrite(_clk, LOW)
#endif

#define SSPI_BEGIN_TRANSACTION()
#define SSPI_END_TRANSACTION()
#define SSPI_WRITE(v)           _spiWrite(v)
#define SSPI_WRITE16(s)         SSPI_WRITE((s) >> 8); SSPI_WRITE(s)
#define SSPI_WRITE32(l)         SSPI_WRITE((l) >> 24); SSPI_WRITE((l) >> 16); SSPI_WRITE((l) >> 8); SSPI_WRITE(l)
#define SSPI_WRITE_PIXELS(c,l)  for(uint32_t i=0; i<(l); i+=2){ SSPI_WRITE(((uint8_t*)(c))[i+1]); SSPI_WRITE(((uint8_t*)(c))[i]); }

// Hardware SPI Macros
 
#ifndef ESP32
    #define SPI_OBJECT  SPI
#else
    #define SPI_OBJECT  _spi
#endif

#if defined (__AVR__) || defined(TEENSYDUINO) || defined(ARDUINO_ARCH_STM32F1)
    #define HSPI_SET_CLOCK() SPI_OBJECT.setClockDivider(SPI_CLOCK_DIV2);
#elif defined (__arm__)
    #define HSPI_SET_CLOCK() SPI_OBJECT.setClockDivider(11);
#elif defined(ESP8266) || defined(ESP32)
    #define HSPI_SET_CLOCK() SPI_OBJECT.setFrequency(SPI_DEFAULT_FREQ);
#elif defined(RASPI)
    #define HSPI_SET_CLOCK() SPI_OBJECT.setClock(SPI_DEFAULT_FREQ);
#elif defined(ARDUINO_ARCH_STM32F1)
    #define HSPI_SET_CLOCK() SPI_OBJECT.setClock(SPI_DEFAULT_FREQ);
#else
    #define HSPI_SET_CLOCK()
#endif

#ifdef SPI_HAS_TRANSACTION
    #define HSPI_BEGIN_TRANSACTION() SPI_OBJECT.beginTransaction(SPISettings(SPI_DEFAULT_FREQ, MSBFIRST, SPI_MODE0))
    #define HSPI_END_TRANSACTION()   SPI_OBJECT.endTransaction()
#else
    #define HSPI_BEGIN_TRANSACTION() HSPI_SET_CLOCK(); SPI_OBJECT.setBitOrder(MSBFIRST); SPI_OBJECT.setDataMode(SPI_MODE0)
    #define HSPI_END_TRANSACTION()
#endif

#ifdef ESP32
    #define SPI_HAS_WRITE_PIXELS
#endif
#if defined(ESP8266) || defined(ESP32)
    // Optimized SPI (ESP8266 and ESP32)
    #define HSPI_READ()              SPI_OBJECT.transfer(0)
    #define HSPI_WRITE(b)            SPI_OBJECT.write(b)
    #define HSPI_WRITE16(s)          SPI_OBJECT.write16(s)
    #define HSPI_WRITE32(l)          SPI_OBJECT.write32(l)
    #ifdef SPI_HAS_WRITE_PIXELS
        #define SPI_MAX_PIXELS_AT_ONCE  32
        #define HSPI_WRITE_PIXELS(c,l)   SPI_OBJECT.writePixels(c,l)
    #else
        #define HSPI_WRITE_PIXELS(c,l)   for(uint32_t i=0; i<((l)/2); i++){ SPI_WRITE16(((uint16_t*)(c))[i]); }
    #endif
#else
    // Standard Byte-by-Byte SPI

    #if defined (__AVR__) || defined(TEENSYDUINO)
    static inline uint8_t _avr_spi_read(void) __attribute__((always_inline));
    static inline uint8_t _avr_spi_read(void) {
        uint8_t r = 0;
        SPDR = r;
        while(!(SPSR & _BV(SPIF)));
        r = SPDR;
        return r;
    }
        #define HSPI_WRITE(b)        {SPDR = (b); while(!(SPSR & _BV(SPIF)));}
        // #define HSPI_READ()          _avr_spi_read()
    #else
        #define HSPI_WRITE(b)        SPI_OBJECT.transfer((uint8_t)(b))
        // #define HSPI_READ()          HSPI_WRITE(0)
    #endif
    // #define HSPI_WRITE16(s)          HSPI_WRITE((s) >> 8); HSPI_WRITE(s)
    // #define HSPI_WRITE32(l)          HSPI_WRITE((l) >> 24); HSPI_WRITE((l) >> 16); HSPI_WRITE((l) >> 8); HSPI_WRITE(l)
    // #define HSPI_WRITE_PIXELS(c,l)   for(uint32_t i=0; i<(l); i+=2){ HSPI_WRITE(((uint8_t*)(c))[i+1]); HSPI_WRITE(((uint8_t*)(c))[i]); }
#endif

// Final SPI Macros

#if defined (ARDUINO_ARCH_ARC32)
    #define SPI_DEFAULT_FREQ         16000000
#elif defined (__AVR__) || defined(TEENSYDUINO)
    #define SPI_DEFAULT_FREQ         8000000
#elif defined(ESP8266) || defined(ESP32)
    #define SPI_DEFAULT_FREQ         40000000
#elif defined(RASPI)
    #define SPI_DEFAULT_FREQ         80000000
#elif defined(ARDUINO_ARCH_STM32F1)
    #define SPI_DEFAULT_FREQ         36000000
#else
    #define SPI_DEFAULT_FREQ         24000000
#endif

#define SPI_BEGIN()             if(_clk < 0){SPI_OBJECT.begin();}
#define SPI_BEGIN_TRANSACTION() if(_clk < 0){HSPI_BEGIN_TRANSACTION();}
#define SPI_END_TRANSACTION()   if(_clk < 0){HSPI_END_TRANSACTION();}
// #define SPI_WRITE16(s)          if(_clk < 0){HSPI_WRITE16(s);}else{SSPI_WRITE16(s);}
// #define SPI_WRITE32(l)          if(_clk < 0){HSPI_WRITE32(l);}else{SSPI_WRITE32(l);}
// #define SPI_WRITE_PIXELS(c,l)   if(_clk < 0){HSPI_WRITE_PIXELS(c,l);}else{SSPI_WRITE_PIXELS(c,l);}

/// Helper macro for managing the cursor position
#define INC_CURSOR() do {             \
    cursor_x++;                       \
    if (cursor_x >= _windowX1 ) {     \
        cursor_x = _windowX0;         \
        cursor_y++;	                  \
        if (cursor_y >= _windowY1) {  \
            cursor_y = _windowY0;     \
        }                             \
    }                                 \
}while(0)

// Helper macros for defining the window area
#define SET_WINDOW_WH(_x,_y,_w,_h)	_setWindow(_x, _y, _x+_w-1, _y+_h-1)
#define SET_WINDOW_POS(_x0,_y0,_x1,_y1)	_setWindow(_x0, _y0, _x1, _y1)
#define SET_WINDOW_MAX() _setWindow(0, 0, _width - 1, _height - 1)

#define ENTRY_MODE_MASK      0x0038
#define ENTRY_MODE_VERTICAL  0x0008
#define ENTRY_MODE_HORIZ_INC 0x0010
#define ENTRY_MODE_VERT_INC  0x0020

#define toggleEntryMode(ENTRY_MODE_BITS) do {       \
    _entryMode ^= (ENTRY_MODE_BITS);                \
    _writeRegister(ILI9225_ENTRY_MODE, _entryMode); \
}while(0)

// Constructor when using software SPI.  All output pins are configurable.
TFT_22_ILI9225::TFT_22_ILI9225(int8_t rst, int8_t rs, int8_t cs, int8_t sdi, int8_t clk, int8_t led) {
    _rst  = rst;
    _rs   = rs;
    _cs   = cs;
    _sdi  = sdi;
    _clk  = clk;
    _led  = led;
    _brightness = 255; // Set to maximum brightness
    hwSPI = false;
    gfxFont = NULL;
}

// Constructor when using software SPI.  All output pins are configurable. Adds backlight brightness 0-255
TFT_22_ILI9225::TFT_22_ILI9225(int8_t rst, int8_t rs, int8_t cs, int8_t sdi, int8_t clk, int8_t led, uint8_t brightness) {
    _rst  = rst;
    _rs   = rs;
    _cs   = cs;
    _sdi  = sdi;
    _clk  = clk;
    _led  = led;
    _brightness = brightness;
    hwSPI = false;
    gfxFont = NULL;
}

// Constructor when using hardware SPI.  Faster, but must use SPI pins
// specific to each board type (e.g. 11,13 for Uno, 51,52 for Mega, etc.)
TFT_22_ILI9225::TFT_22_ILI9225(int8_t rst, int8_t rs, int8_t cs, int8_t led) {
    _rst  = rst;
    _rs   = rs;
    _cs   = cs;
    _sdi  = _clk = -1;
    _led  = led;
    _brightness = 255; // Set to maximum brightness
    hwSPI = true;
    writeRefCount = 0;
    gfxFont = NULL;
}

// Constructor when using hardware SPI.  Faster, but must use SPI pins
// specific to each board type (e.g. 11,13 for Uno, 51,52 for Mega, etc.)
// Adds backlight brightness 0-255
TFT_22_ILI9225::TFT_22_ILI9225(int8_t rst, int8_t rs, int8_t cs, int8_t led, uint8_t brightness) {
    _rst  = rst;
    _rs   = rs;
    _cs   = cs;
    _sdi  = _clk = -1;
    _led  = led;
    _brightness = brightness;
    hwSPI = true;
    gfxFont = NULL;
}


#ifdef ESP32
void TFT_22_ILI9225::begin(SPIClass &spi)
#else
void TFT_22_ILI9225::begin()
#endif
{
#ifdef ESP32
    _spi = spi;
#endif
    writeRefCount = 0;
    _entryMode = 0x1030;

    // Set up reset pin
    if (_rst > 0) {
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, LOW);
    }
    // Set up backlight pin, turn off initially
    if (_led > 0) {
        pinMode(_led, OUTPUT);
        setBacklight(false);
    }

    // Control pins
    pinMode(_rs, OUTPUT);
    digitalWrite(_rs, LOW);
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);

#ifdef USE_FAST_PINIO
    csport    = portOutputRegister(digitalPinToPort(_cs));
    cspinmask = digitalPinToBitMask(_cs);
    dcport    = portOutputRegister(digitalPinToPort(_rs));
    dcpinmask = digitalPinToBitMask(_rs);
#endif

    // Software SPI
    if(_clk >= 0){
        pinMode(_sdi, OUTPUT);
        digitalWrite(_sdi, LOW);
        pinMode(_clk, OUTPUT);
        digitalWrite(_clk, HIGH);
#ifdef USE_FAST_PINIO
        clkport     = portOutputRegister(digitalPinToPort(_clk));
        clkpinmask  = digitalPinToBitMask(_clk);
        mosiport    = portOutputRegister(digitalPinToPort(_sdi));
        mosipinmask = digitalPinToBitMask(_sdi);
        SSPI_SCK_LOW();
        SSPI_MOSI_LOW();
    } else {
        clkport     = 0;
        clkpinmask  = 0;
        mosiport    = 0;
        mosipinmask = 0;
#endif
    }

    // Hardware SPI
    SPI_BEGIN();

    // Initialization Code
    if (_rst > 0) {
        digitalWrite(_rst, HIGH); // Pull the reset pin high to release the ILI9225C from the reset status
        delay(1); 
        digitalWrite(_rst, LOW); // Pull the reset pin low to reset ILI9225
        delay(10);
        digitalWrite(_rst, HIGH); // Pull the reset pin high to release the ILI9225C from the reset status
        delay(50);
    }

    /* Start Initial Sequence */

    /* Set SS bit and direction output from S528 to S1 */
    startWrite();
    _writeRegister(ILI9225_POWER_CTRL1, 0x0000); // Set SAP,DSTB,STB
    _writeRegister(ILI9225_POWER_CTRL2, 0x0000); // Set APON,PON,AON,VCI1EN,VC
    _writeRegister(ILI9225_POWER_CTRL3, 0x0000); // Set BT,DC1,DC2,DC3
    _writeRegister(ILI9225_POWER_CTRL4, 0x0000); // Set GVDD
    _writeRegister(ILI9225_POWER_CTRL5, 0x0000); // Set VCOMH/VCOML voltage
    endWrite();
    delay(40); 

    // Power-on sequence
    startWrite();
    _writeRegister(ILI9225_POWER_CTRL2, 0x0018); // Set APON,PON,AON,VCI1EN,VC
    _writeRegister(ILI9225_POWER_CTRL3, 0x6121); // Set BT,DC1,DC2,DC3
    _writeRegister(ILI9225_POWER_CTRL4, 0x006F); // Set GVDD   /*007F 0088 */
    _writeRegister(ILI9225_POWER_CTRL5, 0x495F); // Set VCOMH/VCOML voltage
    _writeRegister(ILI9225_POWER_CTRL1, 0x0800); // Set SAP,DSTB,STB
    endWrite();
    delay(10);
    startWrite();
    _writeRegister(ILI9225_POWER_CTRL2, 0x103B); // Set APON,PON,AON,VCI1EN,VC
    endWrite();
    delay(50);

    startWrite();
    _writeRegister(ILI9225_DRIVER_OUTPUT_CTRL, 0x011C); // set the display line number and display direction
    _writeRegister(ILI9225_LCD_AC_DRIVING_CTRL, 0x0100); // set 1 line inversion
    _writeRegister(ILI9225_ENTRY_MODE, _entryMode); // set GRAM write direction and BGR=1.
    _writeRegister(ILI9225_DISP_CTRL1, 0x0000); // Display off
    _writeRegister(ILI9225_BLANK_PERIOD_CTRL1, 0x0808); // set the back porch and front porch
    _writeRegister(ILI9225_FRAME_CYCLE_CTRL, 0x1100); // set the clocks number per line
    _writeRegister(ILI9225_INTERFACE_CTRL, 0x0000); // CPU interface
    _writeRegister(ILI9225_OSC_CTRL, 0x0D01); // Set Osc  /*0e01*/
    _writeRegister(ILI9225_VCI_RECYCLING, 0x0020); // Set VCI recycling
    _writeRegister(ILI9225_RAM_ADDR_SET1, 0x0000); // RAM Address
    _writeRegister(ILI9225_RAM_ADDR_SET2, 0x0000); // RAM Address

    /* Set GRAM area */
    _writeRegister(ILI9225_GATE_SCAN_CTRL, 0x0000); 
    _writeRegister(ILI9225_VERTICAL_SCROLL_CTRL1, 0x00DB); 
    _writeRegister(ILI9225_VERTICAL_SCROLL_CTRL2, 0x0000); 
    _writeRegister(ILI9225_VERTICAL_SCROLL_CTRL3, 0x0000); 
    _writeRegister(ILI9225_PARTIAL_DRIVING_POS1, 0x00DB); 
    _writeRegister(ILI9225_PARTIAL_DRIVING_POS2, 0x0000); 
    _writeRegister(ILI9225_HORIZONTAL_WINDOW_ADDR1, 0x00AF); 
    _writeRegister(ILI9225_HORIZONTAL_WINDOW_ADDR2, 0x0000); 
    _writeRegister(ILI9225_VERTICAL_WINDOW_ADDR1, 0x00DB); 
    _writeRegister(ILI9225_VERTICAL_WINDOW_ADDR2, 0x0000); 

    /* Set GAMMA curve */
    _writeRegister(ILI9225_GAMMA_CTRL1, 0x0000); 
    _writeRegister(ILI9225_GAMMA_CTRL2, 0x0808); 
    _writeRegister(ILI9225_GAMMA_CTRL3, 0x080A); 
    _writeRegister(ILI9225_GAMMA_CTRL4, 0x000A); 
    _writeRegister(ILI9225_GAMMA_CTRL5, 0x0A08); 
    _writeRegister(ILI9225_GAMMA_CTRL6, 0x0808); 
    _writeRegister(ILI9225_GAMMA_CTRL7, 0x0000); 
    _writeRegister(ILI9225_GAMMA_CTRL8, 0x0A00); 
    _writeRegister(ILI9225_GAMMA_CTRL9, 0x0710); 
    _writeRegister(ILI9225_GAMMA_CTRL10, 0x0710); 

    _writeRegister(ILI9225_DISP_CTRL1, 0x0012); 
    endWrite();
    delay(50); 
    startWrite();
    _writeRegister(ILI9225_DISP_CTRL1, 0x1017);
    endWrite();

    // Turn on backlight
    setBacklight(true);
    setOrientation(0);

    // Initialize variables
    setBackgroundColor( COLOR_BLACK );

    clear();
}


void TFT_22_ILI9225::_spiWrite(uint8_t b) {
    if(_clk < 0){
        HSPI_WRITE(b);
        return;
    }
    // Fast SPI bitbang swiped from LPD8806 library
    for(uint8_t bit = 0x80; bit; bit >>= 1){
        if((b) & bit){
            SSPI_MOSI_HIGH();
        } else {
            SSPI_MOSI_LOW();
        }
        SSPI_SCK_HIGH();
        SSPI_SCK_LOW();
    }
}


void TFT_22_ILI9225::_spiWriteCommand(uint8_t c) {
    SPI_DC_LOW();
    SPI_CS_LOW();
    _spiWrite(c);
    SPI_CS_HIGH();
}


void TFT_22_ILI9225::_spiWriteData(uint8_t c) {
    SPI_DC_HIGH();
    SPI_CS_LOW();
    _spiWrite(c);
    SPI_CS_HIGH();
}


void TFT_22_ILI9225::_setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;

    if (x1 >= _width) x1 = _width - 1;
    if (y1 >= _height) y1 = _height - 1;
    if (x0 >= _width) x0 = _width - 1;
    if (y0 >= _height) y0 = _height - 1;

    if (cursor_x != x0 || cursor_y != y0 || x0 != _windowX0 || y0 != _windowY0 || x1  != _windowX1 || y1 != _windowY1) {
        _writeRegister(endH, x1);
        _writeRegister(startH, x0);
        _writeRegister(endV, y1);
        _writeRegister(startV, y0);

        if (_entryMode & ENTRY_MODE_HORIZ_INC)
            _writeRegister(ramAddrOne, x0);
        else
            _writeRegister(ramAddrOne, x1);

        if (_entryMode & ENTRY_MODE_VERT_INC)
            _writeRegister(ramAddrTwo, y0);
        else
            _writeRegister(ramAddrTwo, y1);
            
  
        cursor_x = x0;
        cursor_y = y0;
        _windowX0 = x0;
        _windowY0 = y0;
        _windowX1 = x1;
        _windowY1 = y1;
        _windowWidth = x1 - x0 + 1;
        _windowHeight = y1 - y0 + 1;
    }
    _writeCommand(0x00, ILI9225_GRAM_DATA_REG);
}


void TFT_22_ILI9225::clear(uint16_t fillColor) {
    fillRectangle(0, 0, _width - 1, _height - 1, fillColor);
}


void TFT_22_ILI9225::invert(boolean flag) {
    startWrite();
    _writeCommand(0x00, flag ? ILI9225C_INVON : ILI9225C_INVOFF);
    endWrite();
}


void TFT_22_ILI9225::setBacklight(boolean flag) {
    blState = flag;
#ifndef ESP32
    if (_led) analogWrite(_led, blState ? _brightness : 0);
#endif
}


void TFT_22_ILI9225::setBacklightBrightness(uint8_t brightness) {
    _brightness = brightness;
    setBacklight(blState);
}


void TFT_22_ILI9225::setDisplay(boolean flag) {
    if (flag) {
        startWrite();
        _writeRegister(0x00ff, 0x0000);
        _writeRegister(ILI9225_POWER_CTRL1, 0x0000);
        endWrite();
        delay(50);
        startWrite();
        _writeRegister(ILI9225_DISP_CTRL1, 0x1017);
        endWrite();
        delay(200);
    } else {
        startWrite();
        _writeRegister(0x00ff, 0x0000);
        _writeRegister(ILI9225_DISP_CTRL1, 0x0000);
        endWrite();
        delay(50);
        startWrite();
        _writeRegister(ILI9225_POWER_CTRL1, 0x0003);
        endWrite();
        delay(200);
    }
}


void TFT_22_ILI9225::setOrientation(uint8_t orientation, bool mirror ) {

    // [TR 2-22-2018] This guy figured out a very intuitive way to handle orientation.
    //  Thanks Paul!
    // https://github.com/PaulStoffregen/ILI9341_t3
    _orientation = orientation % 4;

    startWrite();

    switch (_orientation)
    {
    case 1:
        _writeRegister(ILI9225_DRIVER_OUTPUT_CTRL, mirror ? 0x021C : 0x001C);
        _entryMode = 0x1038; // default for this orientation
        startH = ILI9225_VERTICAL_WINDOW_ADDR2;
        endH = ILI9225_VERTICAL_WINDOW_ADDR1;
        startV = ILI9225_HORIZONTAL_WINDOW_ADDR2;
        endV = ILI9225_HORIZONTAL_WINDOW_ADDR1;
        ramAddrOne = ILI9225_RAM_ADDR_SET2;
        ramAddrTwo = ILI9225_RAM_ADDR_SET1;
        _width = ILI9225_LCD_HEIGHT;
        _height = ILI9225_LCD_WIDTH;
        break;

    case 2:
        _writeRegister(ILI9225_DRIVER_OUTPUT_CTRL, mirror ? 0x031C : 0x021C);
        _entryMode = 0x1030; // default for this orientation
        startH = ILI9225_HORIZONTAL_WINDOW_ADDR2;
        endH = ILI9225_HORIZONTAL_WINDOW_ADDR1;
        startV = ILI9225_VERTICAL_WINDOW_ADDR2;
        endV = ILI9225_VERTICAL_WINDOW_ADDR1;
        ramAddrOne = ILI9225_RAM_ADDR_SET1;
        ramAddrTwo = ILI9225_RAM_ADDR_SET2;
        _width = ILI9225_LCD_WIDTH;
        _height = ILI9225_LCD_HEIGHT;
        break;

    case 3:
        _writeRegister(ILI9225_DRIVER_OUTPUT_CTRL, mirror ? 0x011C : 0x031C);
        _entryMode = 0x1038; // default for this orientation
        startH = ILI9225_VERTICAL_WINDOW_ADDR2;
        endH = ILI9225_VERTICAL_WINDOW_ADDR1;
        startV = ILI9225_HORIZONTAL_WINDOW_ADDR2;
        endV = ILI9225_HORIZONTAL_WINDOW_ADDR1;
        ramAddrOne = ILI9225_RAM_ADDR_SET2;
        ramAddrTwo = ILI9225_RAM_ADDR_SET1;
        _width = ILI9225_LCD_HEIGHT;
        _height = ILI9225_LCD_WIDTH;
        break;

    default: // 0
        _writeRegister(ILI9225_DRIVER_OUTPUT_CTRL, mirror ? 0x001C : 0x011C);
        _entryMode = 0x1030; // default for this orientation
        startH = ILI9225_HORIZONTAL_WINDOW_ADDR2;
        endH = ILI9225_HORIZONTAL_WINDOW_ADDR1;
        startV = ILI9225_VERTICAL_WINDOW_ADDR2;
        endV = ILI9225_VERTICAL_WINDOW_ADDR1;
        ramAddrOne = ILI9225_RAM_ADDR_SET1;
        ramAddrTwo = ILI9225_RAM_ADDR_SET2;
        _width = ILI9225_LCD_WIDTH;
        _height = ILI9225_LCD_HEIGHT;
        break;
    }
    _writeRegister(ILI9225_ENTRY_MODE, _entryMode);
    SET_WINDOW_MAX();
    

    endWrite();
}


uint8_t TFT_22_ILI9225::getOrientation() {
    return _orientation;
}


void TFT_22_ILI9225::drawRectangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    startWrite();

    drawHLine(x1, x2, y1, color);	// TOP
    drawVLine(y1, y2, x1, color);	// LEFT
    drawHLine(x1, x2, y2, color);	// BOTTOM
    drawVLine(y1, y2, x2, color);	// RIGHT

    endWrite();
}


void TFT_22_ILI9225::fillRectangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    startWrite();

    SET_WINDOW_POS(x1, y1, x2, y2);

    SPI_DC_HIGH();
    SPI_CS_LOW();

    for (uint16_t t = _windowWidth * _windowHeight; t > 0; t--)
    {
        _spiWrite(color >> 8);
        _spiWrite(color);
        INC_CURSOR();
    }
    SPI_CS_HIGH();

    endWrite();
}


void TFT_22_ILI9225::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    startWrite();
    SET_WINDOW_MAX();
    _drawPixel(x0, y0 + r, color);
    _drawPixel(x0, y0 - r, color);
    _drawPixel(x0 + r, y0, color);
    _drawPixel(x0 - r, y0, color);

    while (x<y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        _drawPixel(x0 + x, y0 + y, color);
        _drawPixel(x0 - x, y0 + y, color);
        _drawPixel(x0 + x, y0 - y, color);
        _drawPixel(x0 - x, y0 - y, color);
        _drawPixel(x0 + y, y0 + x, color);
        _drawPixel(x0 - y, y0 + x, color);
        _drawPixel(x0 + y, y0 - x, color);
        _drawPixel(x0 - y, y0 - x, color);
    }
    endWrite();
}


void TFT_22_ILI9225::fillCircle(int16_t x0, int16_t y0, int16_t radius, uint16_t color) {
    int16_t f = 1 - radius;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * radius;
    int16_t x = 0;
    int16_t y = radius;

    startWrite();
    while (x<y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        drawLine(x0 + x, y0 + y, x0 - x, y0 + y, color); // bottom
        drawLine(x0 + x, y0 - y, x0 - x, y0 - y, color); // top
        drawLine(x0 + y, y0 - x, x0 + y, y0 + x, color); // right
        drawLine(x0 - y, y0 - x, x0 - y, y0 + x, color); // left
    }
    fillRectangle(x0 - x, y0 - y, x0 + x, y0 + y, color);
    endWrite();
}


void TFT_22_ILI9225::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {

    // Classic Bresenham algorithm
    int16_t steep = abs((int16_t)(y2 - y1)) > abs((int16_t)(x2 - x1));

    int16_t dx, dy;

    if (steep) {
        _swap(x1, y1);
        _swap(x2, y2);
    }

    if (x1 > x2) {
        _swap(x1, x2);
        _swap(y1, y2);
    }

    dx = x2 - x1;
    dy = abs((int16_t)(y2 - y1));

    int16_t err = dx / 2;
    int16_t ystep;

    if (y1 < y2) ystep = 1;
    else ystep = -1;

    startWrite();

    SET_WINDOW_MAX();

    for (; x1<=x2; x1++) {
        if (steep) _drawPixel(y1, x1, color);
        else       _drawPixel(x1, y1, color);

        err -= dy;
        if (err < 0) {
            y1 += ystep;
            err += dx;
        }
    }

    endWrite();
}


void TFT_22_ILI9225::drawPixel(int16_t x1, int16_t y1, uint16_t color) {
 
    if (x1 < _windowX0 || y1 < _windowY0 || x1 > _windowX1 || y1 > _windowY1) return;

    startWrite();
    SET_WINDOW_MAX();
    _drawPixel(x1, y1, color);
    endWrite();
}


uint16_t TFT_22_ILI9225::maxX() {
    return _width;
}


uint16_t TFT_22_ILI9225::maxY() {
    return _height;
}


uint16_t TFT_22_ILI9225::setColor(uint8_t red8, uint8_t green8, uint8_t blue8) {
    // rgb16 = red5 green6 blue5
    return (red8 >> 3) << 11 | (green8 >> 2) << 5 | (blue8 >> 3);
}


void TFT_22_ILI9225::splitColor(uint16_t rgb, uint8_t &red, uint8_t &green, uint8_t &blue) {
    // rgb16 = red5 green6 blue5
    red   = (rgb & 0b1111100000000000) >> 11 << 3;
    green = (rgb & 0b0000011111100000) >>  5 << 2;
    blue  = (rgb & 0b0000000000011111)       << 3;
}


void TFT_22_ILI9225::_swap(int16_t &a, int16_t &b) {
    int16_t w = a;
    a = b;
    b = w;
}


// Utilities
void TFT_22_ILI9225::_writeCommand(uint8_t HI, uint8_t LO) {
    _spiWriteCommand(HI);
    _spiWriteCommand(LO);
}


void TFT_22_ILI9225::_writeData(uint8_t HI, uint8_t LO) {
    _spiWriteData(HI);
    _spiWriteData(LO);
}


void TFT_22_ILI9225::_writeRegister(uint16_t reg, uint16_t data) {
    _writeCommand(reg >> 8, reg & 255);
    _writeData(data >> 8, data & 255);
}


void TFT_22_ILI9225::drawTriangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, uint16_t color) {
    startWrite();
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x3, y3, color);
    drawLine(x3, y3, x1, y1, color);
    endWrite();
}


void TFT_22_ILI9225::fillTriangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, uint16_t color) {
    int16_t a, b, y, last;

    // Sort coordinates by Y order (y3 >= y2 >= y1)
    if (y1 > y2) {
        _swap(y1, y2); _swap(x1, x2);
    }
    if (y2 > y3) {
        _swap(y3, y2); _swap(x3, x2);
    }
    if (y1 > y2) {
        _swap(y1, y2); _swap(x1, x2);
    }

    startWrite();
    if (y1 == y3) { // Handle awkward all-on-same-line case as its own thing
        a = b = x1;
        if (x2 < a)      a = x2;
        else if (x2 > b) b = x2;
        if (x3 < a)      a = x3;
        else if (x3 > b) b = x3;
            drawLine(a, y1, b, y1, color);
        endWrite();
        return;
    }

    int16_t dx11 = x2 - x1,
            dy11 = y2 - y1,
            dx12 = x3 - x1,
            dy12 = y3 - y1,
            dx22 = x3 - x2,
            dy22 = y3 - y2;
    int32_t sa   = 0,
            sb   = 0;

    // For upper part of triangle, find scanline crossings for segments
    // 0-1 and 0-2.  If y2=y3 (flat-bottomed triangle), the scanline y2
    // is included here (and second loop will be skipped, avoiding a /0
    // error there), otherwise scanline y2 is skipped here and handled
    // in the second loop...which also avoids a /0 error here if y1=y2
    // (flat-topped triangle).
    if (y2 == y3) last = y2;   // Include y2 scanline
    else          last = y2 - 1; // Skip it

    for (y = y1; y <= last; y++) {
        a   = x1 + sa / dy11;
        b   = x1 + sb / dy12;
        sa += dx11;
        sb += dx12;
        /* longhand:
        a = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        b = x1 + (x3 - x1) * (y - y1) / (y3 - y1);
        */
        if (a > b) _swap(a,b);
        drawLine(a, y, b, y, color);
    }

    // For lower part of triangle, find scanline crossings for segments
    // 0-2 and 1-2.  This loop is skipped if y2=y3.
    sa = dx22 * (y - y2);
    sb = dx12 * (y - y1);
    for (; y<=y3; y++) {
        a   = x2 + sa / dy22;
        b   = x1 + sb / dy12;
        sa += dx22;
        sb += dx12;
        /* longhand:
        a = x2 + (x3 - x2) * (y - y2) / (y3 - y2);
        b = x1 + (x3 - x1) * (y - y1) / (y3 - y1);
        */
        if (a > b) _swap(a,b);
            drawLine(a, y, b, y, color);
    }
    endWrite();
}


void TFT_22_ILI9225::setBackgroundColor(uint16_t color) {
    _bgColor = color;
}


void TFT_22_ILI9225::setFont(uint8_t* font) {

    cfont.font     = font;
    cfont.width    = readFontByte(0);
    cfont.height   = readFontByte(1);
    cfont.offset   = readFontByte(2);
    cfont.numchars = readFontByte(3);
    cfont.nbrows   = cfont.height / 8;

    if (cfont.height % 8) cfont.nbrows++;  // Set number of bytes used by height of font in multiples of 8
}

#if TFT_USE_STRING_CLASS == 1
void TFT_22_ILI9225::drawText(int16_t x, int16_t y, String s, uint16_t color) {

    int16_t currx = x;

    startWrite();
    // Print every character in string
    for (uint8_t k = 0; k < s.length(); k++) {
        currx += drawChar(currx, y, s.charAt(k), color);
    }
    endWrite();
}
#endif

int16_t TFT_22_ILI9225::drawChar(int16_t x, int16_t y, uint8_t ch, uint16_t color) {
    uint8_t charData, charWidth;
    uint8_t h, i, j;
    int16_t charOffset;

    charOffset = (cfont.width * cfont.nbrows) + 1; // bytes used by each character
    charOffset = (charOffset * (ch - cfont.offset)) + FONT_HEADER_SIZE; // char offset (add 4 for font header)
    charWidth = readFontByte(charOffset); // get font width from 1st byte
    charOffset++; // increment pointer to first character data byte

    startWrite();
    toggleEntryMode(ENTRY_MODE_VERTICAL);
    SET_WINDOW_WH(x, y, charWidth+1, cfont.height);
    SPI_DC_HIGH();
    SPI_CS_LOW();

    for (i = 0; i <= charWidth; i++) { // each font "column" (+1 blank column for spacing)
        if (x + i > _windowX1) break; // No need to process excess bits

        h = 0; // keep track of char height
        for (j = 0; j < cfont.nbrows; j++) { // each column byte
            if (i == charWidth) charData = (uint8_t)0x0; // Insert blank column
            else charData = readFontByte(charOffset);
            charOffset++;

            // Process every row in font character
            for (uint8_t k = 0; k < 8; k++) {
                if (h >= _windowHeight) break; // No need to process excess bits
                if (bitRead(charData, k)) {
                    _spiWrite(color >> 8);
                    _spiWrite(color);
                }
                else {
                    _spiWrite(_bgColor >> 8);
                    _spiWrite(_bgColor);
                }

                h++;
            };
        };
    };
    toggleEntryMode(ENTRY_MODE_VERTICAL);
    endWrite();

    return _windowX1 - _windowX0 + 1;
}

int16_t TFT_22_ILI9225::drawVertChar(int16_t x, int16_t y, uint8_t ch, uint16_t color, uint8_t direction) {
    uint8_t charData, charWidth;
    uint8_t h, i, j;
    int16_t charOffset;

    charOffset = (cfont.width * cfont.nbrows) + 1; // bytes used by each character
    charOffset = (charOffset * (ch - cfont.offset)) + FONT_HEADER_SIZE; // char offset (add 4 for font header)
    charWidth = readFontByte(charOffset); // get font width from 1st byte
    charOffset++; // increment pointer to first character data byte

    startWrite();
    toggleEntryMode((direction==1?ENTRY_MODE_VERT_INC:ENTRY_MODE_HORIZ_INC));
    SET_WINDOW_WH(x, y, cfont.height, (charWidth+1));
    SPI_DC_HIGH();
    SPI_CS_LOW();

    for (i = 0; i <= charWidth; i++) { // each font "column" (+1 blank column for spacing)
        if (y + i > _windowY1) break; // No need to process excess bits

        h = 0; // keep track of char height
        for (j = 0; j < cfont.nbrows; j++) { // each column byte
            if (i == charWidth) charData = (uint8_t)0x0; // Insert blank column
            else charData = readFontByte(charOffset);
            charOffset++;

            // Process every row in font character
            for (uint8_t k = 0; k < 8; k++) {
                if (h >= _windowWidth) break; // No need to process excess bits
                if (bitRead(charData, k)) {
                    _spiWrite(color >> 8);
                    _spiWrite(color);
                }
                else {
                    _spiWrite(_bgColor >> 8);
                    _spiWrite(_bgColor);
                }

                h++;
            };
        };
    };
    toggleEntryMode((direction==1?ENTRY_MODE_VERT_INC:ENTRY_MODE_HORIZ_INC));
    endWrite();

    return _windowY1 - _windowY0 + 1;
}

// Draw a 1-bit image (bitmap) at the specified (x,y) position from the
// provided bitmap buffer (must be PROGMEM memory) using the specified
// foreground color (unset bits are transparent).
void TFT_22_ILI9225::drawBitmap(int16_t x, int16_t y,
const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color) {

    int16_t i, j, byteWidth = (w + 7) / 8;
    uint8_t byte = 0;

    startWrite();
    SET_WINDOW_WH(x, y, w, h);

    for (j = 0; j < _windowHeight; j++) {
        for (i = 0; i < _windowWidth; i++) {
            if (i & 7) byte <<= 1;
            else       byte   = pgm_read_byte(bitmap + j * byteWidth + i / 8);
            if (byte & 0x80) _drawPixel(x + i, y + j, color);
        }
    }

    endWrite();
}

// Draw a 1-bit image (bitmap) at the specified (x,y) position from the
// provided bitmap buffer (must be PROGMEM memory) using the specified
// foreground (for set bits) and background (for clear bits) colors.
void TFT_22_ILI9225::drawBitmap(int16_t x, int16_t y,
const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color, uint16_t bg) {

    int16_t i, j, byteWidth = (w + 7) / 8;
    uint8_t byte;

    startWrite();
    SET_WINDOW_WH(x, y, w, h);
    SPI_DC_HIGH();
    SPI_CS_LOW();

    for (j = 0; j < _windowHeight; j++) {
        for (i = 0; i < _windowWidth; i++) {
            if (i & 7) byte <<= 1;
            else       byte   = pgm_read_byte(bitmap + j * byteWidth + i / 8);
            if (byte & 0x80) {
                _spiWrite(color >> 8);
                _spiWrite(color);
            } else {
                _spiWrite(bg >> 8);
                _spiWrite(bg);
            }
        }
    }

    endWrite();
}


// drawBitmap() variant for RAM-resident (not PROGMEM) bitmaps.
void TFT_22_ILI9225::drawBitmap(int16_t x, int16_t y,
uint8_t *bitmap, int16_t w, int16_t h, uint16_t color) {

    int16_t i, j, byteWidth = (w + 7) / 8;
    uint8_t byte;

    startWrite();
    SET_WINDOW_WH(x, y, w, h);

    for (j = 0; j < _windowHeight; j++) {
        for (i = 0; i < _windowWidth; i++) {
            if (i & 7) byte <<= 1;
            else       byte = bitmap[j * byteWidth + i / 8];
            if (byte & 0x80) _drawPixel(x + i, y + j, color);
        }
    }

    endWrite();
}


// drawBitmap() variant w/background for RAM-resident (not PROGMEM) bitmaps.
void TFT_22_ILI9225::drawBitmap(int16_t x, int16_t y,
uint8_t *bitmap, int16_t w, int16_t h, uint16_t color, uint16_t bg) {

    int16_t i, j, byteWidth = (w + 7) / 8;
    uint8_t byte;

    startWrite();
    SET_WINDOW_WH(x, y, w, h);

    for (j = 0; j < _windowHeight; j++) {
        for (i = 0; i < _windowWidth; i++) {
            if (i & 7) byte <<= 1;
            else       byte = bitmap[j * byteWidth + i / 8];
            if (byte & 0x80) {
                _spiWrite(color >> 8);
                _spiWrite(color);
            } else {
                _spiWrite(bg >> 8);
                _spiWrite(bg);
            }

        }
    }

    endWrite();
}


//Draw XBitMap Files (*.xbm), exported from GIMP,
//Usage: Export from GIMP to *.xbm, rename *.xbm to *.c and open in editor.
//C Array can be directly used with this function
void TFT_22_ILI9225::drawXBitmap(int16_t x, int16_t y,
const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color) {

    int16_t i, j, byteWidth = (w + 7) / 8;
    uint8_t byte;

    startWrite();
    SET_WINDOW_WH(x, y, w, h);

    for (j = 0; j < _windowHeight; j++) {
        for (i = 0; i < _windowWidth; i++) {
            if (i & 7) byte >>= 1;
            else       byte   = pgm_read_byte(bitmap + j * byteWidth + i / 8);
            if (byte & 0x01) _drawPixel(x + i, y, color);
        }
    }
    endWrite();
}


void TFT_22_ILI9225::startWrite(void) {
    if (++writeRefCount != 1) return;

    SPI_BEGIN_TRANSACTION();
    SPI_CS_LOW();
}


void TFT_22_ILI9225::endWrite(void) {
    if (--writeRefCount != 0) return;
    SPI_CS_HIGH();
    SPI_END_TRANSACTION();
}


// TEXT- AND CHARACTER-HANDLING FUNCTIONS ----------------------------------

void TFT_22_ILI9225::setGFXFont(const GFXfont *f) {
    gfxFont = (GFXfont *)f;
}

#if TFT_USE_STRING_CLASS == 1
// Draw a string
void TFT_22_ILI9225::drawGFXText(int16_t x, int16_t y, String s, uint16_t color) {

    int16_t currx = x;

    if(gfxFont) {
        startWrite();
        // Print every character in string
        for (uint8_t k = 0; k < s.length(); k++) {
            currx += drawGFXChar(currx, y, s.charAt(k), color) + 1;
        }
        endWrite();
    }
}
#endif

// Draw a character
int16_t TFT_22_ILI9225::drawGFXChar(int16_t x, int16_t y, unsigned char c, uint16_t color) {

    c -= (uint8_t)pgm_read_byte(&gfxFont->first);
    GFXglyph *glyph  = &(((GFXglyph *)pgm_read_pointer(&gfxFont->glyph))[c]);
    uint8_t  *bitmap = (uint8_t *)pgm_read_pointer(&gfxFont->bitmap);

    uint16_t bo = pgm_read_word(&glyph->bitmapOffset);
    uint8_t  w  = pgm_read_byte(&glyph->width),
             h  = pgm_read_byte(&glyph->height),
             xa = pgm_read_byte(&glyph->xAdvance);
    int8_t   xo = pgm_read_byte(&glyph->xOffset),
             yo = pgm_read_byte(&glyph->yOffset);
    uint8_t  xx, yy, bits = 0, bit = 0;

    // NOTE: Char is clipped to the available window area

    startWrite();
    SET_WINDOW_WH(x + xo, y + yo, w, h);
    for (yy=0; yy<h; yy++) {
        for (xx=0; xx<w; xx++) {
            if(!(bit++ & 7)) { 
                bits = pgm_read_byte(&bitmap[bo++]);
            }
            if (bits & 0x80) {
                _drawPixel(x + xo + xx, y + yo + yy, color);
            }
            bits <<= 1;
        }
    }
    endWrite();

    return (uint16_t)xa;
}


void TFT_22_ILI9225::getGFXCharExtent(uint8_t c, int16_t *gw, int16_t *gh, int16_t *xa) {
    uint8_t first = pgm_read_byte(&gfxFont->first),
            last  = pgm_read_byte(&gfxFont->last);
    // Char present in this font?
    if((c >= first) && (c <= last)) {
        GFXglyph *glyph = &(((GFXglyph *)pgm_read_pointer(&gfxFont->glyph))[c - first]);
        *gw = pgm_read_byte(&glyph->width);
        *gh = pgm_read_byte(&glyph->height);
        *xa = pgm_read_byte(&glyph->xAdvance);
        // int8_t  xo = pgm_read_byte(&glyph->xOffset),
        //         yo = pgm_read_byte(&glyph->yOffset);
    }
}


void TFT_22_ILI9225::scroll(uint8_t start,uint8_t end, uint8_t amt) {
    startWrite();
    SET_WINDOW_MAX();
    _writeRegister(ILI9225_VERTICAL_SCROLL_CTRL1,end);
    _writeRegister(ILI9225_VERTICAL_SCROLL_CTRL2,start);
    _writeRegister(ILI9225_VERTICAL_SCROLL_CTRL3,amt);
    endWrite();
}


#if TFT_USE_STRING_CLASS == 1
void TFT_22_ILI9225::getGFXTextExtent(String str, int16_t *w, int16_t *h) {
    *w  = *h = 0;
    for (uint8_t k = 0; k < str.length(); k++) {
        uint8_t c = str.charAt(k);
        int16_t gw, gh, xa;
        getGFXCharExtent(c, &gw, &gh, &xa);
        if(gh > *h) {
            *h = gh;
        }
        *w += xa;
    }
}
#endif

void TFT_22_ILI9225::_setCursor(int16_t x0, int16_t y0) {
    if (cursor_x != x0 || cursor_y != y0)
    {
        _writeRegister(ramAddrOne, x0);
        _writeRegister(ramAddrTwo, y0);
        cursor_x = x0;
        cursor_y = y0;
        _writeCommand(0x00, ILI9225_GRAM_DATA_REG);
    }
}


void TFT_22_ILI9225::_drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x > _windowX1 || y > _windowY1) return;

    _setCursor(x, y);
    SPI_DC_HIGH();
    SPI_CS_LOW();
    _spiWrite(color >> 8);
    _spiWrite(color);
    SPI_CS_HIGH();

    INC_CURSOR();
}


void TFT_22_ILI9225::drawHLine(int16_t x1, int16_t x2, int16_t y1, uint16_t color) {
    // //horizontal line
    if (x1 > x2) _swap(x1, x2);
    y1 = abs(y1);

    startWrite();

    SET_WINDOW_POS(x1, y1, x2, y1);

    SPI_DC_HIGH();
    SPI_CS_LOW();
    x1 = _windowWidth;
    while (--x1 >= 0) {
        _spiWrite(color >> 8);
        _spiWrite(color);
        INC_CURSOR();
    }
    SPI_CS_HIGH();

    endWrite();
}


void TFT_22_ILI9225::drawVLine(int16_t y1, int16_t y2, int16_t x1, uint16_t color) {
    // vertical line
    if (y1 > y2) _swap(y1, y2);
    x1 = abs(x1);
    
    startWrite();

    SET_WINDOW_POS(x1, y1, x1, y2);

    SPI_DC_HIGH();
    SPI_CS_LOW();
    y1 = _windowHeight;
    while (--y1 >= 0) {
        _spiWrite(color >> 8);
        _spiWrite(color);
        INC_CURSOR();
    }
    SPI_CS_HIGH();

    endWrite();
}

#if TFT_USE_STRING_CLASS == 0
int16_t TFT_22_ILI9225::drawText(int16_t x, int16_t y, char *pStr, uint16_t color, uint8_t strLen) {

    int16_t textWidth = 0;

    startWrite();
    // Print every character in string
    for (uint8_t k = 0; k < strLen; k++) {
        uint8_t c = pStr[k];
        if (c == 0) break;
        if (textWidth + x >=_width) break;
        if (textWidth + x + fontX() < 0) {
            x+= fontX() + 1;
            continue;
        }
        textWidth+=drawChar(textWidth+x, y, pStr[k], color);
    }
    endWrite();
    return textWidth;
}


int16_t TFT_22_ILI9225::drawText(int16_t x, int16_t y, const char *pStr, uint16_t color, uint8_t strLen) {

    int16_t textWidth = 0;

    startWrite();
    // Print every character in string
    for (uint8_t k = 0; k < strLen; k++) {
        uint8_t c = pgm_read_byte(pStr + k);
        if (c == 0) break;
        if (textWidth + x >=_width) break;
        if (textWidth + x + fontX() < 0) {
            x+= fontX() + 1;
            continue;
        }
        textWidth+=drawChar(textWidth+x, y, pStr[k], color);
    }
    endWrite();
    return textWidth;
}


int16_t TFT_22_ILI9225::drawGFXText(int16_t x, int16_t y, char *pStr, uint16_t color, uint8_t maxChars) {
    int16_t currx = x;

    if (gfxFont) {
        startWrite();
        // Print every character in string
        for (uint8_t k = 0; pStr[k]!='\0' && k < maxChars; k++) {
            currx += drawGFXChar(currx, y, pStr[k], color) + 1;
        }
        endWrite();
    }

   return currx-x;
}

int16_t TFT_22_ILI9225::drawGFXText(int16_t x, int16_t y, const char *pStr, uint16_t color, uint8_t maxChars) {
    int16_t currx = x;

    if (gfxFont) {
        startWrite();
        // Print every character in string
        for (uint8_t k = 0; k < maxChars; k++) {
            uint8_t c = pgm_read_byte(pStr + k);
            if (c == 0) break;
            currx += drawGFXChar(currx, y, c, color) + 1;
        }
        endWrite();
    }

   return currx-x;
}


void TFT_22_ILI9225::getGFXTextExtent(char *pStr, int16_t *w, int16_t *h, uint8_t strLen) {
    *w = *h = 0;
    for (uint8_t k = 0; k < strLen && pStr[k]!='\0'; k++) {
        int16_t gw, gh, xa;
        getGFXCharExtent(pStr[k], &gw, &gh, &xa);
        if (gh > *h) { *h = gh; }
        *w += xa;
    }
}


void TFT_22_ILI9225::getGFXTextExtent(const char *pStr, int16_t *w, int16_t *h, uint8_t strLen) {
    *w = *h = 0;
    for (uint8_t k = 0; k < strLen; k++) {
        uint8_t c = pgm_read_byte(pStr+k);
        int16_t gw, gh, xa;
        if (c == 0) break;
        getGFXCharExtent(c, &gw, &gh, &xa);
        if (gh > *h) { *h = gh; }
        *w += xa;
    }
}
#endif