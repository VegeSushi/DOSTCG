#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <malloc.h>
#include <dos.h>
#include "types.h"

extern void set_video_mode(unsigned char mode);
extern void draw_sprite(int start_x, int start_y, unsigned char far* sprite);
extern void draw_rect(int x, int y, int w, int h, unsigned char color);
extern void draw_ui_box(void);
extern void print_text(int row, int col, const char* str, unsigned char color);

void clear_ui_text(void) { 
    draw_rect(2, 142, 316, 56, 1); 
}

// --- FILE IO ---
int load_character(const char* filename, Character* target) {
    FILE *f;
    ArchiveHeader header; 
    ArchiveEntry entry; 
    int i;
    unsigned char temp_byte; // Safe buffer to prevent pointer truncation!
    
    f = fopen(filename, "rb");
    if (!f) return 0; 
    
    fread(&header, sizeof(ArchiveHeader), 1, f);
    if (strncmp(header.magic, "CHAR", 4) != 0) { fclose(f); return 0; }
    
    fread(&entry, sizeof(ArchiveEntry), 1, f);
    fread(target->name, 16, 1, f);
    fread(&target->max_hp, 4, 1, f); fread(&target->hp, 4, 1, f);
    fread(&target->base_atk, 4, 1, f); fread(&target->base_def, 4, 1, f);
    fread(&target->pad1, 4, 1, f); fread(&target->pad2, 4, 1, f);
    
    for(i = 0; i < 4; i++) {
        fread(target->moves[i].name, 16, 1, f);
        fread(&target->moves[i].type, 4, 1, f);
        fread(&target->moves[i].power, 4, 1, f);
    }
    
    // Use Watcom's _fmalloc to assign extended memory
    target->sprite_data = (unsigned char far*)_fmalloc(4096);
    if (target->sprite_data) {
        // Read into Near memory, then push to Far memory to prevent W112 truncation
        for(i = 0; i < 4096; i++) {
            fread(&temp_byte, 1, 1, f);
            target->sprite_data[i] = temp_byte;
        }
    }
    
    fclose(f); 
    return 1;
}

int load_core_bg(int index, Background* bg) {
    FILE *f;
    ArchiveHeader header; 
    int i;
    
    f = fopen("content\\core.pak", "rb");
    if(!f) return 0;
    
    fread(&header, sizeof(ArchiveHeader), 1, f);
    for(i = 0; i < header.num_entries; i++) {
        fread(bg->bg_name, 16, 1, f);
        fread(&bg->bg_color, 1, 1, f);
        if(i == index) { fclose(f); return 1; }
    }
    fclose(f); 
    return 0;
}

// --- BATTLE ---
void battle_scene(Character* player, Character* enemy, Background* bg) {
    char buffer[50]; 
    int action = 1; 
    char key;
    
    draw_rect(0, 0, 320, 140, bg->bg_color); 
    if(player->sprite_data) draw_sprite(40, 60, player->sprite_data);
    if(enemy->sprite_data) draw_sprite(210, 20, enemy->sprite_data);
    draw_ui_box();
    
    while(player->hp > 0 && enemy->hp > 0) {
        clear_ui_text();
        sprintf(buffer, "%s HP:%ld", enemy->name, enemy->hp);
        print_text(1, 1, buffer, 15); 
        sprintf(buffer, "%s HP:%ld", player->name, player->hp);
        print_text(15, 20, buffer, 15); 
        
        print_text(19, 2, "Action:", 14); 
        print_text(21, 4, "1. Atk", action == 1 ? 15 : 7); 
        print_text(22, 4, "2. Item", action == 2 ? 15 : 7);
        
        key = getch();
        if(key == '1') action = 1; if(key == '2') action = 2;
        
        if(key == 0x0D) { 
            clear_ui_text();
            if(action == 1) {
                sprintf(buffer, "%s uses %s!", player->name, player->moves[0].name);
                print_text(20, 2, buffer, 15);
                enemy->hp -= (player->base_atk + player->moves[0].power) - enemy->base_def;
            }
            print_text(22, 2, "Press Key...", 14); getch();
            
            if(enemy->hp > 0) {
                clear_ui_text();
                sprintf(buffer, "%s attacks!", enemy->name);
                print_text(20, 2, buffer, 12); 
                player->hp -= enemy->base_atk;
                print_text(22, 2, "Press Key...", 14); getch();
            }
        }
    }
    
    clear_ui_text();
    sprintf(buffer, "%s Wins!", player->hp > 0 ? player->name : enemy->name);
    print_text(20, 4, buffer, 14); 
    getch();
}

int main() {
    // ALL variables declared at the top of the block! (Strict C89)
    int running = 1, selection = 1, bg_sel = 0, p1_sel = 0, p2_sel = 0;
    char key; 
    Character player, enemy; 
    Background bg;
    char roster[20][64];        
    char display_names[20][16]; 
    int roster_count = 0;
    struct find_t fileinfo;
    unsigned rc;
    char *dot;
    int cust_menu;

    // Scan Content Directory
    rc = _dos_findfirst("content\\*.chr", _A_NORMAL, &fileinfo);
    while (rc == 0 && roster_count < 20) {
        sprintf(roster[roster_count], "content\\%s", fileinfo.name);
        
        strncpy(display_names[roster_count], fileinfo.name, 15);
        display_names[roster_count][15] = '\0';
        dot = strchr(display_names[roster_count], '.');
        if (dot) *dot = '\0';

        roster_count++;
        rc = _dos_findnext(&fileinfo);
    }

    if (roster_count > 1) { p2_sel = 1; } 

    set_video_mode(0x13);

    while(running) {
        draw_rect(0, 0, 320, 200, 0); 
        print_text(5, 14, "DOSTCG", 14);
        print_text(11, 12, "1. Play Game", selection == 1 ? 15 : 7);
        print_text(13, 12, "2. Customize", selection == 2 ? 15 : 7);
        print_text(15, 12, "3. Exit", selection == 3 ? 15 : 7);

        key = getch();
        if(key == '1') selection = 1; 
        if(key == '2') selection = 2; 
        if(key == '3') selection = 3;

        if(key == 0x0D) {
            if(selection == 1) {
                if(roster_count == 0) {
                    draw_rect(0, 0, 320, 200, 0);
                    print_text(10, 5, "Error: No .chr files found!", 12); getch(); continue;
                }
                
                if(!load_character(roster[p1_sel], &player) || 
                   !load_character(roster[p2_sel], &enemy) || 
                   !load_core_bg(bg_sel, &bg)) {
                    draw_rect(0, 0, 320, 200, 0);
                    print_text(10, 5, "Error loading match data!", 12); getch(); continue;
                }
                
                battle_scene(&player, &enemy, &bg);
                
                // Cleanup RAM using Watcom's _ffree
                if(player.sprite_data) _ffree(player.sprite_data); 
                if(enemy.sprite_data) _ffree(enemy.sprite_data);
            }
            else if(selection == 2) {
                cust_menu = 1;
                while(cust_menu) {
                    draw_rect(0, 0, 320, 200, 0);
                    print_text(2, 5, "--- CUSTOMIZE MATCH ---", 14);
                    
                    if(roster_count == 0) {
                        print_text(6, 5, "NO CHARACTERS IN /CONTENT!", 12);
                    } else {
                        print_text(6, 5, "1. Player: ", 15); print_text(6, 17, display_names[p1_sel], 11);
                        print_text(8, 5, "2. Enemy:  ", 15); print_text(8, 17, display_names[p2_sel], 12);
                    }
                    
                    print_text(10, 5, "3. Arena:  ", 15); 
                    print_text(10, 17, bg_sel==0 ? "Grass" : (bg_sel==1 ? "Water" : "Fire"), 10);
                    print_text(14, 5, "Press 1,2,3 to cycle.", 7);
                    print_text(16, 5, "Press ENTER to return.", 7);
                    
                    key = getch();
                    if(key == '1' && roster_count > 0) p1_sel = (p1_sel + 1) % roster_count;
                    if(key == '2' && roster_count > 0) p2_sel = (p2_sel + 1) % roster_count;
                    if(key == '3') bg_sel = (bg_sel + 1) % 3; 
                    if(key == 0x0D) cust_menu = 0;
                }
            }
            else if(selection == 3) { running = 0; }
        }
    }
    set_video_mode(0x03); 
    return 0;
}