/* main program of Free Software for Calypso Phone */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <debug.h>
#include <memory.h>
#include <delay.h>
#include <rffe.h>
#include <keypad.h>
#include <board.h>
#include <abb/twl3025.h>
#include <rf/trf6151.h>
#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>
#include <comm/sercomm.h>
#include <comm/timer.h>
#include <fb/framebuffer.h>
#include <battery/battery.h>

/* Main Program */
const char *hr = "======================================================================\n";

/*
 *  -- Good info --
 *  Screen size: 96x64  (Nokia 3310 resolution: 84×48)
 *
 */

#define GAME_PIXEL_X_MIN 3
#define GAME_PIXEL_Y_MIN 12
#define GAME_PIXEL_X_SIZE 90
#define GAME_PIXEL_Y_SIZE 48

#define SNAKE_PART_SIZE 3
#define SNAKE_INIT_LENGTH 3
#define GAME_X_SIZE (GAME_PIXEL_X_SIZE/SNAKE_PART_SIZE)
#define GAME_Y_SIZE (GAME_PIXEL_Y_SIZE/SNAKE_PART_SIZE)
#define SNAKE_MAX_LENGTH GAME_X_SIZE*GAME_Y_SIZE

typedef enum { MENU = 0, GAME, GAMEOVER } game_state_t;
typedef enum { UP = 0, RIGHT, DOWN, LEFT } direction_t;
typedef enum { SNAKE = 0, MOUSE } drawtype_t;

typedef struct {
  int8_t x, y;
} position_t;

typedef struct {
  position_t parts[SNAKE_MAX_LENGTH];
  direction_t direction;
  uint16_t head;
  uint16_t tail;
} snake_t;

snake_t snake;
position_t mouse;
direction_t new_dir;
game_state_t game_state;

/*
 *  MENU
 */

void enter_menu_screen();
void menu_key_handler(enum key_codes, enum key_states);
void print_menu_screen();

/*
 *  GAME
 */

void enter_game_screen();
void game_key_handler(enum key_codes, enum key_states);
void print_game_screen();
//
static struct osmo_timer_list game_loop_timer;
void game_loop(void *);
//
void print_game_part(uint8_t, uint8_t, drawtype_t);
void clear_game_part(uint8_t, uint8_t);
// Snake
void snake_init();
void snake_step(direction_t);
int snake_collides(position_t);
void mouse_spawn();


/*
 *  RANDOM GENERATOR
 */

static struct osmo_timer_list random_seed_timer;
void random_seed(void *);
//
uint32_t get_rand();
position_t get_random_position();

/*
 *  GAME OVER
 */

void enter_gameover_screen();
void gameover_key_handler(enum key_codes, enum key_states);
void print_gameover_screen();


/*
 *  MENU  MENU  MENU  MENU  MENU  MENU  MENU  MENU
 */

void enter_menu_screen() {
  game_state = MENU;

  // Setup keys and screen
  keypad_set_handler(&menu_key_handler);
  print_menu_screen();
}

void menu_key_handler(enum key_codes code, enum key_states state) {
  if (state != PRESSED) return;

  switch (code) {
  case KEY_LEFT_SB:
    enter_game_screen();
  default:
    break;
  }
}

void print_menu_screen() {
  fb_clear();

  fb_setfg(FB_COLOR_BLACK);
  fb_setbg(FB_COLOR_WHITE);
  fb_setfont(FB_FONT_HELVB14);
  fb_gotoxy(24,35);
  fb_putstr("SNAKE", 96);

  fb_setfg(FB_COLOR_BLACK);
  fb_setbg(FB_COLOR_WHITE);
  fb_setfont(FB_FONT_HELVR08);
  fb_gotoxy(3,59);
  fb_putstr("PLAY", 96);

  fb_setbg(FB_COLOR_TRANSP);
  fb_gotoxy(1,51);
  fb_boxto(29,62);

  fb_flush();
}

/*
 *  GAME  GAME  GAME  GAME  GAME  GAME  GAME  GAME
 */

void enter_game_screen() {
  game_state = GAME;

  // Setup keys and screen
  keypad_set_handler(&game_key_handler);
  print_game_screen();

  // Setup game
  snake_init();
  mouse_spawn();

  // Start game
  osmo_timer_schedule(&game_loop_timer,100);
}

void game_key_handler(enum key_codes code, enum key_states state) {
  if (state != PRESSED) return;

  if (code == KEY_LEFT_SB) {
    enter_menu_screen();
  } else if (code == KEY_UP || code == KEY_2) {
    new_dir = UP;
  } else if (code == KEY_LEFT || code == KEY_4) {
    new_dir = LEFT;
  } else if (code == KEY_RIGHT || code == KEY_6) {
    new_dir = RIGHT;
  } else if (code == KEY_DOWN || code == KEY_8) {
    new_dir = DOWN;
  }
}

void print_game_screen() {
  fb_clear();

  /* Divider line */
  fb_setfg(FB_COLOR_BLACK);
  fb_setbg(FB_COLOR_TRANSP);
  fb_gotoxy(1, 8);
  fb_lineto(94,8);

  /* Box */
  fb_gotoxy(1, 10);
  fb_boxto(94, 62);

  /* Score */
  fb_setfont(FB_FONT_HELVR08);
  fb_gotoxy(0,6);
  fb_putstr("1234567890", 96);

  fb_flush();
}

/* Timer that fires the game loop regularly */
static struct osmo_timer_list game_loop_timer = {
  .cb = &game_loop,
  .data = &game_loop_timer
};

int collided = 0;

void game_loop(void *p) {
  if (game_state != GAME) return;

  snake_step(new_dir);

  fb_gotoxy(95,63);
  fb_lineto(95,63);
  fb_flush();

  if (collided) {
    mouse_spawn();
  }

  if (game_state == GAME) {
    osmo_timer_schedule((struct osmo_timer_list*)p, 100);
  } else if (game_state == GAMEOVER) {
    enter_gameover_screen();
  }
}

void snake_step(direction_t dir) {
  // Inverting direction not allowed
  if ( (snake.direction == UP && dir == DOWN)
     || (snake.direction == RIGHT && dir == LEFT)
     || (snake.direction == DOWN && dir == UP)
     || (snake.direction == LEFT && dir == RIGHT) ) {
    dir = snake.direction;
  } else {
    snake.direction = dir;
  }

  /*
   *  TAIL
   */

  uint16_t next_tail = snake.tail;
  if (! collided) {
    ++next_tail;
    if (next_tail >= SNAKE_MAX_LENGTH) {
      next_tail = 0;
    }
    clear_game_part(snake.parts[snake.tail].x, snake.parts[snake.tail].y);
    snake.tail = next_tail;
  }

  /*
   * HEAD
   */

  uint16_t next_head = snake.head + 1;
  if (next_head >= SNAKE_MAX_LENGTH) {
    next_head = 0;
  }
  if (next_head == snake.tail) {
    game_state = GAMEOVER;
  }

  // Copy current head
  snake.parts[next_head] = snake.parts[snake.head];

  // Move head in correct direction
  if (dir == UP) {
    snake.parts[next_head].y -= 1;
  } else if (dir == RIGHT) {
    snake.parts[next_head].x += 1;
  } else if (dir == DOWN) {
    snake.parts[next_head].y += 1;
  } else if (dir == LEFT) {
    snake.parts[next_head].x -= 1;
  }

  // Collission with wall
  if ( (snake.parts[next_head].x < 0)
     || (snake.parts[next_head].x >= GAME_X_SIZE)
     || (snake.parts[next_head].y < 0)
     || (snake.parts[next_head].y >= GAME_Y_SIZE) ) {
    game_state = GAMEOVER;
  }

  // Collission with mouse
  if (snake.parts[next_head].x == mouse.x
    && snake.parts[next_head].y == mouse.y) {
    collided = 1;
  } else {
    collided = 0;
  }

  // Move head forward
  snake.head = next_head;

  // Print new head
  if (game_state == GAME) {
    print_game_part(snake.parts[next_head].x, snake.parts[next_head].y, SNAKE);
  }
}

void print_game_part(uint8_t x, uint8_t y, drawtype_t type) {
  if (x > GAME_X_SIZE || y > GAME_Y_SIZE) {
    puts("Error: Out of bounds.");
    return;
  }

  uint16_t xpos = GAME_PIXEL_X_MIN + (x * SNAKE_PART_SIZE);
  uint16_t ypos = GAME_PIXEL_Y_MIN + (y * SNAKE_PART_SIZE);

  fb_setfg(FB_COLOR_BLACK);
  fb_setbg(FB_COLOR_BLACK);

  if (type == SNAKE) {
    fb_gotoxy(xpos, ypos);
    fb_boxto(xpos+SNAKE_PART_SIZE-1, ypos+SNAKE_PART_SIZE-1);
  } else if (type == MOUSE) {
    // Only for 3x3
    fb_gotoxy(xpos+1, ypos);
    fb_lineto(xpos+1, ypos);

    fb_gotoxy(xpos, ypos+1);
    fb_lineto(xpos, ypos+1);

    fb_gotoxy(xpos+2, ypos+1);
    fb_lineto(xpos+2, ypos+1);

    fb_gotoxy(xpos+1, ypos+2);
    fb_lineto(xpos+1, ypos+2);
  }
}

void clear_game_part(uint8_t x, uint8_t y) {
  if (x > GAME_X_SIZE || y > GAME_Y_SIZE) {
    puts("Error: Out of bounds.");
    return;
  }

  uint16_t xpos = GAME_PIXEL_X_MIN + (x * SNAKE_PART_SIZE);
  uint16_t ypos = GAME_PIXEL_Y_MIN + (y * SNAKE_PART_SIZE);

  fb_setfg(FB_COLOR_WHITE);
  fb_setbg(FB_COLOR_WHITE);
  fb_gotoxy(xpos, ypos);
  fb_boxto(xpos+SNAKE_PART_SIZE-1, ypos+SNAKE_PART_SIZE-1);
}

void snake_init() {
  new_dir = UP;
  snake.direction = UP;
  snake.tail = 0;
  snake.head = SNAKE_INIT_LENGTH-1;

  int x = GAME_X_SIZE/2;
  int y = GAME_Y_SIZE/2;

  int i;
  for (i = 0; i < SNAKE_INIT_LENGTH; ++i) {
    snake.parts[i].x = x;
    snake.parts[i].y = y;
    --y;
  }

  for (i = 1; i < SNAKE_INIT_LENGTH; ++i) {
    print_game_part(snake.parts[i].x, snake.parts[i].y, SNAKE);
  }
}

void mouse_spawn() {
  mouse = get_random_position();

  do {
    ++mouse.x;
    if (mouse.x >= GAME_X_SIZE) {
      mouse.x = 0;
      ++mouse.y;
      if (mouse.y >= GAME_Y_SIZE) {
        mouse.y = 0;
      }
    }
  } while (snake_collides(mouse));

  print_game_part(mouse.x, mouse.y, MOUSE);
}

int snake_collides(position_t pos) {
  int i = snake.tail;
  while (1) {
    // check collission
    if (snake.parts[i].x == pos.x && snake.parts[i].y == pos.y) {
      return 1;
    }

    // Next
    if (i == snake.head) {
      break;
    }
    ++i;
    if (i >= SNAKE_MAX_LENGTH) {
      i = 0;
    }
  }
  return 0;
}

/*
 *  RANDOM GENERATOR  RANDOM GENERATOR  RANDOM GENERATOR
 */

// LFSR seeds
uint16_t lfsr_seed = 0xBABEu;
uint32_t lfsr_bit;

// xor128 PRNG seeds
uint32_t xor128_x = 123456789;
uint32_t xor128_y = 362436069;
uint32_t xor128_z = 521288629;
uint32_t xor128_w = 88675123;

static struct osmo_timer_list random_seed_timer = {
  .cb = &random_seed,
  .data = &random_seed_timer
};

void random_seed(void *p) {
  lfsr_bit  = ((lfsr_seed >> 0) ^ (lfsr_seed >> 2) ^ (lfsr_seed >> 3) ^ (lfsr_seed >> 5) ) & 1;
  lfsr_seed =  (lfsr_seed >> 1) | (lfsr_bit << 15);

  osmo_timer_schedule((struct osmo_timer_list*)p, 100);
}

uint32_t get_rand() {
  uint32_t t;
  xor128_x ^= lfsr_seed;
  t = xor128_x ^ (xor128_x << 11);
  xor128_x = xor128_y; xor128_y = xor128_z; xor128_z = xor128_w;
  return xor128_w = xor128_w ^ (xor128_w >> 19) ^ (t ^ (t >> 8));
}

position_t get_random_position() {
  position_t pos;
  pos.x = get_rand() % GAME_X_SIZE;
  pos.y = get_rand() % GAME_Y_SIZE;
  return pos;
}

/*
 *  GAME OVER GAME OVER GAME OVER GAME OVER
 */

void enter_gameover_screen() {
  game_state = GAMEOVER;

  // Setup keys and screen
  keypad_set_handler(&gameover_key_handler);
  print_gameover_screen();
}

void gameover_key_handler(enum key_codes code, enum key_states state) {
  if (state != PRESSED) return;

  switch (code) {
  case KEY_LEFT_SB:
    enter_menu_screen();
  default:
    break;
  }
}

void print_gameover_screen() {
  fb_setfg(FB_COLOR_BLACK);
  fb_setbg(FB_COLOR_WHITE);
  fb_setfont(FB_FONT_HELVB14);
  fb_gotoxy(3,35);
  fb_putstr("GAME OVER", 96);

  fb_setfg(FB_COLOR_BLACK);
  fb_setbg(FB_COLOR_WHITE);
  fb_setfont(FB_FONT_HELVR08);
  fb_gotoxy(3,59);
  fb_putstr("EXIT", 96);

  fb_setbg(FB_COLOR_TRANSP);
  fb_gotoxy(1,51);
  fb_boxto(24,62);

  fb_flush();
}

/*
 *  MAIN  MAIN  MAIN  MAIN  MAIN  MAIN  MAIN
 */

int main(void)
{
  board_init(1);

  /* Game started */
  puts("\n\n< Snake >\n");

  /* Initaiate "random generator" */
  osmo_timer_schedule(&random_seed_timer, 100);

  /* Splash screen */
  enter_menu_screen();

  /* Beyond this point we only react to interrupts */
  puts("Entering interrupt loop\n");
  while (1) {
    osmo_timers_update();
  }

  twl3025_power_off();

  while (1) {}
}
