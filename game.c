/* simple catcher game for the GBA*/
#include "background.h"
#include "map.h"
#include "map2.h"
#include "bowl2.h"

/* the width and height of the screen */
#define WIDTH 240
#define HEIGHT 160

/* these identifiers define different bit positions of the display control */
#define MODE4 0x0004
#define BG2 0x0400

/* the three tile modes */
#define MODE0 0x00
#define MODE1 0x01
#define MODE2 0x02

/* enable bits for the four tile layers */
#define BG0_ENABLE 0x100
#define BG1_ENABLE 0x200
#define BG2_ENABLE 0x400
#define BG3_ENABLE 0x800

/* the control registers for the four tile layers */
volatile unsigned short* bg0_control = (volatile unsigned short*) 0x4000008;
volatile unsigned short* bg1_control = (volatile unsigned short*) 0x400000a;
volatile unsigned short* bg2_control = (volatile unsigned short*) 0x400000c;
volatile unsigned short* bg3_control = (volatile unsigned short*) 0x400000e;

/* palette is always 256 colors */
#define PALETTE_SIZE 256


/* this bit indicates whether to display the front or the back buffer
 * this allows us to refer to bit 4 of the display_control register */
#define SHOW_BACK 0x10

/* the screen is simply a pointer into memory at a specific address this
 *  * pointer points to 16-bit colors of which there are 240x160 */
volatile unsigned short* screen = (volatile unsigned short*) 0x6000000;

/* the display control pointer points to the gba graphics register */
volatile unsigned long* display_control = (volatile unsigned long*) 0x4000000;

/* the address of the color palette used in graphics mode 4 */
volatile unsigned short* palette = (volatile unsigned short*) 0x5000000;

/* pointers to the front and back buffers - the front buffer is the start
 * of the screen array and the back buffer is a pointer to the second half */
volatile unsigned short* front_buffer = (volatile unsigned short*) 0x6000000;
volatile unsigned short* back_buffer = (volatile unsigned short*)  0x600A000;

/* the button register holds the bits which indicate whether each button has
 * been pressed - this has got to be volatile as well
 */
volatile unsigned short* buttons = (volatile unsigned short*) 0x04000130;

/* scrolling registers for backgrounds */
volatile short* bg0_x_scroll = (unsigned short*) 0x4000010;
volatile short* bg0_y_scroll = (unsigned short*) 0x4000012;
volatile short* bg1_x_scroll = (unsigned short*) 0x4000014;
volatile short* bg1_y_scroll = (unsigned short*) 0x4000016;
volatile short* bg2_x_scroll = (unsigned short*) 0x4000018;
volatile short* bg2_y_scroll = (unsigned short*) 0x400001a;
volatile short* bg3_x_scroll = (unsigned short*) 0x400001c;
volatile short* bg3_y_scroll = (unsigned short*) 0x400001e;

/* the bit positions indicate each button - the first bit is for A, second for
 * B, and so on, each constant below can be ANDED into the register to get the
 * status of any one button */
#define BUTTON_A (1 << 0)
#define BUTTON_B (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START (1 << 3)
#define BUTTON_RIGHT (1 << 4)
#define BUTTON_LEFT (1 << 5)
#define BUTTON_UP (1 << 6)
#define BUTTON_DOWN (1 << 7)
#define BUTTON_R (1 << 8)
#define BUTTON_L (1 << 9)

/* the scanline counter is a memory cell which is updated to indicate how
 * much of the screen has been drawn */
volatile unsigned short* scanline_counter = (volatile unsigned short*) 0x4000006;

/* wait for the screen to be fully drawn so we can do something during vblank */
void wait_vblank() {
    /* wait until all 160 lines have been updated */
    while (*scanline_counter < 160) { }
}

/* this function checks whether a particular button has been pressed */
unsigned char button_pressed(unsigned short button) {
    /* and the button register with the button constant we want */
    unsigned short pressed = *buttons & button;

    /* if this value is zero, then it's not pressed */
    if (pressed == 0) {
        return 1;
    } else {
        return 0;
    }
}

/* keep track of the next palette index */
int next_palette_index = 0;

/*
 * function which adds a color to the palette and returns the
 * index to it
 */
unsigned char add_color(unsigned char r, unsigned char g, unsigned char b) {
    unsigned short color = b << 10;
    color += g << 5;
    color += r;

    /* add the color to the palette */
    palette[next_palette_index] = color;

    /* increment the index */
    next_palette_index++;

    /* return index of color just added */
    return next_palette_index - 1;
}

/* a colored square */
struct square {
    unsigned short x, y, size;
    unsigned char color;
};

/* put a pixel on the screen in mode 4 */
void put_pixel(volatile unsigned short* buffer, int row, int col, unsigned char color) {
    /* find the offset which is the regular offset divided by two */
    unsigned short offset = (row * WIDTH + col) >> 1;

    /* read the existing pixel which is there */
    unsigned short pixel = buffer[offset];

    /* if it's an odd column */
    if (col & 1) {
        /* put it in the left half of the short */
        buffer[offset] = (color << 8) | (pixel & 0x00ff);
    } else {
        /* it's even, put it in the left half */
        buffer[offset] = (pixel & 0xff00) | color;
    }
}

/* draw a square onto the screen */
void draw_square(volatile unsigned short* buffer, struct square* s) {
    short row, col;
    /* for each row of the square */
    for (row = s->y; row < (s->y + s->size); row++) {
        /* loop through each column of the square */
        for (col = s->x; col < (s->x + s->size); col++) {
            /* set the screen location to this color */
            put_pixel(buffer, row, col, s->color);
        }
    }
}

/* clear the screen right around the square */
void update_screen(volatile unsigned short* buffer, unsigned short color, struct square* s) {
    short row, col;
    for (row = s->y - 3; row < (s->y + s->size + 3); row++) {
        for (col = s->x - 3; col < (s->x + s->size + 3); col++) {
            put_pixel(buffer, row, col, color);
        }
    }
}

/* this function takes a video buffer and returns to you the other one */
volatile unsigned short* flip_buffers(volatile unsigned short* buffer) {
    /* if the back buffer is up, return that */
    if(buffer == front_buffer) {
        /* clear back buffer bit and return back buffer pointer */
        *display_control &= ~SHOW_BACK;
        return back_buffer;
    } else {
        /* set back buffer bit and return front buffer */
        *display_control |= SHOW_BACK;
        return front_buffer;
    }
}

/* handle the buttons which are pressed down */
void handle_buttons(struct square* s) {
    /* move the square with the arrow keys */
    if (button_pressed(BUTTON_DOWN)) {
        s->y += 1;
    }
    if (button_pressed(BUTTON_UP)) {
        s->y -= 1;
    }
    if (button_pressed(BUTTON_RIGHT)) {
        s->x += 1;
    }
    if (button_pressed(BUTTON_LEFT)) {
        s->x -= 1;
    }
}

/* clear the screen to black */
void clear_screen(volatile unsigned short* buffer, unsigned short color) {
    unsigned short row, col;
    /* set each pixel black */
    for (row = 0; row < HEIGHT; row++) {
        for (col = 0; col < WIDTH; col++) {
            put_pixel(buffer, row, col, color);
        }
    }
}
/* return a pointer to one of the 4 character blocks (0-3) */
volatile unsigned short* char_block(unsigned long block) {
    /* they are each 16K big */
    return (volatile unsigned short*) (0x6000000 + (block * 0x4000));
}
/* return a pointer to one of the 32 screen blocks (0-31) */
volatile unsigned short* screen_block(unsigned long block) {
    /* they are each 2K big */
    return (volatile unsigned short*) (0x6000000 + (block * 0x800));
}

/* function to setup background 0 for this program */
void setup_background() {

    /* load the palette from the image into palette memory*/
    for (int i = 0; i < PALETTE_SIZE; i++) {
        palette[i] = background_palette[i];
    }

    /* load the image into char block 0 (16 bits at a time) */
    volatile unsigned short* dest = char_block(0);
    unsigned short* image = (unsigned short*) background_data;
    for (int i = 0; i < ((background_width * background_height) / 2); i++) {
        dest[i] = image[i];
    }

    /* set all control the bits in this register */
    *bg0_control = 1 |    /* priority, 0 is highest, 3 is lowest */
        (0 << 2)  |       /* the char block the image data is stored in */
        (0 << 6)  |       /* the mosaic flag */
        (1 << 7)  |       /* color mode, 0 is 16 colors, 1 is 256 colors */
        (16 << 8) |       /* the screen block the tile data is stored in */
        (1 << 13) |       /* wrapping flag */
        (0 << 14);        /* bg size, 0 is 256x256 */

    /* load the tile data into screen block 16 */
    dest = screen_block(16);
    for (int i = 0; i < (map_width * map_height); i++) {
        dest[i] = map[i];
    }
}

 /* function to setup background 0 for this program */
void setup_overlay() {

     /* load the palette from the image into palette memory*/
     for (int i = 0; i < PALETTE_SIZE; i++) {
         palette[i] = background_palette[i];
     }

     /* load the image into char block 0 (16 bits at a time) */
     volatile unsigned short* dest = char_block(0);
     unsigned short* image = (unsigned short*) background_data;
     for (int i = 0; i < ((background_width * background_height) / 2); i++) {
         dest[i] = image[i];
     }

     /* set all control the bits in new register for map 2 */
       *bg1_control = 0 |   /* new register */
          (0 << 2)  |
          (0 << 6)  |
          (1 << 7)  |
          (17 << 8) |       /* new screen block the tile data is stored in */
          (1 << 13) |
          (0 << 14);
     
    /* load the tile data into screen block 17 */
      dest = screen_block(17);
      for (int i = 0; i < (map2_width * map2_height); i++) {
         dest[i] = map2[i];
     }
}


/* just kill time */
void delay(unsigned int amount) {
    for (int i = 0; i < amount * 10; i++);
}

/* the main function */
int main() {
    /* we set the mode to mode 4 with bg2 on */
    *display_control = MODE0 | BG0_ENABLE | BG1_ENABLE;

    /* setup the background and overlay */
    setup_background();
    setup_overlay();

    /* set initial scroll to 0 */
    int xscroll = 0;
    int x1scroll = 0;

    /* loop forever */
    while (1) {
        if (button_pressed(BUTTON_RIGHT)) {
            xscroll++;
        }
        if (button_pressed(BUTTON_LEFT)) {
            xscroll--;
        }

        /* wiat for vblank before switching buffers */
        wait_vblank();
        *bg0_x_scroll = xscroll;
        *bg1_x_scroll = x1scroll;

        /* delay some */
        delay(50);
    }
}
