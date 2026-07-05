#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <malloc.h>
#include <dos.h>
#include <time.h>
#include "types.h"
#include "serial.h"

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

#define BIOS_TICKS_TIMEOUT 18L /* ~1 second, BIOS tick counter runs at ~18.2 Hz */
#define TEAM_EXCHANGE_TIMEOUT (600L * BIOS_TICKS_TIMEOUT) /* ~10 minutes: this only
    happens once, while a human is still browsing the team-select screen, so it
    needs to be generous rather than tuned like the in-battle timeouts. */

#define READY_BYTE 0x55          /* distinct from the 0xAA handshake marker */
#define READY_LINK_SILENCE_TIMEOUT (600L * BIOS_TICKS_TIMEOUT) /* only give up if the
    link has been TOTALLY silent (no bytes at all, not even a stray one) this long -
    that means the cable/link is actually dead, not just "still picking a team". */
#define TEAM_SEND_MAX_RETRIES 5

#define MOVE_WAIT_TIMEOUT (600L * BIOS_TICKS_TIMEOUT) /* ~10 minutes: waiting for the
    OTHER player's move/next-fighter pick is bounded by how long a human takes to
    decide, not by link health - a short timeout here (the old code used 5-10
    SECONDS) means the wait fails almost every single turn as soon as either
    player takes a normal amount of time to think. This should only ever fire if
    the link is truly gone. */

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
    if (target->hp > target->max_hp) target->hp = target->max_hp;
    if (target->hp < 0) target->hp = 0;
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

/* Writes a Character back out in the exact same layout load_character() reads,
   so files this game saves can also be read by it (and vice versa with packer.py). */
int save_character(const char* filename, Character* target) {
    FILE *f;
    ArchiveHeader header;
    ArchiveEntry entry;
    int i;
    unsigned char temp_byte;

    f = fopen(filename, "wb");
    if (!f) return 0;

    memcpy(header.magic, "CHAR", 4);
    header.version = 1;
    header.num_entries = 1;
    fwrite(&header, sizeof(ArchiveHeader), 1, f);

    memset(entry.name, 0, 16);
    strncpy(entry.name, target->name, 15);
    entry.offset = 28;
    entry.size = 4096 + 56;
    entry.type = 0;
    entry.pad[0] = entry.pad[1] = entry.pad[2] = 0;
    fwrite(&entry, sizeof(ArchiveEntry), 1, f);

    fwrite(target->name, 16, 1, f);
    fwrite(&target->max_hp, 4, 1, f); fwrite(&target->hp, 4, 1, f);
    fwrite(&target->base_atk, 4, 1, f); fwrite(&target->base_def, 4, 1, f);
    fwrite(&target->pad1, 4, 1, f); fwrite(&target->pad2, 4, 1, f);

    for (i = 0; i < 4; i++) {
        fwrite(target->moves[i].name, 16, 1, f);
        fwrite(&target->moves[i].type, 4, 1, f);
        fwrite(&target->moves[i].power, 4, 1, f);
    }

    /* Sprite lives in far memory; pull each byte through a near temp
       (mirrors the far-memory-safe read pattern used in load_character). */
    for (i = 0; i < 4096; i++) {
        temp_byte = target->sprite_data ? target->sprite_data[i] : 16;
        fwrite(&temp_byte, 1, 1, f);
    }

    fclose(f);
    return 1;
}

#define LOADOUT_FILE "content\\loadout.sav"
#define LOADOUT_MAGIC "LOUT"

/* Remembers whatever the player picked in Customize (both teams + arena)
   so they don't have to redo it every time they start the game. */
void save_loadout(int* p1_idx, int p1_count, int* p2_idx, int p2_count, int bg_sel) {
    FILE *f;

    f = fopen(LOADOUT_FILE, "wb");
    if (!f) return;

    fwrite(LOADOUT_MAGIC, 4, 1, f);
    fwrite(&p1_count, sizeof(int), 1, f);
    fwrite(p1_idx, sizeof(int), MAX_TEAM, f);
    fwrite(&p2_count, sizeof(int), 1, f);
    fwrite(p2_idx, sizeof(int), MAX_TEAM, f);
    fwrite(&bg_sel, sizeof(int), 1, f);

    fclose(f);
}

/* Returns 1 and fills the outputs if a valid loadout was read, 0 otherwise.
   Any index that no longer fits inside the current roster is dropped, so a
   saved loadout never crashes the game if .chr files were added/removed. */
int load_loadout(int* p1_idx, int* p1_count, int* p2_idx, int* p2_count, int* bg_sel, int roster_count) {
    FILE *f;
    char magic[4];
    int i, w;

    f = fopen(LOADOUT_FILE, "rb");
    if (!f) return 0;

    if (fread(magic, 4, 1, f) != 1 || strncmp(magic, LOADOUT_MAGIC, 4) != 0) { fclose(f); return 0; }

    fread(p1_count, sizeof(int), 1, f);
    fread(p1_idx, sizeof(int), MAX_TEAM, f);
    fread(p2_count, sizeof(int), 1, f);
    fread(p2_idx, sizeof(int), MAX_TEAM, f);
    fread(bg_sel, sizeof(int), 1, f);
    fclose(f);

    /* Sanitize: compact out any index that doesn't exist in this roster */
    w = 0;
    for (i = 0; i < *p1_count && i < MAX_TEAM; i++) {
        if (p1_idx[i] >= 0 && p1_idx[i] < roster_count) p1_idx[w++] = p1_idx[i];
    }
    *p1_count = w;

    w = 0;
    for (i = 0; i < *p2_count && i < MAX_TEAM; i++) {
        if (p2_idx[i] >= 0 && p2_idx[i] < roster_count) p2_idx[w++] = p2_idx[i];
    }
    *p2_count = w;

    if (*bg_sel < 0 || *bg_sel > 2) *bg_sel = 0;

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
            sprintf(buffer, "%d. %.15s (PWR:%ld)", i + 1, pc->moves[i].name, pc->moves[i].power);
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
            sprintf(buffer, "%s %d. %.15s", (found_idx >= 0) ? "[X]" : "[ ]", i + 1, display_names[i]);
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
                sprintf(buffer, "%d. %.15s (FAINTED)", i + 1, t->members[i].name);
                print_text(6 + i, 4, buffer, 8);
            } else {
                sprintf(buffer, "%d. %.15s HP:%ld", i + 1, t->members[i].name, t->members[i].hp);
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
    if (dmg > 9999) dmg = 9999; /* defensive cap: guards against overflowed/corrupt stats */
    return dmg;
}

/* Move types: 0=Attack, 1=Heal, 2=Buff_Atk, 3=Debuff_Def.
   Applies the move's effect (damage/heal/stat change) and writes two lines
   of battle text: line1 is the "X uses Y!" announcement, line2 describes
   what happened as a result. Both buffers should be at least 50 bytes. */
void resolve_move(Character* attacker, Move* move, Character* defender, char* line1, char* line2) {
    long dmg, amt;

    sprintf(line1, "%.15s uses %.15s!", attacker->name, move->name);

    switch (move->type) {
        case 0: /* Attack */
            dmg = compute_damage(attacker, move, defender);
            defender->hp -= dmg;
            if (defender->hp < 0) defender->hp = 0;
            if (defender->hp > defender->max_hp) defender->hp = defender->max_hp;
            sprintf(line2, "%.15s takes %ld damage!", defender->name, dmg);
            break;

        case 1: /* Heal */
            amt = move->power;
            attacker->hp += amt;
            if (attacker->hp > attacker->max_hp) attacker->hp = attacker->max_hp;
            sprintf(line2, "%.15s recovers %ld HP!", attacker->name, amt);
            break;

        case 2: /* Buff Atk */
            amt = move->power;
            attacker->base_atk += amt;
            if (attacker->base_atk > 9999) attacker->base_atk = 9999;
            sprintf(line2, "%.15s's attack rose!", attacker->name);
            break;

        case 3: /* Debuff Def */
            amt = move->power;
            defender->base_def -= amt;
            if (defender->base_def < 0) defender->base_def = 0;
            sprintf(line2, "%.15s's defense fell!", defender->name);
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
        /* Enemy HP Update */
        sprintf(buffer, "%.15s HP:%-4ld", ec->name, ec->hp);
        print_text(1, 1, buffer, 15);

        /* Player HP Update */
        sprintf(buffer, "%.15s HP:%-4ld", pc->name, pc->hp);
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
                    sprintf(buffer, "Enemy sends out %.15s!", ec->name);
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
                    sprintf(buffer, "%.15s has no moves!", ec->name);
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

/* --- MULTIPLAYER (COM PORT) ---
   Turn-based, so a simple polled/lockstep protocol over the serial link is
   enough: both sides send their pick, then both wait for the other side's
   pick, then both apply moves in the same fixed order (host first), so
   the two machines' game states never diverge. */
void net_send_character(int port, Character* c) {
    int i;
    unsigned char b;

    serial_send_buffer(port, c->name, 16);
    serial_send_buffer(port, &c->max_hp, 4);
    serial_send_buffer(port, &c->hp, 4);
    serial_send_buffer(port, &c->base_atk, 4);
    serial_send_buffer(port, &c->base_def, 4);
    serial_send_buffer(port, &c->pad1, 4);
    serial_send_buffer(port, &c->pad2, 4);

    for (i = 0; i < MAX_MOVES; i++) {
        serial_send_buffer(port, c->moves[i].name, 16);
        serial_send_buffer(port, &c->moves[i].type, 4);
        serial_send_buffer(port, &c->moves[i].power, 4);
    }

    for (i = 0; i < 4096; i++) {
        b = c->sprite_data ? c->sprite_data[i] : 16;
        while (!serial_send_byte(port, b)) { /* retry */ }
    }
}

int net_recv_character(int port, Character* c, long timeout_ticks) {
    int i;
    unsigned char b;

    if (!serial_recv_buffer(port, c->name, 16, timeout_ticks)) return 0;
    if (!serial_recv_buffer(port, &c->max_hp, 4, timeout_ticks)) return 0;
    if (!serial_recv_buffer(port, &c->hp, 4, timeout_ticks)) return 0;
    if (!serial_recv_buffer(port, &c->base_atk, 4, timeout_ticks)) return 0;
    if (!serial_recv_buffer(port, &c->base_def, 4, timeout_ticks)) return 0;
    if (!serial_recv_buffer(port, &c->pad1, 4, timeout_ticks)) return 0;
    if (!serial_recv_buffer(port, &c->pad2, 4, timeout_ticks)) return 0;

    for (i = 0; i < MAX_MOVES; i++) {
        if (!serial_recv_buffer(port, c->moves[i].name, 16, timeout_ticks)) return 0;
        if (!serial_recv_buffer(port, &c->moves[i].type, 4, timeout_ticks)) return 0;
        if (!serial_recv_buffer(port, &c->moves[i].power, 4, timeout_ticks)) return 0;
    }

    c->sprite_data = (unsigned char far*)_fmalloc(4096);
    for (i = 0; i < 4096; i++) {
        if (!serial_recv_byte(port, &b, timeout_ticks)) return 0;
        if (c->sprite_data) c->sprite_data[i] = b;
    }
    return 1;
}

void net_send_team(int port, Team* t) {
    int i;
    unsigned long chk;
    serial_checksum_start();
    net_send_int(port, t->count);
    for (i = 0; i < t->count; i++) net_send_character(port, &t->members[i]);
    chk = serial_checksum_value();
    serial_send_buffer(port, &chk, sizeof(chk));
}

/* Returns 0 on timeout/link failure, 1 on success, -1 on a checksum
   mismatch (bytes arrived but got scrambled/desynced somewhere - worth
   a fresh retry rather than treating it the same as a dead link). */
int net_recv_team(int port, Team* t, long timeout_ticks) {
    int i;
    unsigned long chk_theirs, chk_mine;

    serial_checksum_start();
    if (!net_recv_int(port, &t->count, timeout_ticks)) { serial_checksum_value(); return 0; }
    if (t->count < 0 || t->count > MAX_TEAM) { serial_checksum_value(); return -1; }
    for (i = 0; i < t->count; i++) {
        if (!net_recv_character(port, &t->members[i], timeout_ticks)) { serial_checksum_value(); return 0; }
    }
    chk_mine = serial_checksum_value();
    if (!serial_recv_buffer(port, &chk_theirs, sizeof(chk_theirs), timeout_ticks)) return 0;
    if (chk_theirs != chk_mine) {
        net_log("net_recv_team: checksum mismatch, data got scrambled");
        return -1;
    }
    return 1;
}

/* Mutual "ready" rendezvous: keeps pinging a marker byte and watching for
   the peer's own marker, so NEITHER side starts streaming the (large,
   slow) team payload until it knows the other side has also finished
   picking its team and is actually sitting in this loop listening.
   This is what stops a fast player's data from racing ahead of a slower
   player who is still on the team-select screen. Stays responsive the
   whole time: screen text is refreshed, ESC still works, and it only
   gives up if the link has gone completely silent for a long while
   (a real dead cable), not just because the peer is still thinking. */
int wait_for_peer_ready(int port) {
    unsigned long last_activity;
    unsigned char b;
    long blink = 0;

    serial_drain_rx(port);
    last_activity = get_ticks();
    for (;;) {
        if (kbhit()) {
            if (getch() == KEY_ESC) { net_log("team exchange: cancelled by user"); return 0; }
        }
        serial_send_byte(port, READY_BYTE);
        if (serial_recv_byte(port, &b, 1)) {
            last_activity = get_ticks();
            if (b == READY_BYTE) return 1;
            /* stray byte (old handshake retry, line noise, etc) - ignore, keep waiting */
        }
        if (get_ticks() - last_activity > READY_LINK_SILENCE_TIMEOUT) {
            net_log("team exchange: link silent too long waiting for peer");
            return 0;
        }
        blink++;
        if (blink % 9 == 0) {
            print_text(11, 5, (blink / 9) % 2 ? "(waiting on the other player if needed)   " :
                                                  "(waiting on the other player if needed) . ", 7);
        }
    }
}

/* host_team/client_team are the same two structs on both machines; is_host
   picks which one is "mine" for display and input, but combat is always
   resolved host-first so both sides compute identical results. */
void multiplayer_battle_scene(Team* host_team, Team* client_team, Background* bg, int port, int is_host) {
    Team *my_team, *opp_team;
    Character *pc, *ec;
    char buffer[50], buffer2[50];
    int move_idx;
    int my_choice, opp_choice;
    int next_idx;

    host_team->active = team_next_alive(host_team, 0);
    client_team->active = team_next_alive(client_team, 0);
    my_team = is_host ? host_team : client_team;
    opp_team = is_host ? client_team : host_team;

    draw_full_battle(my_team, opp_team, bg);

    while (team_alive_count(host_team) > 0 && team_alive_count(client_team) > 0) {
        pc = &my_team->members[my_team->active];
        ec = &opp_team->members[opp_team->active];

        clear_ui_text();
        sprintf(buffer, "%.15s HP:%-4ld", ec->name, ec->hp);
        print_text(1, 1, buffer, 15);
        sprintf(buffer, "%.15s HP:%-4ld", pc->name, pc->hp);
        print_text(15, 20, buffer, 15);
        print_text(19, 2, "Your move:", 14);

        my_choice = -1;
        while (my_choice < 0) {
            move_idx = select_move(pc);
            if (move_idx >= 0) my_choice = move_idx;
        }

        clear_ui_text();
        print_text(19, 2, "Waiting for opponent...", 14);

        net_send_int(port, my_choice);
        if (!net_recv_int(port, &opp_choice, MOVE_WAIT_TIMEOUT)) {
            print_text(22, 2, "Connection lost!", 12); getch(); return;
        }

        clear_ui_text();
        if (is_host) {
            resolve_move(&host_team->members[host_team->active], &host_team->members[host_team->active].moves[my_choice],
                         &client_team->members[client_team->active], buffer, buffer2);
        } else {
            resolve_move(&host_team->members[host_team->active], &host_team->members[host_team->active].moves[opp_choice],
                         &client_team->members[client_team->active], buffer, buffer2);
        }
        print_text(20, 2, buffer, 15); print_text(21, 2, buffer2, 15);
        print_text(22, 2, "Press Key...", 14); getch();

        if (client_team->members[client_team->active].hp <= 0 && team_alive_count(client_team) > 0) {
            if (is_host) {
                if (!net_recv_int(port, &next_idx, MOVE_WAIT_TIMEOUT)) {
                    print_text(22, 2, "Connection lost!", 12); getch(); return;
                }
            } else {
                next_idx = choose_next_character(client_team, "Your fighter fainted!");
                net_send_int(port, next_idx);
            }
            client_team->active = next_idx;
            clear_ui_text();
            sprintf(buffer, "%.15s sent out!", client_team->members[client_team->active].name);
            print_text(20, 2, buffer, 14);
            print_text(22, 2, "Press Key...", 14); getch();
        }

        if (team_alive_count(client_team) > 0) {
            clear_ui_text();
            if (is_host) {
                resolve_move(&client_team->members[client_team->active], &client_team->members[client_team->active].moves[opp_choice],
                             &host_team->members[host_team->active], buffer, buffer2);
            } else {
                resolve_move(&client_team->members[client_team->active], &client_team->members[client_team->active].moves[my_choice],
                             &host_team->members[host_team->active], buffer, buffer2);
            }
            print_text(20, 2, buffer, 12); print_text(21, 2, buffer2, 12);
            print_text(22, 2, "Press Key...", 14); getch();

            if (host_team->members[host_team->active].hp <= 0 && team_alive_count(host_team) > 0) {
                if (is_host) {
                    next_idx = choose_next_character(host_team, "Your fighter fainted!");
                    net_send_int(port, next_idx);
                } else {
                    if (!net_recv_int(port, &next_idx, MOVE_WAIT_TIMEOUT)) {
                        print_text(22, 2, "Connection lost!", 12); getch(); return;
                    }
                }
                host_team->active = next_idx;
                clear_ui_text();
                sprintf(buffer, "%.15s sent out!", host_team->members[host_team->active].name);
                print_text(20, 2, buffer, 14);
                print_text(22, 2, "Press Key...", 14); getch();
            }
        }

        draw_full_battle(my_team, opp_team, bg);
    }

    clear_ui_text();
    sprintf(buffer, "%s Wins!", team_alive_count(my_team) > 0 ? "You" : "Opponent");
    print_text(20, 4, buffer, 14);
    print_text(22, 4, "Press any key...", 7);
    getch();
}

/* Handles the whole "Multiplayer" flow: pick Host/Join, pick a COM port,
   handshake over the wire, exchange team data, then run the battle. */
void multiplayer_menu(char roster[][64], char display_names[][16], int roster_count) {
    int key, sel, port, done;
    int is_host;
    unsigned char hb;
    static Team host_team, client_team;
    static int my_idx[MAX_TEAM];
    int my_count;
    Background bg;
    char buffer[64];
    int i;
    long handshake_tries;

    if (roster_count == 0) {
        draw_rect(0, 0, 320, 200, 0);
        print_text(10, 5, "Error: No .chr files found!", 12); getch(); return;
    }

    /* Host or Join */
    sel = 1; done = 0;
    while (!done) {
        draw_rect(0, 0, 320, 200, 0);
        print_text(4, 8, "--- MULTIPLAYER (COM PORT) ---", 14);
        print_text(8, 10, "1. Host Game", sel == 1 ? 15 : 7);
        print_text(10, 10, "2. Join Game", sel == 2 ? 15 : 7);
        print_text(12, 10, "3. Back", sel == 3 ? 15 : 7);
        key = read_key();
        if (key == '1') sel = 1;
        if (key == '2') sel = 2;
        if (key == '3') sel = 3;
        if (key == KEY_UP) { sel--; if (sel < 1) sel = 3; }
        if (key == KEY_DOWN) { sel++; if (sel > 3) sel = 1; }
        if (key == KEY_ESC) return;
        if (key == KEY_ENTER) done = 1;
    }
    if (sel == 3) return;
    is_host = (sel == 1);

    /* Choose COM port */
    port = 1; done = 0;
    while (!done) {
        draw_rect(0, 0, 320, 200, 0);
        print_text(6, 8, "Which COM port? (null-modem cable)", 14);
        sprintf(buffer, "1. COM1"); print_text(9, 10, buffer, port == 1 ? 15 : 7);
        sprintf(buffer, "2. COM2"); print_text(11, 10, buffer, port == 2 ? 15 : 7);
        print_text(14, 10, "ENTER: continue  ESC: back", 7);
        key = read_key();
        if (key == '1') port = 1;
        if (key == '2') port = 2;
        if (key == KEY_UP || key == KEY_DOWN) port = (port == 1) ? 2 : 1;
        if (key == KEY_ESC) return;
        if (key == KEY_ENTER) done = 1;
    }

    serial_init(port, 9600);
    serial_drain_rx(port);
    net_log(is_host ? "multiplayer_menu: role=HOST" : "multiplayer_menu: role=CLIENT");

    /* Handshake: keep exchanging a marker byte until both sides see it,
       ESC cancels. This also drains any garbage left on the line. */
    draw_rect(0, 0, 320, 200, 0);
    print_text(10, 5, is_host ? "Waiting for other player..." : "Connecting to host...", 14);
    print_text(12, 5, "(ESC to cancel)", 7);
    handshake_tries = 0;
    for (;;) {
        if (kbhit()) { if (getch() == KEY_ESC) { net_log("handshake: cancelled by user"); serial_close(port); return; } }
        serial_send_byte(port, 0xAA);
        if (serial_recv_byte(port, &hb, 1) && hb == 0xAA) break;
        handshake_tries++;
        if (handshake_tries % 200 == 0) {
            sprintf(buffer, "handshake: still trying (%ld attempts, no 0xAA echoed back)", handshake_tries);
            net_log(buffer);
        }
    }
    sprintf(buffer, "handshake: connected after %ld attempts", handshake_tries);
    net_log(buffer);
    print_text(14, 5, "Connected!", 10);
    print_text(15, 5, "Press any key...", 7); getch();

    /* Pick my own team locally */
    my_count = select_team(display_names, roster_count, my_idx, "SELECT YOUR TEAM (up to 4)");
    if (my_count == 0) { net_log("multiplayer_menu: no team picked, aborting"); serial_close(port); return; }

    if (is_host) {
        host_team.count = my_count;
        for (i = 0; i < my_count; i++) load_character(roster[my_idx[i]], &host_team.members[i]);
    } else {
        client_team.count = my_count;
        for (i = 0; i < my_count; i++) load_character(roster[my_idx[i]], &client_team.members[i]);
    }

    draw_rect(0, 0, 320, 200, 0);
    print_text(10, 5, "Exchanging team data...", 14);
    print_text(11, 5, "(waiting on the other player if needed)", 7);
    print_text(13, 5, "(ESC to cancel)", 7);
    net_log("multiplayer_menu: starting team exchange");

    /* Don't let either side start streaming its team until BOTH sides have
       actually finished picking and are here listening - this is what used
       to race: whoever picked faster would dump its team while the other
       was still on the select-team screen, which could desync the link and
       show "Connection lost" even though nothing was really wrong yet. */
    if (!wait_for_peer_ready(port)) {
        print_text(12, 5, "Connection cancelled.", 12); getch(); serial_close(port); return;
    }

    {
        int attempt, ok;
        long recv_timeout;

        for (attempt = 1; attempt <= TEAM_SEND_MAX_RETRIES; attempt++) {
            sprintf(buffer, "team exchange: attempt %d of %d", attempt, TEAM_SEND_MAX_RETRIES);
            net_log(buffer);
            /* Give it plenty of time on the first try (still nothing but a
               link problem could explain a stall here now), then shorter
               once we know we might need to just retry the framed send. */
            recv_timeout = (attempt == 1) ? TEAM_EXCHANGE_TIMEOUT : 10L * BIOS_TICKS_TIMEOUT;

            /* Fixed order avoids both sides trying to send at once: host sends first. */
            if (is_host) {
                net_log("team exchange: sending host team");
                net_send_team(port, &host_team);
                net_log("team exchange: waiting to receive client team");
                ok = net_recv_team(port, &client_team, recv_timeout);
            } else {
                net_log("team exchange: waiting to receive host team");
                ok = net_recv_team(port, &host_team, recv_timeout);
                if (ok == 1) {
                    net_log("team exchange: sending client team");
                    net_send_team(port, &client_team);
                }
            }

            if (ok == 1) break;

            if (ok == 0) {
                net_log("team exchange: TIMED OUT / link problem");
                print_text(12, 5, "Connection lost.", 12); getch(); serial_close(port); return;
            }

            /* ok == -1: bytes arrived scrambled/out of sync - resync and retry
               rather than giving up outright. */
            net_log("team exchange: retrying after scrambled data");
            print_text(12, 5, "Hiccup - retrying...       ", 12);
            if (attempt == TEAM_SEND_MAX_RETRIES) {
                print_text(12, 5, "Connection lost.", 12); getch(); serial_close(port); return;
            }
            if (!wait_for_peer_ready(port)) {
                print_text(12, 5, "Connection cancelled.", 12); getch(); serial_close(port); return;
            }
        }
    }
    load_core_bg(0, &bg);
    net_log("team exchange: complete, entering battle");

    multiplayer_battle_scene(&host_team, &client_team, &bg, port, is_host);

    for (i = 0; i < host_team.count; i++) if (host_team.members[i].sprite_data) _ffree(host_team.members[i].sprite_data);
    for (i = 0; i < client_team.count; i++) if (client_team.members[i].sprite_data) _ffree(client_team.members[i].sprite_data);
    serial_close(port);
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

    /* If the player saved a loadout last time, use it instead of the defaults above */
    load_loadout(p1_idx, &p1_count, p2_idx, &p2_count, &bg_sel, roster_count);
    if (roster_count > 0 && p1_count == 0) { p1_idx[0] = 0; p1_count = 1; }
    if (roster_count > 0 && p2_count == 0) { p2_idx[0] = (roster_count > 1) ? 1 : 0; p2_count = 1; }

    set_video_mode(0x13);

    while (running) {
        draw_rect(0, 0, 320, 200, 0);
        print_text(5, 14, "DOSTCG", 14);
        print_text(11, 12, "1. Play Game", selection == 1 ? 15 : 7);
        print_text(13, 12, "2. Multiplayer (COM)", selection == 2 ? 15 : 7);
        print_text(15, 12, "3. Customize", selection == 3 ? 15 : 7);
        print_text(17, 12, "4. Exit", selection == 4 ? 15 : 7);

        key = read_key();
        if (key == '1') selection = 1;
        if (key == '2') selection = 2;
        if (key == '3') selection = 3;
        if (key == '4') selection = 4;
        if (key == KEY_UP) { selection--; if (selection < 1) selection = 4; }
        if (key == KEY_DOWN) { selection++; if (selection > 4) selection = 1; }

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
                multiplayer_menu(roster, display_names, roster_count);
            }
            else if (selection == 3) {
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
                        save_loadout(p1_idx, p1_count, p2_idx, p2_count, bg_sel);
                    }

                    if (key == KEY_ESC) {
                        save_loadout(p1_idx, p1_count, p2_idx, p2_count, bg_sel);
                    }
                }
            }
            else if (selection == 4) { running = 0; }
        }
    }
    set_video_mode(0x03);
    return 0;
}