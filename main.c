#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <malloc.h>
#include <dos.h>
#include <time.h>
#include "types.h"

extern void set_video_mode(unsigned char mode);
extern void draw_sprite(int start_x, int start_y, unsigned char far* sprite);
extern void draw_rect(int x, int y, int w, int h, unsigned char color);
extern void draw_ui_box(void);
extern void print_text(int row, int col, const char* str, unsigned char color);

#define MAX_TEAM 4
#define MAX_ROSTER 20
#define MAX_VISIBLE 15
#define MAX_MOVES 4

/* Translated key codes (kept well outside char/ASCII range) */
#define KEY_UP    1000
#define KEY_DOWN  1001
#define KEY_LEFT  1002
#define KEY_RIGHT 1003
#define KEY_ENTER 0x0D
#define KEY_ESC   0x1B

typedef struct {
    Character members[MAX_TEAM];
    int count;
    int active; /* index into members[], -1 if none alive/none set */
} Team;

void clear_ui_text(void) {
    draw_rect(2, 142, 316, 56, 1);
}

/* --- INPUT HANDLING ---
   IMPORTANT: the caller must store the return value in an int,
   NOT a char, because KEY_UP/DOWN/LEFT/RIGHT fall outside char range.
   Extended keys (arrows, function keys) are reported by getch() as
   a 0 (or 0xE0 on some BIOSes) followed by a scan code byte. */
int read_key(void) {
    int c;
    c = getch();
    if (c == 0 || c == 0xE0) {
        c = getch();
        switch (c) {
            case 72: return KEY_UP;
            case 80: return KEY_DOWN;
            case 75: return KEY_LEFT;
            case 77: return KEY_RIGHT;
            default: return 0;
        }
    }
    return c;
}

/* --- FILE IO --- */
int load_character(const char* filename, Character* target) {
    FILE *f;
    ArchiveHeader header;
    ArchiveEntry entry;
    int i;
    unsigned char temp_byte; /* Safe buffer to prevent pointer truncation! */

    f = fopen(filename, "rb");
    if (!f) return 0;

    fread(&header, sizeof(ArchiveHeader), 1, f);
    if (strncmp(header.magic, "CHAR", 4) != 0) { fclose(f); return 0; }

    fread(&entry, sizeof(ArchiveEntry), 1, f);
    fread(target->name, 16, 1, f);
    fread(&target->max_hp, 4, 1, f); fread(&target->hp, 4, 1, f);
    fread(&target->base_atk, 4, 1, f); fread(&target->base_def, 4, 1, f);
    fread(&target->pad1, 4, 1, f); fread(&target->pad2, 4, 1, f);

    for (i = 0; i < 4; i++) {
        fread(target->moves[i].name, 16, 1, f);
        fread(&target->moves[i].type, 4, 1, f);
        fread(&target->moves[i].power, 4, 1, f);
    }

    /* Use Watcom's _fmalloc to assign extended (far) memory */
    target->sprite_data = (unsigned char far*)_fmalloc(4096);
    if (target->sprite_data) {
        /* Read into Near memory, then push to Far memory to prevent W112 truncation */
        for (i = 0; i < 4096; i++) {
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
    if (!f) return 0;

    fread(&header, sizeof(ArchiveHeader), 1, f);
    for (i = 0; i < header.num_entries; i++) {
        fread(bg->bg_name, 16, 1, f);
        fread(&bg->bg_color, 1, 1, f);
        if (i == index) { fclose(f); return 1; }
    }
    fclose(f);
    return 0;
}

/* --- TEAM HELPERS --- */
int team_alive_count(Team* t) {
    int i, c;
    c = 0;
    for (i = 0; i < t->count; i++) if (t->members[i].hp > 0) c++;
    return c;
}

int team_next_alive(Team* t, int start) {
    int i;
    for (i = start; i < t->count; i++) if (t->members[i].hp > 0) return i;
    return -1;
}

/* --- MOVE HELPERS ---
   A move slot is "empty" if the packer left it as the "None" placeholder.
   We only ever show/consider valid (non-empty) moves to the player or the AI. */
int move_is_valid(Move* m) {
    return (m->name[0] != '\0' && strncmp(m->name, "None", 4) != 0);
}

int count_valid_moves(Character* c) {
    int i, n;
    n = 0;
    for (i = 0; i < MAX_MOVES; i++) if (move_is_valid(&c->moves[i])) n++;
    return n;
}

/* Player move-select screen. Returns move index (0..3), or -1 if cancelled. */
int select_move(Character* pc) {
    int cursor;
    int key;
    int i;
    int done;
    int chosen;
    char buffer[48];

    cursor = 0;
    for (i = 0; i < MAX_MOVES; i++) { if (move_is_valid(&pc->moves[i])) { cursor = i; break; } }

    done = 0;
    chosen = -1;
    while (!done) {
        clear_ui_text();
        print_text(19, 2, "Choose a move (ESC: back):", 14);

        for (i = 0; i < MAX_MOVES; i++) {
            if (!move_is_valid(&pc->moves[i])) continue;
            sprintf(buffer, "%d. %s (PWR:%ld)", i + 1, pc->moves[i].name, pc->moves[i].power);
            print_text(21 + i, 4, buffer, (cursor == i) ? 15 : 7);
        }

        key = read_key();
        if (key == KEY_UP) {
            do { cursor--; if (cursor < 0) cursor = MAX_MOVES - 1; } while (!move_is_valid(&pc->moves[cursor]));
        } else if (key == KEY_DOWN) {
            do { cursor++; if (cursor >= MAX_MOVES) cursor = 0; } while (!move_is_valid(&pc->moves[cursor]));
        } else if (key >= '1' && key <= '4') {
            i = key - '1';
            if (i < MAX_MOVES && move_is_valid(&pc->moves[i])) cursor = i;
        } else if (key == KEY_ENTER) {
            if (move_is_valid(&pc->moves[cursor])) { chosen = cursor; done = 1; }
        } else if (key == KEY_ESC) {
            done = 1; /* chosen stays -1 */
        }
    }
    return chosen;
}

/* AI: pick uniformly at random among the character's valid moves. */
int ai_pick_move(Character* ec) {
    int valid_idx[MAX_MOVES];
    int n, i, pick;

    n = 0;
    for (i = 0; i < MAX_MOVES; i++) {
        if (move_is_valid(&ec->moves[i])) { valid_idx[n] = i; n++; }
    }
    if (n == 0) return -1;
    pick = rand() % n;
    return valid_idx[pick];
}

/* --- TEAM SELECTION SCREEN (up to MAX_TEAM cards, arrow keys + numbers) --- */
int select_team(char display_names[][16], int roster_count, int* chosen, const char* title) {
    int cursor;
    int count;
    int done;
    int key;
    int i, j;
    int found_idx;
    int visible;
    char buffer[48];

    if (roster_count <= 0) return 0;

    cursor = 0;
    count = 0;
    done = 0;
    visible = (roster_count < MAX_VISIBLE) ? roster_count : MAX_VISIBLE;

    for (i = 0; i < MAX_TEAM; i++) chosen[i] = -1;

    while (!done) {
        draw_rect(0, 0, 320, 200, 0);
        print_text(1, 2, title, 14);
        sprintf(buffer, "Selected: %d/%d", count, MAX_TEAM);
        print_text(3, 2, buffer, 7);
        print_text(4, 2, "Arrows/1-9: move  ENTER: toggle  ESC: back", 8);

        for (i = 0; i < visible; i++) {
            found_idx = -1;
            for (j = 0; j < MAX_TEAM; j++) if (chosen[j] == i) found_idx = j;
            sprintf(buffer, "%s %d. %s", (found_idx >= 0) ? "[X]" : "[ ]", i + 1, display_names[i]);
            print_text(6 + i, 4, buffer, (cursor == i) ? 15 : ((found_idx >= 0) ? 10 : 7));
        }
        print_text(6 + visible, 4, "[ DONE ]", (cursor == visible) ? 15 : 12);

        key = read_key();

        if (key == KEY_UP) { cursor--; if (cursor < 0) cursor = visible; }
        else if (key == KEY_DOWN) { cursor++; if (cursor > visible) cursor = 0; }
        else if (key >= '1' && key <= '9') {
            i = key - '1';
            if (i < visible) cursor = i;
        }
        else if (key == KEY_ESC) {
            done = 1; /* leave with whatever was chosen so far (may be 0) */
        }
        else if (key == KEY_ENTER) {
            if (cursor == visible) {
                if (count > 0) done = 1;
            } else {
                found_idx = -1;
                for (j = 0; j < MAX_TEAM; j++) if (chosen[j] == cursor) found_idx = j;
                if (found_idx >= 0) {
                    /* remove & compact */
                    for (i = found_idx; i < MAX_TEAM - 1; i++) chosen[i] = chosen[i + 1];
                    chosen[MAX_TEAM - 1] = -1;
                    count--;
                } else if (count < MAX_TEAM) {
                    chosen[count] = cursor;
                    count++;
                }
            }
        }
    }
    return count;
}

/* --- BATTLE-TIME "CHOOSE NEXT FIGHTER" SCREEN --- */
int choose_next_character(Team* t, const char* label) {
    int cursor;
    int key;
    int i;
    int done;
    int chosen;
    char buffer[48];

    cursor = 0;
    for (i = 0; i < t->count; i++) { if (t->members[i].hp > 0) { cursor = i; break; } }

    done = 0;
    chosen = -1;
    while (!done) {
        draw_rect(0, 0, 320, 200, 0);
        print_text(2, 2, label, 14);
        print_text(4, 2, "Choose next fighter:", 15);

        for (i = 0; i < t->count; i++) {
            if (t->members[i].hp <= 0) {
                sprintf(buffer, "%d. %s (FAINTED)", i + 1, t->members[i].name);
                print_text(6 + i, 4, buffer, 8);
            } else {
                sprintf(buffer, "%d. %s HP:%ld", i + 1, t->members[i].name, t->members[i].hp);
                print_text(6 + i, 4, buffer, (cursor == i) ? 15 : 7);
            }
        }

        key = read_key();
        if (key == KEY_UP) {
            do { cursor--; if (cursor < 0) cursor = t->count - 1; } while (t->members[cursor].hp <= 0);
        } else if (key == KEY_DOWN) {
            do { cursor++; if (cursor >= t->count) cursor = 0; } while (t->members[cursor].hp <= 0);
        } else if (key >= '1' && key <= '9') {
            i = key - '1';
            if (i < t->count && t->members[i].hp > 0) cursor = i;
        } else if (key == KEY_ENTER) {
            if (t->members[cursor].hp > 0) { chosen = cursor; done = 1; }
        }
    }
    return chosen;
}

/* --- BATTLE --- */
void draw_full_battle(Team* p1, Team* p2, Background* bg) {
    draw_rect(0, 0, 320, 140, bg->bg_color);
    if (p1->active >= 0 && p1->members[p1->active].sprite_data)
        draw_sprite(40, 60, p1->members[p1->active].sprite_data);
    if (p2->active >= 0 && p2->members[p2->active].sprite_data)
        draw_sprite(210, 20, p2->members[p2->active].sprite_data);
    draw_ui_box();
}

/* Damage is always at least 1: prevents (atk+power)-def from going negative
   and healing the target instead of hurting it. */
long compute_damage(Character* attacker, Move* move, Character* defender) {
    long dmg;
    dmg = (attacker->base_atk + move->power) - defender->base_def;
    if (dmg < 1) dmg = 1;
    return dmg;
}

/* Move types: 0=Attack, 1=Heal, 2=Buff_Atk, 3=Debuff_Def.
   Applies the move's effect (damage/heal/stat change) and writes two lines
   of battle text: line1 is the "X uses Y!" announcement, line2 describes
   what happened as a result. Both buffers should be at least 50 bytes. */
void resolve_move(Character* attacker, Move* move, Character* defender, char* line1, char* line2) {
    long dmg, amt;

    sprintf(line1, "%s uses %s!", attacker->name, move->name);

    switch (move->type) {
        case 0: /* Attack */
            dmg = compute_damage(attacker, move, defender);
            defender->hp -= dmg;
            if (defender->hp > defender->max_hp) defender->hp = defender->max_hp;
            if (defender->hp < 0) defender->hp = 0;
            sprintf(line2, "%s takes %ld damage!", defender->name, dmg);
            break;

        case 1: /* Heal */
            amt = move->power;
            attacker->hp += amt;
            if (attacker->hp > attacker->max_hp) attacker->hp = attacker->max_hp;
            sprintf(line2, "%s recovers %ld HP!", attacker->name, amt);
            break;

        case 2: /* Buff Atk */
            amt = move->power;
            attacker->base_atk += amt;
            sprintf(line2, "%s's attack rose!", attacker->name);
            break;

        case 3: /* Debuff Def */
            amt = move->power;
            defender->base_def -= amt;
            if (defender->base_def < 0) defender->base_def = 0;
            sprintf(line2, "%s's defense fell!", defender->name);
            break;

        default:
            sprintf(line2, "...but nothing happened!");
            break;
    }
}

void battle_scene(Team* p1, Team* p2, Background* bg) {
    char buffer[50];
    char buffer2[50];
    int action;
    int key;
    Character* pc;
    Character* ec;
    int next_idx;
    int move_idx;

    action = 1;
    p1->active = team_next_alive(p1, 0);
    p2->active = team_next_alive(p2, 0);

    draw_full_battle(p1, p2, bg);

    while (team_alive_count(p1) > 0 && team_alive_count(p2) > 0) {
        pc = &p1->members[p1->active];
        ec = &p2->members[p2->active];

        clear_ui_text();
        sprintf(buffer, "%s HP:%ld", ec->name, ec->hp);
        print_text(1, 1, buffer, 15);
        sprintf(buffer, "%s HP:%ld", pc->name, pc->hp);
        print_text(15, 20, buffer, 15);

        print_text(19, 2, "Action:", 14);
        print_text(21, 4, "1. Atk", action == 1 ? 15 : 7);
        print_text(22, 4, "2. Item", action == 2 ? 15 : 7);

        key = read_key();
        if (key == '1' || key == KEY_UP) action = 1;
        if (key == '2' || key == KEY_DOWN) action = 2;

        if (key == KEY_ENTER) {
            move_idx = -1;
            if (action == 1) {
                move_idx = select_move(pc);
            }

            if (action == 1 && move_idx < 0) {
                /* Player backed out of the move menu; redraw and let them pick again. */
                draw_full_battle(p1, p2, bg);
                continue;
            }

            clear_ui_text();
            if (action == 1) {
                resolve_move(pc, &pc->moves[move_idx], ec, buffer, buffer2);
                print_text(20, 2, buffer, 15);
                print_text(21, 2, buffer2, 15);
            }
            print_text(22, 2, "Press Key...", 14); getch();

            if (ec->hp <= 0) {
                next_idx = team_next_alive(p2, 0);
                if (next_idx >= 0) {
                    p2->active = next_idx;
                    ec = &p2->members[p2->active];
                    clear_ui_text();
                    sprintf(buffer, "Enemy sends out %s!", ec->name);
                    print_text(20, 2, buffer, 14);
                    print_text(22, 2, "Press Key...", 14); getch();
                    draw_full_battle(p1, p2, bg);
                }
                /* if next_idx < 0, enemy team is wiped; outer while() ends the battle */
            } else {
                clear_ui_text();
                move_idx = ai_pick_move(ec);
                if (move_idx >= 0) {
                    resolve_move(ec, &ec->moves[move_idx], pc, buffer, buffer2);
                    print_text(20, 2, buffer, 12);
                    print_text(21, 2, buffer2, 12);
                } else {
                    sprintf(buffer, "%s has no moves!", ec->name);
                    print_text(20, 2, buffer, 12);
                }
                print_text(22, 2, "Press Key...", 14); getch();

                if (pc->hp <= 0 && team_alive_count(p1) > 0) {
                    next_idx = choose_next_character(p1, "Your fighter fainted!");
                    if (next_idx >= 0) {
                        p1->active = next_idx;
                        draw_full_battle(p1, p2, bg);
                    }
                }
            }
        }
    }

    clear_ui_text();
    sprintf(buffer, "%s Wins!", team_alive_count(p1) > 0 ? "Player" : "Enemy");
    print_text(20, 4, buffer, 14);
    print_text(22, 4, "Press any key...", 7);
    getch();
}

int main() {
    /* ALL variables declared at the top of the block! (Strict C89) */
    int running, selection, bg_sel;
    int key;
    static Team p1team, p2team;
    static Background bg;
    static char roster[MAX_ROSTER][64];
    static char display_names[MAX_ROSTER][16];
    int roster_count;
    struct find_t fileinfo;
    unsigned rc;
    char* dot;
    int cust_menu, cust_cursor, activate;
    static int p1_idx[MAX_TEAM], p2_idx[MAX_TEAM];
    int p1_count, p2_count;
    int i;
    char buffer[48];

    srand((unsigned)time(NULL));

    running = 1;
    selection = 1;
    bg_sel = 0;
    roster_count = 0;
    cust_cursor = 1;
    p1_count = 0;
    p2_count = 0;

    /* Scan Content Directory */
    rc = _dos_findfirst("content\\*.chr", _A_NORMAL, &fileinfo);
    while (rc == 0 && roster_count < MAX_ROSTER) {
        sprintf(roster[roster_count], "content\\%s", fileinfo.name);

        strncpy(display_names[roster_count], fileinfo.name, 15);
        display_names[roster_count][15] = '\0';
        dot = strchr(display_names[roster_count], '.');
        if (dot) *dot = '\0';

        roster_count++;
        rc = _dos_findnext(&fileinfo);
    }

    /* Sensible defaults so "Play" works before visiting Customize */
    if (roster_count > 0) {
        p1_idx[0] = 0;
        p1_count = 1;
        p2_idx[0] = (roster_count > 1) ? 1 : 0;
        p2_count = 1;
    }

    set_video_mode(0x13);

    while (running) {
        draw_rect(0, 0, 320, 200, 0);
        print_text(5, 14, "DOSTCG", 14);
        print_text(11, 12, "1. Play Game", selection == 1 ? 15 : 7);
        print_text(13, 12, "2. Customize", selection == 2 ? 15 : 7);
        print_text(15, 12, "3. Exit", selection == 3 ? 15 : 7);

        key = read_key();
        if (key == '1') selection = 1;
        if (key == '2') selection = 2;
        if (key == '3') selection = 3;
        if (key == KEY_UP) { selection--; if (selection < 1) selection = 3; }
        if (key == KEY_DOWN) { selection++; if (selection > 3) selection = 1; }

        if (key == KEY_ENTER) {
            if (selection == 1) {
                if (roster_count == 0) {
                    draw_rect(0, 0, 320, 200, 0);
                    print_text(10, 5, "Error: No .chr files found!", 12); getch(); continue;
                }

                p1team.count = p1_count;
                p2team.count = p2_count;
                for (i = 0; i < p1_count; i++) load_character(roster[p1_idx[i]], &p1team.members[i]);
                for (i = 0; i < p2_count; i++) load_character(roster[p2_idx[i]], &p2team.members[i]);

                if (!load_core_bg(bg_sel, &bg)) {
                    draw_rect(0, 0, 320, 200, 0);
                    print_text(10, 5, "Error loading match data!", 12); getch(); continue;
                }

                battle_scene(&p1team, &p2team, &bg);

                /* Cleanup RAM using Watcom's _ffree */
                for (i = 0; i < p1team.count; i++)
                    if (p1team.members[i].sprite_data) _ffree(p1team.members[i].sprite_data);
                for (i = 0; i < p2team.count; i++)
                    if (p2team.members[i].sprite_data) _ffree(p2team.members[i].sprite_data);
            }
            else if (selection == 2) {
                cust_menu = 1;
                cust_cursor = 1;
                while (cust_menu) {
                    draw_rect(0, 0, 320, 200, 0);
                    print_text(2, 5, "--- CUSTOMIZE MATCH ---", 14);

                    if (roster_count == 0) {
                        print_text(6, 5, "NO CHARACTERS IN /CONTENT!", 12);
                    } else {
                        sprintf(buffer, "1. Player Team: %d/%d picked", p1_count, MAX_TEAM);
                        print_text(6, 5, buffer, cust_cursor == 1 ? 15 : 11);
                        sprintf(buffer, "2. Enemy Team:  %d/%d picked", p2_count, MAX_TEAM);
                        print_text(8, 5, buffer, cust_cursor == 2 ? 15 : 12);
                    }

                    sprintf(buffer, "3. Arena: %s", bg_sel == 0 ? "Grass" : (bg_sel == 1 ? "Water" : "Fire"));
                    print_text(10, 5, buffer, cust_cursor == 3 ? 15 : 10);
                    print_text(12, 5, "4. Back", cust_cursor == 4 ? 15 : 7);

                    print_text(15, 5, "Arrows/1-4: move  ENTER: select", 7);
                    print_text(16, 5, "ESC: back", 7);

                    key = read_key();
                    activate = 0;

                    if (key == '1') { cust_cursor = 1; activate = 1; }
                    if (key == '2') { cust_cursor = 2; activate = 1; }
                    if (key == '3') { cust_cursor = 3; activate = 1; }
                    if (key == '4') { cust_cursor = 4; activate = 1; }
                    if (key == KEY_UP) { cust_cursor--; if (cust_cursor < 1) cust_cursor = 4; }
                    if (key == KEY_DOWN) { cust_cursor++; if (cust_cursor > 4) cust_cursor = 1; }
                    if (key == KEY_ENTER) activate = 1;
                    if (key == KEY_ESC) cust_menu = 0;

                    if (activate && cust_cursor == 4) {
                        cust_menu = 0;
                    } else if (activate && roster_count > 0) {
                        if (cust_cursor == 1) {
                            p1_count = select_team(display_names, roster_count, p1_idx, "SELECT YOUR TEAM (up to 4)");
                        } else if (cust_cursor == 2) {
                            p2_count = select_team(display_names, roster_count, p2_idx, "SELECT ENEMY TEAM (up to 4)");
                        } else if (cust_cursor == 3) {
                            bg_sel = (bg_sel + 1) % 3;
                        }
                    }
                }
            }
            else if (selection == 3) { running = 0; }
        }
    }
    set_video_mode(0x03);
    return 0;
}
