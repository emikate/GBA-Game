
/*
 * game.c
 * simple catcher game for the GBA
 */

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

/* include these files */
#include "map.h"
#include "bg.h"
#include "objects.h"
#include <stdlib.h>

/* the tile mode flags needed for display control register */
#define MODE0 0x00
#define MODE1 0x00
#define MODE4 0x0400
#define BG0_ENABLE 0x100
#define BG1_ENABLE 0x200

//control registers for tile layers
volatile unsigned short* bg0_control = (volatile unsigned short*) 0x4000008;
volatile unsigned short* bg1_control = (volatile     unsigned short*) 0x400000a;

/* flags to set sprite handling in display control register */
#define SPRITE_MAP_2D 0x0
#define SPRITE_MAP_1D 0x40
#define SPRITE_ENABLE 0x1000

/* palette is always 256 colors */
#define PALETTE_SIZE 256

/* there are 128 sprites on the GBA */
#define NUM_SPRITES 128

/* the display control pointer points to the gba graphics register */
volatile unsigned long* display_control = (volatile unsigned long*) 0x4000000;

#define SHOW_BACK 64

/*  pointer points to 16-bit colors of which there are 240x160 */
 volatile unsigned short* screen = (volatile unsigned short*) 0x6000000;
volatile unsigned short* front_buffer = (volatile unsigned short*) 0x6000000;
volatile unsigned short* back_buffer = (volatile unsigned short*)  0x600A000;

/* the memory location which controls sprite attributes */
volatile unsigned short* sprite_attribute_memory = (volatile unsigned short*) 0x7000000;

/* the memory location which stores sprite image data */
volatile unsigned short* sprite_image_memory = (volatile unsigned short*) 0x6010000;

/* the address of the color palettes used for backgrounds and sprites */
volatile unsigned short* background_palette = (volatile unsigned short*) 0x5000000;
volatile unsigned short* sprite_palette = (volatile unsigned short*) 0x5000200;
int next_palette_index = 0;

/* the button register holds the bits which indicate whether each button has
 * been pressed - this has got to be volatile as well
 */
volatile unsigned short* buttons = (volatile unsigned short*) 0x04000130;
unsigned char button_pressed(unsigned short button) {     /* and the button register with the button constant we want */
     unsigned short pressed = *buttons & button;
    /* if this value is zero, then it's not pressed */
     if (pressed == 0) {
         return 1;
     } else {
         return 0;
     }
}

/* scrolling registers for backgrounds */
volatile short* bg0_x_scroll = (unsigned short*) 0x4000010;
volatile short* bg0_y_scroll = (unsigned short*) 0x4000012;
volatile short* bg1_x_scroll = (unsigned short*) 0x4000014;
volatile short* bg1_y_scroll = (unsigned short*) 0x4000016;
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

/* flag for turning on DMA */
#define DMA_ENABLE 0x80000000

/* flags for the sizes to transfer, 16 or 32 bits */
#define DMA_16 0x00000000
#define DMA_32 0x04000000

/* pointer to the DMA source location */
volatile unsigned int* dma_source = (volatile unsigned int*) 0x40000D4;

/* pointer to the DMA destination location */
volatile unsigned int* dma_destination = (volatile unsigned int*) 0x40000D8;

/* pointer to the DMA count/control */
volatile unsigned int* dma_count = (volatile unsigned int*) 0x40000DC;

/* copy data using DMA */
void memcpy16_dma(unsigned short* dest, unsigned short* source, int amount) {
    *dma_source = (unsigned int) source;
    *dma_destination = (unsigned int) dest;
    *dma_count = amount | DMA_16 | DMA_ENABLE;
}

/* function to setup background 0 for this program */
void setup_background() {

    /* load the palette from the image into palette memory*/
    memcpy16_dma((unsigned short*) background_palette, (unsigned short*) 
bg_palette, PALETTE_SIZE);

    /* load the image into char block 0 */
    memcpy16_dma((unsigned short*) char_block(0), (unsigned short*) bg_data,
            (bg_width * bg_height) / 2);

    /* set all control the bits in this register */
    *bg0_control = 0 |    /* priority, 0 is highest, 3 is lowest */
        (0 << 2)  |       /* the char block the image data is stored in */
        (0 << 6)  |       /* the mosaic flag */
        (1 << 7)  |       /* color mode, 0 is 16 colors, 1 is 256 colors */
        (16 << 8) |       /* the screen block the tile data is stored in */
        (1 << 13) |       /* wrapping flag */
        (0 << 14);        /* bg size, 0 is 256x256 */

    /* load the tile data into screen block 16 */
    memcpy16_dma((unsigned short*) screen_block(16), (unsigned short*) map, map_width * map_height);
}

/* just kill time */
void delay(unsigned int amount) {
    for (int i = 0; i < amount * 10; i++);
}

 unsigned char add_color(unsigned char r, unsigned char g, unsigned char b) {
    unsigned short color = b << 10;
     color += g << 5;
     color += r;

     /* add the color to the palette */
     background_palette[next_palette_index] = color;

     /* increment the index */
     next_palette_index++;

     /* return index of color just added */
     return next_palette_index - 1;
}


/* a sprite is a moveable image on the screen */
struct Sprite {
    unsigned short attribute0;
    unsigned short attribute1;
    unsigned short attribute2;
    unsigned short attribute3;
};

/* array of all the sprites available on the GBA */
struct Sprite sprites[NUM_SPRITES];
int next_sprite_index = 0;

/* the different sizes of sprites which are possible */
enum SpriteSize {
    SIZE_8_8,
    SIZE_16_16,
    SIZE_32_32,
    SIZE_64_64,
    SIZE_16_8,
    SIZE_32_8,
    SIZE_32_16,
    SIZE_64_32,
    SIZE_8_16,
    SIZE_8_32,
    SIZE_16_32,
    SIZE_32_64
};

/* function to initialize a sprite with its properties, and return a pointer */
struct Sprite* sprite_init(int x, int y, enum SpriteSize size,
        int horizontal_flip, int vertical_flip, int tile_index, int priority) {

    /* grab the next index */
    int index = next_sprite_index++;

    /* setup the bits used for each shape/size possible */
    int size_bits, shape_bits;
    switch (size) {
        case SIZE_8_8:   size_bits = 0; shape_bits = 0; break;
        case SIZE_16_16: size_bits = 1; shape_bits = 0; break;
        case SIZE_32_32: size_bits = 2; shape_bits = 0; break;
        case SIZE_64_64: size_bits = 3; shape_bits = 0; break;
        case SIZE_16_8:  size_bits = 0; shape_bits = 1; break;
        case SIZE_32_8:  size_bits = 1; shape_bits = 1; break;
        case SIZE_32_16: size_bits = 2; shape_bits = 1; break;
        case SIZE_64_32: size_bits = 3; shape_bits = 1; break;
        case SIZE_8_16:  size_bits = 0; shape_bits = 2; break;
        case SIZE_8_32:  size_bits = 1; shape_bits = 2; break;
        case SIZE_16_32: size_bits = 2; shape_bits = 2; break;
        case SIZE_32_64: size_bits = 3; shape_bits = 2; break;
    }

    int h = horizontal_flip ? 1 : 0;
    int v = vertical_flip ? 1 : 0;

    /* set up the first attribute */
    sprites[index].attribute0 = y |             /* y coordinate */
                            (0 << 8) |          /* rendering mode */
                            (0 << 10) |         /* gfx mode */
                            (0 << 12) |         /* mosaic */
                            (1 << 13) |         /* color mode, 0:16, 1:256 */
                            (shape_bits << 14); /* shape */

    /* set up the second attribute */
    sprites[index].attribute1 = x |             /* x coordinate */
                            (0 << 9) |          /* affine flag */
                            (h << 12) |         /* horizontal flip flag */
                            (v << 13) |         /* vertical flip flag */
                            (size_bits << 14);  /* size */

    /* setup the second attribute */
    sprites[index].attribute2 = tile_index |   // tile index */
                            (priority << 10) | // priority */
                            (0 << 12);         // palette bank (only 16 color)*/

    /* return pointer to this sprite */
    return &sprites[index];
}

/* update all of the spries on the screen */
void sprite_update_all() {
    /* copy them all over */
    memcpy16_dma((unsigned short*) sprite_attribute_memory, (unsigned short*) sprites, NUM_SPRITES * 4);
}

/* setup all sprites */
void sprite_clear() {
    /* clear the index counter */
    next_sprite_index = 0;

    /* move all sprites offscreen to hide them */
    for(int i = 0; i < NUM_SPRITES; i++) {
        sprites[i].attribute0 = SCREEN_HEIGHT;
        sprites[i].attribute1 = SCREEN_WIDTH;
    }
}

/* set a sprite postion */
void sprite_position(struct Sprite* sprite, int x, int y) {
    /* clear out the y coordinate */
    sprite->attribute0 &= 0xff00;

    /* set the n/ew y coordinate */
    sprite->attribute0 |= (y & 0xff);

    /* clear out the x coordinate */
    sprite->attribute1 &= 0xfe00;

    /* set the new x coordinate */
    sprite->attribute1 |= (x & 0x1ff);
}

/* move a sprite in a direction */
void sprite_move(struct Sprite* sprite, int dx, int dy) {
    /* get the current y coordinate */
    int y = sprite->attribute0 & 0xff;

    /* get the current x coordinate */
    int x = sprite->attribute1 & 0x1ff;

    /* move to the new location */
    sprite_position(sprite, x + dx, y + dy);
}

/* change the vertical flip flag */
void sprite_set_vertical_flip(struct Sprite* sprite, int vertical_flip) {
    if (vertical_flip) {
        /* set the bit */
        sprite->attribute1 |= 0x2000;
    } else {
        /* clear the bit */
        sprite->attribute1 &= 0xdfff;
    }
}

/* change the vertical flip flag */
void sprite_set_horizontal_flip(struct Sprite* sprite, int horizontal_flip) {
    if (horizontal_flip) {
        /* set the bit */
        sprite->attribute1 |= 0x1000;
    } else {
        /* clear the bit */
        sprite->attribute1 &= 0xefff;
    }
}

/* change the tile offset of a sprite */
void sprite_set_offset(struct Sprite* sprite, int offset) {
    /* clear the old offset */
    sprite->attribute2 &= 0xfc00;

    /* apply the new one */
    sprite->attribute2 |= (offset & 0x03ff);
}

/* setup the sprite image and palette */
void setup_sprite_image() {
    /* load the palette from the image into palette memory*/
    memcpy16_dma((unsigned short*) sprite_palette, (unsigned short*) objects_palette, PALETTE_SIZE);

    /* load the image into sprite image memory */
    memcpy16_dma((unsigned short*) sprite_image_memory, (unsigned short*) objects_data, (objects_width * objects_height) / 2);
}

/* a struct for the bowl's logic and behavior */
struct Bowl {
    /* the actual sprite attribute info */
    struct Sprite* sprite;

    /* the x and y postion */
    int x, y;

    /* which frame of the animation he is on */
    int frame;

    /* the number of frames to wait before flipping */
    int animation_delay;

    /* the animation counter counts how many frames until we flip */
    int counter;

    /* whether the bowl is moving right now or not */
    int move;

    /* the number of pixels away from the edge of the screen the bowl stays */
    int border;
};

struct Bowl playerBowl;

/* initialize the bowl */
void bowl_init(struct Bowl* bowl) {
    bowl->x = 100;
    bowl->y = 113;
    bowl->border = 40;
    bowl->frame = 0;
    bowl->move = 0;
    bowl->counter = 0;
    bowl->animation_delay = 8;
    bowl->sprite = sprite_init(bowl->x, bowl->y, SIZE_32_32, 0, 0, bowl->frame, 0);
}

/* move the bowl left or right returns if it is at edge of the screen */
int bowl_left(struct Bowl* bowl) {
    /* face left */
    sprite_set_horizontal_flip(bowl->sprite, 1);
    bowl->move = 1;

    /* if we are at the left end, just scroll the screen */
    if (bowl->x < bowl->border) {
        return 1;
    } else {
        /* else move left */
        bowl->x--;
        return 0;
    }
}
int bowl_right(struct Bowl* bowl) {
    /* face right */
    sprite_set_horizontal_flip(bowl->sprite, 0);
    bowl->move = 1;

    /* if we are at the right end, just scroll the screen */
    if (bowl->x > (SCREEN_WIDTH - 16 - bowl->border)) {
        return 1;
    } else {
        /* else move right */
        bowl->x++;
        return 0;
    }
}

void bowl_stop(struct Bowl* bowl) {
    bowl->move = 0;
    bowl->frame = 0;
    bowl->counter = 7;
    //sprite_set_offset(bowl->sprite, bowl->frame);
}

/* update the bowl */
void bowl_update(struct Bowl* bowl) {
    if (bowl->move) {
        bowl->counter++;
        if (bowl->counter >= bowl->animation_delay) {
            bowl->frame = bowl->frame + 16;
            if (bowl->frame > 16) {
                bowl->frame = 0;
            }
            //sprite_set_offset(bowl->sprite, bowl->frame);
            bowl->counter = 0;
        }
    }

    sprite_position(bowl->sprite, bowl->x, bowl->y);
}

//call before move function to initialize sprites to fall more randomly
void falling_sprites(struct Sprite *sprite, int x, int y) {
    sprite->attribute0 = y | (0 << 8) | (0 << 10) | (0 << 12) | (1 << 13) | (0 << 14);
    sprite->attribute1 = x | (0 << 9) | (0 << 12) | (0 << 13) | (2 << 14); 
    sprite->attribute2 = 32 | (0 << 10) | (0 << 12);

    int randomOffset = rand() % SCREEN_WIDTH;
    sprite_position(sprite, randomOffset, y);
}

//score by hearts (lives)!
#define NUM_LIVES 5
struct Sprite *lives[NUM_LIVES];

void lives_init() {
    for (int i=0; i < NUM_LIVES; i++) {
        lives[i] = sprite_init(i * 32, 0, SIZE_32_32, 0, 0, 32*3, 0);
    }
}

int sprite_collide(struct Sprite* sprite1, struct Sprite* sprite2) {
    int x1 = sprite1->attribute1 & 0x1FF;
    int y1 = sprite1->attribute0 & 0xFF;
    int x2 = sprite2->attribute1 & 0x1FF;
    int y2 = sprite2->attribute0 & 0xFF;

    return (x1 < x2 + 32) && (x1 + 32 > x2) && (y1 < y2 +32) && (y1 + 32 > y2);
}


void gg(struct Sprite* life) {
    life->attribute0 = SCREEN_HEIGHT;
    life->attribute1 = SCREEN_WIDTH;
}

int score = 5;
int total_lives = NUM_LIVES;
int game_over =0;

void decrease_score() {
    score--;
    if (score <= 0) {
        total_lives--;    
        if (total_lives <= 0) {
            game_over = 1;
        } else { 
            score = 5;
        }
    }
}

void handle_collisions(struct Bowl* bowl) {
    for (int i=0; i < NUM_LIVES; i++) {
        if (sprite_collide(lives[i], bowl->sprite)) {
            decrease_score();
            gg(lives[i]);
            lives[i] = sprite_init(i*32, 0, SIZE_32_32, 0, 0, 32*3, 0);
        }
    }
}

enum Gamestate {
    INTRO,
    GAME,
    GAME_OVER 
};

enum Gamestate gameState = INTRO;

//require graphics/draw text functions -if time permits
void intro_screen() {
}

void over_screen() {
}

void intro_input() {
    if (button_pressed(BUTTON_START)) {
        gameState = GAME;
    }
}
void gg_input() {
    if (button_pressed(BUTTON_START)) {
    gameState = INTRO;
    score = 5;
    total_lives = NUM_LIVES;
    game_over = 0;
    bowl_init(&playerBowl);
    lives_init();
    }
}

/* the main function */
int main() {
    /* we set the mode to mode 0 with bg0 on */
    *display_control = MODE0 | BG0_ENABLE | SPRITE_ENABLE | SPRITE_MAP_1D;

    /* setup the background 0 */
    setup_background();

    /* setup the sprite image data */
    setup_sprite_image();

    /* clear all the sprites on screen now */
    sprite_clear();

    struct Bowl playerBowl;
    bowl_init(&playerBowl);
    lives_init();

    struct Sprite *grape = sprite_init(32, 0, SIZE_32_32, 0, 0, 32, 0);
    struct Sprite *apple = sprite_init(64, 0, SIZE_32_32, 0, 0, 64, 0);
    struct Sprite *bananas = sprite_init(128, 0, SIZE_32_32, 0, 0, 128, 0);
    struct Sprite *mushroom = sprite_init(160, 0, SIZE_32_32, 0, 0, 160, 0);
    /* set initial scroll to 0 */
    int xscroll = 0;

    /* loop forever */
    while (1) {
        int dx = 0;
        int dy = 1;
        sprite_move(grape, dx, dy);
        sprite_move(apple, dx, dy);
        sprite_move(bananas, dx, dy);  
        sprite_move(mushroom,dx,dy);        

        bowl_update(&playerBowl);

        /* now the arrow keys move the bowl */
        if (button_pressed(BUTTON_RIGHT)) {
            if (bowl_right(&playerBowl)) {
                xscroll++;
            }
        } else if (button_pressed(BUTTON_LEFT)) {
            if (bowl_left(&playerBowl)) {
                xscroll--;
            }
        } else {
            bowl_stop(&playerBowl);
        }

        /* wait for vblank before scrolling and moving sprites */
        wait_vblank();
        *bg0_x_scroll = xscroll;
        sprite_update_all();

        /* delay some */
        delay(100);
    }
}

