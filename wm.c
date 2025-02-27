#include <X11/X.h>
#include <X11/XF86keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <unistd.h>

#define MOD Mod4Mask

#define get_win_size(W, gx, gy, gw, gh)                                \
  XGetGeometry(d, W, &(Window){0}, gx, gy, gw, gh, &(unsigned int){0}, \
               &(unsigned int){0})

#define mod_clean(mask)                                                   \
  (mask & ~(numlock | LockMask) &                                         \
   (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask | \
    Mod5Mask))

typedef struct vec2 {
  int x;
  int y;
} vec2;

typedef struct client {
  vec2 pos;
  Window win;

  /* needed for linked list */
  struct client* next;
} client;

typedef union {
  const char** command;
  vec2 dest;
  Window window;
} args;

struct key {
  unsigned int mod;
  KeySym keysym;
  void (*function)(const args arg);
  args arg;
};

enum layout { Tiled, Focused };

typedef enum cell_state {
  PRIMARY_ONLY,
  SECONDARY_ONLY,
  BOTH_CLIENTS,
  NO_CLIENTS
} cell_state;

typedef enum direction {
  UP = 40,
  DOWN = 41,
  LEFT = 42,
  RIGHT = 43,
} direction;

typedef struct cell {
  enum layout mode;

  client* primary;
  client* secondary;
} cell;

/* keybindinds */
void run(const args arg);
void kill_client(const args arg);
void swap_clients();
void pocket_action();
void refresh_cell();
void map_cell(vec2 n_pos);
void go_to_cell(const args arg);
void move_to_cell(const args arg);
void update_cell_layout(vec2 n_pos);
inline cell_state get_cell_state(cell* c);

struct key keys[] = {
    {.mod = MOD,
     .keysym = XK_Return,
     .function = run,
     .arg = (args){.command = (const char*[]){"alacritty", NULL}}},
    {.mod = MOD,
     .keysym = XK_slash,
     .function = run,
     .arg = (args){.command = (const char*[]){"dmenu_run", NULL}}},
    {.mod = MOD,
     .keysym = XK_Up,
     .function = go_to_cell,
     .arg = (args){.dest = (vec2){UP, 0}}},
    {.mod = MOD,
     .keysym = XK_Down,
     .function = go_to_cell,
     .arg = (args){.dest = (vec2){DOWN, 0}}},

    {.mod = MOD | ShiftMask,
     .keysym = XK_k,
     .function = kill_client,
     .arg = (args){.window = 0}},
    {.mod = MOD,
     .keysym = XK_k,
     .function = kill_client,
     .arg = (args){.window = 1}},

    {.mod = MOD, .keysym = XK_o, .function = swap_clients, .arg = (args){}},
    {.mod = MOD | ShiftMask,
     .keysym = XK_o,
     .function = refresh_cell,
     .arg = (args){}},
     
    {.mod = MOD, .keysym = XK_p, .function = pocket_action, .arg = (args){}},

    /* i love hard coding values */
    {MOD, XK_1, go_to_cell, (args){.dest = (vec2){0, 0}}},
    {MOD, XK_2, go_to_cell, (args){.dest = (vec2){1, 0}}},
    {MOD, XK_3, go_to_cell, (args){.dest = (vec2){2, 0}}},
    {MOD, XK_4, go_to_cell, (args){.dest = (vec2){3, 0}}},
    {MOD, XK_5, go_to_cell, (args){.dest = (vec2){4, 0}}},
    {MOD, XK_6, go_to_cell, (args){.dest = (vec2){5, 0}}},
    {MOD, XK_7, go_to_cell, (args){.dest = (vec2){6, 0}}},
    {MOD, XK_8, go_to_cell, (args){.dest = (vec2){7, 0}}},
    {MOD, XK_9, go_to_cell, (args){.dest = (vec2){8, 0}}},

    {MOD | ShiftMask, XK_1, move_to_cell, (args){.dest = (vec2){0, 0}}},
    {MOD | ShiftMask, XK_2, move_to_cell, (args){.dest = (vec2){1, 0}}},
    {MOD | ShiftMask, XK_3, move_to_cell, (args){.dest = (vec2){2, 0}}},
    {MOD | ShiftMask, XK_4, move_to_cell, (args){.dest = (vec2){3, 0}}},
    {MOD | ShiftMask, XK_5, move_to_cell, (args){.dest = (vec2){4, 0}}},
    {MOD | ShiftMask, XK_6, move_to_cell, (args){.dest = (vec2){5, 0}}},
    {MOD | ShiftMask, XK_7, move_to_cell, (args){.dest = (vec2){6, 0}}},
    {MOD | ShiftMask, XK_8, move_to_cell, (args){.dest = (vec2){7, 0}}},
    {MOD | ShiftMask, XK_9, move_to_cell, (args){.dest = (vec2){8, 0}}},
};

static Display* display;
static XButtonEvent mouse;
static Window root;

#define ROWS 9
#define COLS 9

static cell cell_grid[COLS][ROWS];
static client* client_ll = NULL;
static client* focused_client = NULL;
static client* pocket = NULL;

vec2 pos = {.x = 0, .y = 0};

static int screen_w = 0, screen_h = 0;
static int numlock = 0;

static int xerror() { return 0; }

void grab_keybindings(Window root) {
  unsigned int i, j, modifiers[] = {0, LockMask, numlock, numlock | LockMask};
  XModifierKeymap* modmap = XGetModifierMapping(display);
  KeyCode code;

  /* Iterate over the modifier keymap to find the Num Lock key */
  for (i = 0; i < 8; i++)
    for (int k = 0; k < modmap->max_keypermod; k++)
      if (modmap->modifiermap[i * modmap->max_keypermod + k] ==
          XKeysymToKeycode(display, 0xff7f))
        numlock = (1 << i);

  XUngrabKey(display, AnyKey, AnyModifier, root);

  /* Iterate over the keys to grab */
  for (i = 0; i < sizeof(keys) / sizeof(*keys); i++)
    if ((code = XKeysymToKeycode(display, keys[i].keysym)))
      for (j = 0; j < sizeof(modifiers) / sizeof(*modifiers); j++)
        XGrabKey(display, code, keys[i].mod | modifiers[j], root, True,
                 GrabModeAsync, GrabModeAsync);

  /* Iterate over the mouse buttons to grab (1 & 3) */
  for (i = 1; i < 4; i += 2)
    for (j = 0; j < sizeof(modifiers) / sizeof(*modifiers); j++)
      XGrabButton(display, i, MOD | modifiers[j], root, True,
                  ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                  GrabModeAsync, GrabModeAsync, 0, 0);

  XFreeModifiermap(modmap);
}

void run(const args arg) {
  if (fork()) return;
  if (display) close(ConnectionNumber(display));

  setsid();
  execvp((char*)arg.command[0], (char**)arg.command);
}

void kill_client(const args arg) {
  /* we must kill ourselves */
  if (arg.window == 0) {
    exit(-1738);
  }
  if (focused_client) XKillClient(display, focused_client->win);
}

void swap_clients() {
  client* tmp = NULL;
  cell* current_cell = &cell_grid[pos.x][pos.y];
  switch (get_cell_state(current_cell)) {
    case BOTH_CLIENTS:
      tmp = current_cell->primary;
      current_cell->primary = current_cell->secondary;
      current_cell->secondary = tmp;
      break;
    default:
      return;
  }
  update_cell_layout(pos);
  map_cell(pos);
}

void pocket_action() {
  /* pick up if pocket is empty */
  if (focused_client && !(pocket)) {
    pocket = focused_client;
    XUnmapWindow(display, focused_client->win);
    if (cell_grid[pos.x][pos.y].primary == focused_client)
      cell_grid[pos.x][pos.y].primary = NULL;
    else if (cell_grid[pos.x][pos.y].secondary == focused_client)
      cell_grid[pos.x][pos.y].secondary = NULL;
  } else if (pocket) {
    switch (get_cell_state(&cell_grid[pos.x][pos.y])) {
      case NO_CLIENTS:
      case SECONDARY_ONLY:
        cell_grid[pos.x][pos.y].primary = pocket;
        break;
      case PRIMARY_ONLY:
        cell_grid[pos.x][pos.y].secondary = pocket;
        break;
      case BOTH_CLIENTS:
        return;
    }
    pocket = NULL;
  }

  /* assume this happens on our current cell */
  update_cell_layout(pos);
  map_cell(pos);
}

void refresh_cell() {
  update_cell_layout(pos);
  map_cell(pos);
}

inline cell_state get_cell_state(cell* c) {
  if (c->primary && c->secondary) return BOTH_CLIENTS;
  if (c->primary) return PRIMARY_ONLY;
  if (c->secondary) return SECONDARY_ONLY;
  return NO_CLIENTS;
}

void client_focus(client* c) {
  focused_client = c;
  XSetInputFocus(display, c->win, RevertToParent, CurrentTime);
}

void delete_client(client* c) {
  if (c == NULL) {
    return;
  }

  if (client_ll == c) {
    client_ll = c->next;
  } else {
    client* p = client_ll;
    while (p->next != NULL && p->next != c) {
      p = p->next;
    }
    if (p->next == c) {
      p->next = c->next;
    }
  }
  free(c);
}

void map_cell(vec2 n_pos) {
  cell* current;
  cell* previous;

  if ((n_pos.x == pos.x) && (n_pos.y == pos.y)) {
    current = previous = &cell_grid[pos.x][pos.y];
  } else {
    current = &cell_grid[n_pos.x][n_pos.y];
    previous = &cell_grid[pos.x][pos.y];
  };

  /*
  if (!((prevy == current_cell_y) && (prevx == current_cell_x))) {
    previous_cell_y = prevy;
    previous_cell_x = prevx;
  }
  */
  if (previous->primary != NULL) XUnmapWindow(display, previous->primary->win);
  if (previous->secondary != NULL)
    XUnmapWindow(display, previous->secondary->win);

  if (current->mode == Tiled) {
    if (current->primary != NULL) XMapWindow(display, current->primary->win);
    if (current->secondary != NULL)
      XMapWindow(display, current->secondary->win);
  } else {
    if (current->primary != NULL) {
      XMapWindow(display, current->primary->win);
      return; /* don't render both! */
    } else if (current->secondary != NULL)
      XMapWindow(display, current->secondary->win);
  }
}

void update_cell_layout(vec2 n_pos) {
  cell* current_cell = &cell_grid[n_pos.x][n_pos.y];

  if (current_cell->mode == Tiled) {
    switch (get_cell_state(current_cell)) {
      case PRIMARY_ONLY:
        XMoveResizeWindow(display, current_cell->primary->win, 0, 0, screen_w,
                          screen_h);
        break;
      case SECONDARY_ONLY:
        XMoveResizeWindow(display, current_cell->secondary->win, 0, 0, screen_w,
                          screen_h);
        break;
      case BOTH_CLIENTS:
        XMoveResizeWindow(display, current_cell->primary->win, 0, 0,
                          screen_w / 2, screen_h);
        XMoveResizeWindow(display, current_cell->secondary->win, screen_w / 2,
                          0, screen_w / 2, screen_h);
        break;
      case NO_CLIENTS:
        break;
    }
  } else { /* 'Fullscreen' view */
    switch (get_cell_state(current_cell)) {
      case PRIMARY_ONLY:
      case BOTH_CLIENTS:
        XMoveResizeWindow(display, current_cell->primary->win, 0, 0, screen_w,
                          screen_h);
        break;
      case SECONDARY_ONLY:
        XMoveResizeWindow(display, current_cell->secondary->win, 0, 0, screen_w,
                          screen_h);
        break;
      case NO_CLIENTS:
        break;
    }
  }
}

void go_to_cell(const args arg) {
  int tmp_y;
  tmp_y = pos.y;
  switch (arg.dest.x) {
    case UP:
      tmp_y -= 1;
      if (tmp_y >= ROWS) {
        tmp_y %= ROWS;
      } else if (tmp_y < 0) {
        tmp_y = (ROWS + (tmp_y % ROWS)) % ROWS;
      }
      update_cell_layout((vec2){pos.x, tmp_y});
      map_cell((vec2){pos.x, tmp_y});
      pos.y = tmp_y;
      break;
    case DOWN:
      tmp_y += 1;
      if (tmp_y >= ROWS) {
        tmp_y %= ROWS;
      } else if (tmp_y < 0) {
        tmp_y = (ROWS + (tmp_y % ROWS)) % ROWS;
      }
      update_cell_layout((vec2){pos.x, tmp_y});
      map_cell((vec2){pos.x, tmp_y});
      pos.y = tmp_y;
      break;
    default:
      update_cell_layout((vec2){arg.dest.x, pos.y});
      map_cell((vec2){arg.dest.x, pos.y});
      pos.x = arg.dest.x;
  }
}

void move_to_cell(const args arg) {
  /* MOD + Shift + # */
  /* get destination */
  cell* current;
  cell* dest_cell;

  if (focused_client == NULL) return;

  if ((arg.dest.x == pos.x) && (arg.dest.y == pos.y)) return;

  current = &cell_grid[pos.x][pos.y];
  dest_cell = &cell_grid[arg.dest.x][pos.y];

  switch (get_cell_state(dest_cell)) {
    case BOTH_CLIENTS:
      return;
    case PRIMARY_ONLY:
      dest_cell->secondary = focused_client;
      update_cell_layout(arg.dest);
      break;

    case SECONDARY_ONLY:
    case NO_CLIENTS:
      dest_cell->primary = focused_client;
      update_cell_layout(arg.dest);
      break;
  }

  if (current->primary == focused_client) current->primary = NULL;
  if (current->secondary == focused_client) current->secondary = NULL;

  XUnmapWindow(display, focused_client->win);

  /* we just moved a client. re-render */
  update_cell_layout(pos);
  map_cell(pos);
}

/* event-loop functions */

void button_press(XEvent* e) {
  /* we could probably do something smart here, but for
    now, we'll just chill. */
  if (e) return;
}

void button_release(XEvent* e) {
  /* reset to root window */
  if (e) mouse.subwindow = 0;
}

/* just a simple pass-through function */
void configure_request(XEvent* e) {
  XConfigureRequestEvent* ev = &e->xconfigurerequest;

  XConfigureWindow(display, ev->window, ev->value_mask,
                   &(XWindowChanges){.x = ev->x,
                                     .y = ev->y,
                                     .width = ev->width,
                                     .height = ev->height,
                                     .sibling = ev->above,
                                     .stack_mode = ev->detail});
}

void key_press(XEvent* e) {
  KeySym keysym = XkbKeycodeToKeysym(display, e->xkey.keycode, 0, 0);

  for (unsigned int i = 0; i < sizeof(keys) / sizeof(*keys); ++i)
    if (keys[i].keysym == keysym &&
        mod_clean(keys[i].mod) == mod_clean(e->xkey.state))
      keys[i].function(keys[i].arg);
}

void map_request(XEvent* e) {
  client* iter;
  int found = 0;
  for (iter = client_ll; iter != NULL; iter = iter->next) {
    if (iter->win == e->xmaprequest.window) {
      found = 1;
      break;
    }
  }

  if (!found) {
    int home_needed = 0;
    int x = 0;
    int y = 0;

    /* If current cell is open, select for dest */
    switch (get_cell_state(&cell_grid[pos.x][pos.y])) {
      case BOTH_CLIENTS:
        home_needed = 1;
        break;
      case PRIMARY_ONLY:
      case SECONDARY_ONLY:
      case NO_CLIENTS:
        x = pos.x;
        y = pos.y;
        break;
    }

    /* otherwise, loop through all cells and find a free one */
    if (home_needed) {
      int found = 0;
      for (y = pos.y; y < ROWS; y++) {
        for (x = (y == pos.y ? pos.x : 0); x < COLS; x++) {
          switch (get_cell_state(&cell_grid[x][y])) {
            case BOTH_CLIENTS:
              break;
            case PRIMARY_ONLY:
            case NO_CLIENTS:
            case SECONDARY_ONLY:
              found = 1;
              break;
          }
          if (found) break;
        }
        if (found) break;
      }
    }

    /* we just got told there is a new window, add to linked list of
      clients if new */

    client* new_client = (client*)malloc(sizeof(client));
    if (new_client == NULL) exit(-11);

    new_client->win = e->xmaprequest.window;

    if (client_ll == NULL) {
      client_ll = new_client;
      new_client->next = NULL;
    } else {
      new_client->next = client_ll;
      client_ll = new_client;
    }

    XSelectInput(display, e->xmaprequest.window,
                 StructureNotifyMask | EnterWindowMask);

    new_client->pos.x = x;
    new_client->pos.y = y;

    cell* home_cell = &cell_grid[x][y];
    /* Find the slot */
    if (home_cell->primary == NULL) {
      home_cell->primary = new_client;
    } else if (home_cell->secondary == NULL) {
      home_cell->secondary = new_client;
    } else {
      /* blow up */
    }
  }

  update_cell_layout(pos);
  map_cell(pos);
}

/* our keyboard layout changed, re-grab keys */
void notify_mapping(XEvent* e) {
  XMappingEvent* ev = &e->xmapping;

  if (ev->request == MappingKeyboard || ev->request == MappingModifier) {
    XRefreshKeyboardMapping(ev);
    grab_keybindings(root);
  }
}

/* good bye, cruel world! */
void notify_destroy(XEvent* e) {
  client* victim;
  for (victim = client_ll; victim != NULL; victim = victim->next) {
    if (victim->win == e->xdestroywindow.window) {
      break;
    }
  }

  if (victim == NULL) return;

  /* remove from grid */
  if (victim == cell_grid[victim->pos.x][victim->pos.y].primary)
    cell_grid[victim->pos.x][victim->pos.y].primary = NULL;
  if (victim == cell_grid[victim->pos.x][victim->pos.y].secondary)
    cell_grid[victim->pos.x][victim->pos.y].secondary = NULL;

  delete_client(victim);

  /* assume this happens on our current cell */
  update_cell_layout(pos);
  map_cell(pos);
}

void notify_enter(XEvent* e) {
  while (XCheckTypedEvent(display, EnterNotify, e))
    ;
  client* tmp = client_ll;
  while (tmp != NULL && tmp->win != e->xcrossing.window) tmp = tmp->next;
  if (tmp != NULL) client_focus(tmp);
}

void notify_motion(XEvent* e) {
  while (XCheckTypedEvent(display, MotionNotify, e))
    ;
}

static void (*events[LASTEvent])(XEvent* e) = {
    [ButtonPress] = button_press,
    [ButtonRelease] = button_release,
    [KeyPress] = key_press,

    [MapRequest] = map_request,
    [DestroyNotify] = notify_destroy,

    /* dumb logic */
    [MappingNotify] = notify_mapping,
    [ConfigureRequest] = configure_request,

    /* for active window tracking */
    [EnterNotify] = notify_enter,
    [MotionNotify] = notify_motion};

int main(void) {
  XEvent ev;

  if (!(display = XOpenDisplay(0))) exit(1);

  signal(SIGCHLD, SIG_IGN);
  XSetErrorHandler(xerror);

  int screen = DefaultScreen(display);
  root = RootWindow(display, screen);
  screen_w = XDisplayWidth(display, screen);
  screen_h = XDisplayHeight(display, screen);

  XSelectInput(display, root, SubstructureRedirectMask);
  XDefineCursor(display, root, XCreateFontCursor(display, 68));
  grab_keybindings(root);

  while (1 && !XNextEvent(display, &ev))  // 1 && will forever be here.
    if (events[ev.type]) events[ev.type](&ev);
}
