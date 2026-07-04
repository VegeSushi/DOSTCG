#include <dos.h>
#include <i86.h>
#include "types.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200

// Declare the pointer globally, but DO NOT initialize it here!
// This fixes "Error! E1054: Expression must be constant"
unsigned char far* VGA;

void set_video_mode(unsigned char mode) {
    union REGS regs;
    
    // Initialize the pointer at runtime inside the function instead
    VGA = (unsigned char far*)MK_FP(0xA000, 0);
    
    regs.h.ah = 0x00;
    regs.h.al = mode;
    int86(0x10, &regs, &regs);
}

// Render 64x64 sprite (skips 0 for transparency)
void draw_sprite(int start_x, int start_y, unsigned char far* sprite) {
    int x, y;
    for(y = 0; y < 64; y++) {
        for(x = 0; x < 64; x++) {
            unsigned char color = sprite[(y * 64) + x];
            if(color != 0) { 
                VGA[(start_y + y) * SCREEN_WIDTH + (start_x + x)] = color;
            }
        }
    }
}

// Draws a solid rectangle (Used for backgrounds and UI borders)
void draw_rect(int x, int y, int w, int h, unsigned char color) {
    int i, j;
    for(j = 0; j < h; j++) {
        for(i = 0; i < w; i++) {
            VGA[(y + j) * SCREEN_WIDTH + (x + i)] = color;
        }
    }
}

// Draws the classic 90s RPG bottom-screen text box
void draw_ui_box() {
    draw_rect(0, 140, 320, 60, 1);    
    draw_rect(0, 140, 320, 2, 15);    
    draw_rect(0, 198, 320, 2, 15);    
    draw_rect(0, 140, 2, 60, 15);     
    draw_rect(318, 140, 2, 60, 15);   
}

// Prints text directly in Mode 13h using the BIOS ROM font!
void print_text(int row, int col, const char* str, unsigned char color) {
    union REGS regs;
    
    regs.h.ah = 0x02;
    regs.h.bh = 0x00; 
    regs.h.dh = row;
    regs.h.dl = col;
    int86(0x10, &regs, &regs);
    
    while(*str) {
        regs.h.ah = 0x0E;       
        regs.h.al = *str++;
        regs.h.bl = color;      
        int86(0x10, &regs, &regs);
    }
}
