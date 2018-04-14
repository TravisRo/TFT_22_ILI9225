#ifndef _WEAVER_FONT_H_
#define _WEAVER_FONT_H_

typedef struct {
    // Bitmap table start index of char pixel data.
	// (bit count = width * height)
    uint16_t bitmapOffset;
    
    // The width and height of the smallest rectangle that completely encloses
    // the glyph (its black box).
    uint8_t  width, height;
    
    // The horizontal distance from the origin of the current character cell
    // to the origin of the next character cell.
    uint8_t  xAdvance;
    
    // The x and y-offsets from the users origin point to the top-left of the.
    // box. (start of draw offset)
    int8_t   xOffset, yOffset;
    
} WeaverGlyph;

typedef struct {
    
    // Pointer to bitmap table.
    uint8_t     *bitmap;
    
    // Array of glyph defintions.
    WeaverGlyph *glyph;
    
    // The first and last char codes in the array of glyphs
    // (start and end chars)
    uint8_t     first, last;

    // Newline distance (y axis)
    uint8_t     yAdvance;
} WeaverFont;

#endif // _WEAVER_FONT_H_
