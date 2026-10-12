/* engine.h - the game's engine layer (port of engine/*.lua).
 *
 * Object model: every game object starts with a `Node` struct. The Lua
 * classes Node and Moveable are merged into this base (every concrete object
 * in the game is a Moveable except G.ROOM). Subclasses embed Node as their
 * first member and provide a vtable for the methods Lua dispatched
 * dynamically. */
#ifndef ENGINE_H
#define ENGINE_H

#include "../core/core.h"
#include "../core/dval.h"
#include "../gfx/gfx.h"

typedef struct Node Node;
typedef struct NodeVT NodeVT;
typedef struct UIBox UIBox;
typedef struct UIElement UIElement;
typedef struct UIDef UIDef;
typedef struct UIConfig UIConfig;
typedef struct UIBoxConfig UIBoxConfig;
typedef struct Sprite Sprite;
typedef struct DynaText DynaText;
typedef struct Particles Particles;
typedef struct Event Event;
typedef struct Card Card;
typedef struct CardArea CardArea;
typedef struct Blind Blind;
typedef struct CardCharacter CardCharacter;
typedef struct CardUITable CardUITableFwd;

/* Colours are shared mutable 4-float arrays, exactly like Lua tables. */
typedef float *Col;

/* ------------------------------------------------------------------ */
/* Reflection-style references (Lua's ref_table[ref_value])            */
/* ------------------------------------------------------------------ */
typedef enum {
    REF_NONE = 0,
    REF_NUM,      /* double *            */
    REF_INT,      /* int *               */
    REF_STR,      /* char ** (heap str)  */
    REF_CSTR,     /* const char **       */
    REF_BOOL,     /* bool *              */
    REF_NODE,     /* Node **             */
    REF_COL,      /* Col * (pointer to a colour pointer) */
    REF_COLV,     /* float * (the colour itself) */
    REF_BUF,      /* char[] buffer (the string itself) */
    REF_FLT,      /* float * (e.g. one colour component) */
} RefKind;
typedef struct { RefKind kind; void *p; } Ref;
#define REFN(ptr) ((Ref){ REF_NUM, (void *)(ptr) })
#define REFI(ptr) ((Ref){ REF_INT, (void *)(ptr) })
#define REFS(ptr) ((Ref){ REF_STR, (void *)(ptr) })
#define REFCS(ptr) ((Ref){ REF_CSTR, (void *)(ptr) })
#define REFB(ptr) ((Ref){ REF_BOOL, (void *)(ptr) })
#define REFNODE(ptr) ((Ref){ REF_NODE, (void *)(ptr) })
#define REFCOL(ptr) ((Ref){ REF_COL, (void *)(ptr) })
#define REFCOLV(ptr) ((Ref){ REF_COLV, (void *)(ptr) })
#define REFBUF(ptr) ((Ref){ REF_BUF, (void *)(ptr) })
#define REFF(ptr) ((Ref){ REF_FLT, (void *)(ptr) })
bool ref_valid(Ref r);
Ref ref_new_str(const char *init);   /* a fresh heap slot ({val = init}) */
double ref_num(Ref r);
void ref_set_num(Ref r, double v);
const char *ref_tostring(Ref r);     /* Lua tostring() of the referenced value (temp string) */
bool ref_truthy(Ref r);
Node *ref_node(Ref r);
float *ref_col(Ref r);

/* ------------------------------------------------------------------ */
/* Basic node data                                                     */
/* ------------------------------------------------------------------ */
typedef struct { double x, y, w, h, r, scale; } Transform;
typedef struct { double x, y; } Vec2;
typedef struct { bool can, is; } StatePair;
typedef struct {
    bool visible;
    StatePair collide, focus, hover, click, drag, release_on;
} NodeStates;

typedef enum {
    NT_NODE, NT_MOVEABLE, NT_SPRITE, NT_ANIMATEDSPRITE, NT_UIBOX, NT_UIELEMENT,
    NT_DYNATEXT, NT_PARTICLES, NT_CARD, NT_CARDAREA, NT_BLIND, NT_CARDCHAR, NT_COUNT
} NodeType;

typedef enum { ROLE_MAJOR = 0, ROLE_MINOR, ROLE_GLUED } RoleType;
typedef enum { BOND_STRONG = 0, BOND_WEAK } Bond;

typedef struct {
    RoleType role_type;
    Vec2 offset;
    Node *major;
    Node *draw_major;
    Bond xy_bond, wh_bond, r_bond, scale_bond;
} Role;

/* arguments for set_role (Lua passes a partial table; unset fields keep their value) */
typedef struct {
    int role_type;          /* -1 = keep */
    bool has_offset; Vec2 offset;
    Node *major;
    Node *draw_major;
    int xy_bond, wh_bond, r_bond, scale_bond; /* -1 = keep */
} RoleArgs;
#define ROLE_ARGS(...) ((RoleArgs){ .role_type = -1, .xy_bond = -1, .wh_bond = -1, .r_bond = -1, .scale_bond = -1, __VA_ARGS__ })

typedef struct {
    char type[8];
    Vec2 offset;
    char prev_type[8];
    Vec2 prev_offset;
    bool lr_clamp;
    struct { bool a, m, c, b, t, l, r, i; } tl;
    double offset_py; bool has_offset_py;   /* Lua alignment.offset.py (saved y) */
} Alignment;

typedef struct {
    Node *major;
    const char *type;       /* NULL = keep */
    int bond;               /* xy bond, -1 = Weak default when major supplied */
    int wh_bond, r_bond, scale_bond; /* -1 = keep */
    bool has_offset; Vec2 offset;
    bool lr_clamp;
} AlignArgs;
#define ALIGN_ARGS(...) ((AlignArgs){ .bond = -1, .wh_bond = -1, .r_bond = -1, .scale_bond = -1, __VA_ARGS__ })

typedef struct { bool x, y; } Pinch;
typedef struct {
    bool active;
    double scale, scale_amt, r, r_amt, start_time, end_time;
    bool handled_elsewhere;
} Juice;
typedef struct { Node *major; Vec2 offset; } MajorInfo;

/* Named / indexed children (Lua: self.children table) */
typedef struct { const char *name; Node *node; } ChildEntry;
typedef VEC(ChildEntry) Children;

struct NodeVT {
    NodeType type;
    const NodeVT *super;
    const char *name;
    void (*draw)(Node *);
    void (*update)(Node *, double dt);
    void (*move)(Node *, double dt);
    void (*remove)(Node *);
    void (*hover)(Node *);
    void (*stop_hover)(Node *);
    void (*click)(Node *);
    void (*release)(Node *, Node *dragged);
    void (*drag)(Node *, Vec2 *offset);
    void (*stop_drag)(Node *);
    Node *(*can_drag)(Node *);
    bool (*collides_with_point)(Node *, Vec2 point);
    void (*put_focused_cursor)(Node *, double *x, double *y);
    void (*set_container)(Node *, Node *container);
    void (*animate)(Node *);
    void (*juice_up)(Node *, double amount, double rot_amt); /* rot_amt: NAN = nil */
    void (*draw_self)(Node *);
    void (*draw_children)(Node *);
    void (*recalculate)(Node *);
    void (*hard_set_T)(Node *, double x, double y, double w, double h);
    void (*align)(Node *, double x, double y);
    void (*set_alignments)(Node *);
    void (*set_wh)(Node *, double *w, double *h, bool *ok);
    void (*initialize_VT)(Node *);
};

struct Node {
    const NodeVT *vt;
    int ID;
    Transform *T;              /* may alias another node's transform (glued sprites) */
    Transform own_T;
    Transform *CT;             /* collision transform (T or VT) */
    Vec2 click_offset, hover_offset;
    bool created_on_pause;
    struct { int DRAW, MOVE; } FRAME;
    MajorInfo frame_major; bool frame_major_valid;
    MajorInfo self_major;
    NodeStates states;
    Node *container;
    Children children;
    bool REMOVED;
    bool under_overlay;
    Node *parent;
    double click_timeout;      /* 0 = none */
    double DEBUG_VALUE_unused;

    /* "config" fields that Node itself reads */
    UIDef *h_popup;
    UIBoxConfig *h_popup_config;
    UIDef *d_popup;
    UIBoxConfig *d_popup_config;
    bool refresh_movement;

    /* --- Moveable --- */
    Transform VT;
    struct { double x, y, r, scale, mag; } velocity;
    Role role;
    Alignment alignment;
    Pinch *pinch; Pinch own_pinch;
    double last_moved, last_aligned;
    bool static_rotation;
    Vec2 offset;
    Node *Mid;
    Vec2 *shadow_parrallax; Vec2 own_shadow_parrallax;
    Vec2 layered_parallax;
    double shadow_height;
    Juice juice;
    bool zoom;
    bool STATIONARY;
    bool NEW_ALIGNMENT;
    bool CALCING;
    bool is_moveable;
    Vec2 *temp_offs_unused;
    /* drawing helpers shared by many subclasses */
    struct { double mx, my, dx, dy, amt; bool set; } tilt_var;
    double hover_tilt;
    double ambient_tilt;
    double dissolve;
    Col dissolve_colours[4]; int n_dissolve_colours;
    double mouse_damping;      /* 0 = nil (1) */
    bool attention_text;       /* UIBox flag used by Game:draw */
    bool ui_object_updated;    /* object inside a UIT.O element changed size */
    bool non_recalc;
    void *user;                /* arbitrary game data */
    /* per-instance method overrides (Lua assigned functions on instances) */
    void (*o_hover)(Node *);
    void (*o_stop_hover)(Node *);
    bool cfg_force_focus;      /* node.config.force_focus on non-UI nodes */
    void *cfg_tag;             /* node.config.tag (tag sprites) */
    CardUITableFwd *ability_UIBox_table;
};

/* type checks */
bool node_is(const Node *n, NodeType t);
#define IS(n, t) ((n) && node_is((const Node *)(n), (t)))

/* children helpers */
Node *child_get(Node *n, const char *name);
void child_set(Node *n, const char *name, Node *c);    /* NULL removes */
void child_push(Node *n, Node *c);
void child_remove_node(Node *n, Node *c);
int child_count(Node *n);

/* ------------------------------------------------------------------ */
/* Node / Moveable API                                                 */
/* ------------------------------------------------------------------ */
extern const NodeVT NODE_VT;
extern const NodeVT MOVEABLE_VT;

typedef struct { double x, y, w, h, r, scale; bool has_r, has_scale; Node *container; } NodeArgs;

void node_init(Node *n, const NodeVT *vt, double x, double y, double w, double h, Node *container);
Node *node_new(double x, double y, double w, double h);          /* plain Node (G.ROOM) */
void moveable_init(Node *n, const NodeVT *vt, double x, double y, double w, double h);
Node *moveable_new(double x, double y, double w, double h);

/* virtual dispatch helpers */
static inline void node_draw(Node *n) { if (n && n->vt->draw) n->vt->draw(n); }
static inline void node_update(Node *n, double dt) { if (n && n->vt->update) n->vt->update(n, dt); }
static inline void node_move(Node *n, double dt) { if (n && n->vt->move) n->vt->move(n, dt); }
void node_remove(Node *n);
static inline void node_hover(Node *n) { if (!n) return; if (n->o_hover) n->o_hover(n); else if (n->vt->hover) n->vt->hover(n); }
static inline void node_stop_hover(Node *n) { if (!n) return; if (n->o_stop_hover) n->o_stop_hover(n); else if (n->vt->stop_hover) n->vt->stop_hover(n); }
static inline void node_click(Node *n) { if (n && n->vt->click) n->vt->click(n); }
static inline void node_release(Node *n, Node *d) { if (n && n->vt->release) n->vt->release(n, d); }
static inline void node_drag(Node *n, Vec2 *off) { if (n && n->vt->drag) n->vt->drag(n, off); }
static inline void node_stop_drag(Node *n) { if (n && n->vt->stop_drag) n->vt->stop_drag(n); }
static inline Node *node_can_drag(Node *n) { return (n && n->vt->can_drag) ? n->vt->can_drag(n) : NULL; }
static inline bool node_collides(Node *n, Vec2 p) { return n && n->vt->collides_with_point && n->vt->collides_with_point(n, p); }
static inline void node_put_focused_cursor(Node *n, double *x, double *y) { n->vt->put_focused_cursor(n, x, y); }
static inline void node_set_container(Node *n, Node *c) { if (n && n->vt->set_container) n->vt->set_container(n, c); }
static inline void node_animate(Node *n) { if (n && n->vt->animate) n->vt->animate(n); }
static inline void node_juice_up(Node *n, double a, double r) { if (n && n->vt->juice_up) n->vt->juice_up(n, a, r); }
static inline void node_hard_set_T(Node *n, double x, double y, double w, double h) { if (n && n->vt->hard_set_T) n->vt->hard_set_T(n, x, y, w, h); }

/* base implementations (callable from subclasses, like Node.draw(self)) */
void Node_draw(Node *n);
void Node_draw_boundingrect(Node *n);
bool Node_collides_with_point(Node *n, Vec2 point);
void Node_set_offset(Node *n, Vec2 point, bool click);
void Node_drag(Node *n, Vec2 *off);
Node *Node_can_drag(Node *n);
void Node_stop_drag(Node *n);
void Node_hover(Node *n);
void Node_stop_hover(Node *n);
void Node_put_focused_cursor(Node *n, double *x, double *y);
void Node_set_container(Node *n, Node *container);
void Node_translate_container(Node *n);
void Node_remove(Node *n);
double Node_fast_mid_dist(Node *a, Node *b);

void Moveable_draw(Node *n);
void Moveable_set_alignment(Node *n, AlignArgs a);
void Moveable_align_to_major(Node *n);
void Moveable_hard_set_T(Node *n, double x, double y, double w, double h);
void Moveable_hard_set_VT(Node *n);
void Moveable_drag(Node *n, Vec2 *offset);
void Moveable_juice_up(Node *n, double amount, double rot_amt);
void Moveable_move_juice(Node *n, double dt);
void Moveable_move(Node *n, double dt);
void Moveable_lr_clamp(Node *n);
void Moveable_glue_to_major(Node *n, Node *major);
void Moveable_move_with_major(Node *n, double dt);
void Moveable_move_xy(Node *n, double dt);
void Moveable_move_scale(Node *n, double dt);
void Moveable_move_wh(Node *n, double dt);
void Moveable_move_r(Node *n, double dt, double vel_x);
void Moveable_calculate_parrallax(Node *n);
void Moveable_set_role(Node *n, RoleArgs a);
MajorInfo *Moveable_get_major(Node *n);
void Moveable_remove(Node *n);
#define JUICE_NIL NAN

/* graveyard: removed nodes are freed later (when no events can reference them) */
void node_free_graveyard(void);

/* ------------------------------------------------------------------ */
/* Sprites                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    char *name;
    GfxImage *image;
    double px, py;
    int frames;            /* animation atlas only */
    bool animation;
} Atlas;

typedef struct { double x, y; int v; } SpritePos;   /* v > 0 = random column */

typedef struct ShaderSend {
    const char *name;
    int kind;              /* 0 float, 1 vec2, 2 vec4, 3 bool, 4 vec2 from double[2] ptr, 5 sprite pos pixel (ptr = Sprite*) */
    float val[4];
    void *ptr;
    Ref ref;               /* if set, value read from ref (num or colour) */
    double (*func)(void);  /* if set, float value from function */
} ShaderSend;

typedef struct DrawStep {
    const char *shader;
    double shadow_height; bool has_shadow_height;
    ShaderSend *send; int nsend;   /* "send" list for custom shaders */
    bool no_tilt;
    Node *other_obj;
    double ms, mr, mx, my;
} DrawStep;

struct Sprite {
    Node n;
    Atlas *atlas;
    Vec2 scale;
    double scale_mag;
    SpritePos sprite_pos, sprite_pos_copy;
    GfxQuad sprite;
    double image_dims[2];
    DrawStep *draw_steps; int n_draw_steps;
    void (*custom_draw)(Sprite *s);   /* replaces Sprite:draw (e.g. stake sprites) */
    float send_to_shader[4];
    bool video;
    /* AnimatedSprite */
    struct { double x, y; int frames, current; double w, h; } animation;
    struct { int current, frames; double w, h; } current_animation;
    double frame_offset;
    double offset_seconds;
    bool float_anim;
    double pixel_offset_unused;
    bool hovering;            /* per-instance hover overrides (collection screens) */
    void *cfg_blind;          /* BlindProto* for blind collection sprites */
};

extern const NodeVT SPRITE_VT;
extern const NodeVT ANIMATEDSPRITE_VT;
Sprite *sprite_new(double x, double y, double w, double h, Atlas *atlas, SpritePos pos);
Sprite *animated_sprite_new(double x, double y, double w, double h, Atlas *atlas, SpritePos pos);
void sprite_init(Sprite *s, const NodeVT *vt, double x, double y, double w, double h, Atlas *atlas, SpritePos pos);
void Sprite_reset(Sprite *s);
void Sprite_set_sprite_pos(Sprite *s, SpritePos pos);
void Sprite_define_draw_steps(Sprite *s, const DrawStep *steps, int n);
void Sprite_draw_shader(Sprite *s, const char *shader, double shadow_height, bool has_shadow, const float *send, int nsend,
                        bool no_tilt, Node *other_obj, double ms, double mr, double mx, double my, bool custom_shader,
                        double tilt_shadow, const ShaderSend *sends, int nsends);
void Sprite_draw_self(Sprite *s, const float *overlay);
void Sprite_draw(Node *n);
void Sprite_draw_from(Sprite *s, Node *other, double ms, double mr, double mx, double my);
void Sprite_get_pos_pixel(Sprite *s, float out[4]);
void AnimatedSprite_set_sprite_pos(Sprite *s, SpritePos pos);
void AnimatedSprite_rescale(Sprite *s);
#define SPOS(X, Y) ((SpritePos){ (X), (Y), 0 })

/* convenience: draw a sprite through a named shader with a vec2 "send" */
void Sprite_draw_shader_simple(Sprite *s, const char *shader, double shadow_height, bool has_shadow);
void Sprite_draw_shader_send(Sprite *s, const char *shader, const float *send, int nsend);
void Sprite_draw_shader_ex(Sprite *s, const char *shader, double shadow_height, bool has_shadow, const float *send, int nsend,
                           bool no_tilt, Node *other_obj, double ms, double mr, double mx, double my);

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */
typedef enum { TRIG_IMMEDIATE = 0, TRIG_AFTER, TRIG_BEFORE, TRIG_EASE, TRIG_CONDITION } EventTrigger;
typedef enum { TIMER_TOTAL = 0, TIMER_REAL, TIMER_REAL_SHADER, TIMER_UPTIME, TIMER_BACKGROUND, TIMER_COUNT } TimerKind;
typedef enum { EASE_LERP = 0, EASE_ELASTIC, EASE_QUAD } EaseType;
typedef enum { Q_BASE = 0, Q_UNLOCK, Q_TUTORIAL, Q_ACHIEVEMENT, Q_OTHER, Q_COUNT } QueueKind;

typedef bool (*EventFn)(Event *e, void *ctx);
typedef double (*EaseFn)(double v, void *ctx);

typedef struct {
    EventTrigger trigger;
    int blocking;          /* -1 = default (true) */
    int blockable;         /* -1 = default (true) */
    bool start_timer;
    EventFn func;
    void *ctx;
    void (*ctx_free)(void *);
    double delay;
    bool no_delete;
    bool pause_force;
    int timer;             /* -1 = default */
    /* ease */
    EaseType ease;
    Ref ref;
    double ease_to;
    EaseFn ease_func;
    /* condition */
    double stop_val;
    bool stop_bool; bool cond_is_bool;
} EventDef;

struct Event {
    EventTrigger trigger;
    bool blocking, blockable, complete, start_timer, no_delete, created_on_pause;
    EventFn func;
    void *ctx;
    void (*ctx_free)(void *);
    double delay;
    TimerKind timer;
    double time;
    struct { EaseType type; Ref ref; double start_val, end_val, start_time, end_time; bool started; EaseFn func; } ease;
    struct { Ref ref; double stop_val; bool stop_bool, is_bool; } condition;
};

#define EVENT(...) event_new(&(EventDef){ .blocking = -1, .blockable = -1, .timer = -1, __VA_ARGS__ })
Event *event_new(const EventDef *def);
void add_event(Event *e);                               /* base queue, back */
void add_event_q(Event *e, QueueKind q, bool front);
void event_clear_queue(int queue /* -1 all */, int exception /* -1 none */);
void event_manager_init(void);
int event_queue_length(QueueKind q);
void event_manager_update(double dt, bool forced);
double timer_value(TimerKind k);

/* allocate a closure context; freed with the event */
#define CTX(T) ((T *)xcalloc(1, sizeof(T)))

/* ------------------------------------------------------------------ */
/* UI                                                                  */
/* ------------------------------------------------------------------ */
enum { UIT_T = 1, UIT_B = 2, UIT_C = 3, UIT_R = 4, UIT_O = 5, UIT_ROOT = 7, UIT_S = 8, UIT_I = 9 };
#define UIT_PADDING 0.0

typedef void (*UIFunc)(UIElement *e);

typedef struct {
    const char *button;     /* gamepad button name for registry */
    bool snap_to;
    const char *type;       /* 'none', 'cycle', 'tab', 'slider' ... */
    const char *nav;        /* 'wide', 'tall' */
    UIElement *funnel_to, *funnel_from;
    bool funnel_to_set, funnel_from_set;
    bool set_button_pip;
    const char *orientation;
    Vec2 offset; bool has_offset;
    double scale;
    bool registered;
    bool no_loop;
    UIElement *funnel_from_node;
} FocusArgs;
#define FOCUS(...) focus_dup(&(FocusArgs){ __VA_ARGS__ })
FocusArgs *focus_dup(const FocusArgs *f);

typedef struct {
    Ref ref;
    double max;
    Col empty_col, filled_col;
} ProgressBar;

struct UIConfig {
    /* layout */
    const char *align;
    double padding;          /* nil -> UIT_PADDING */
    double minw, minh, maxw, maxh;     /* maxw/maxh: 0 = nil */
    double w, h;             /* NAN = nil */
    double r;                /* NAN = nil */
    double res;              /* 0 = nil */
    double emboss;           /* 0 = nil */
    double line_emboss;
    double outline;          /* NAN = nil */
    double ext_up;
    bool no_fill;
    bool vert;
    bool mid;
    int draw_layer;          /* 0 = nil */
    bool draw_after;
    /* look */
    Col colour, outline_colour, shadow_colour;
    bool shadow;
    double scale;            /* 0 = nil */
    bool juice;
    int chosen;              /* 0 none, 1 true, 2 'vert' */
    bool choice;
    bool hover;
    /* text */
    char *text;
    Ref ref;                 /* text / object / slider binding */
    char *prev_value;        /* last stringified ref value */
    void *lang;              /* Language* (G.LANG default) */
    void *text_drawable;     /* non-NULL once prepared */
    bool no_recalc;
    /* objects */
    Node *object;
    bool no_role;
    RoleArgs *role;
    bool focus_with_object;
    /* interaction */
    UIFunc button;
    UIFunc button_temp;
    UIFunc func;
    bool insta_func;
    bool one_press;
    UIElement *button_UIE;
    double button_delay;     /* 0 = nil */
    double button_delay_start, button_delay_end, button_delay_progress;
    bool button_delay_started;
    double button_dist;      /* 0 = nil (1) */
    FocusArgs *focus_args;
    bool force_focus;
    bool force_collision;
    int can_collide;         /* 0 nil, 1 true, -1 false */
    bool collideable;
    const char *id;
    const char *group;
    /* tooltips */
    struct Tooltip *tooltip;
    struct Tooltip *on_demand_tooltip;
    void *detailed_tooltip;  /* Center* or TagProto* */
    int detailed_tooltip_kind; /* 1 = center, 2 = tag */
    ProgressBar *progress_bar;
    /* free-form data used by button callbacks (Lua stored anything here) */
    void *ref_table;
    const char *ref_value;
    void *ref_parent;
    void *data;              /* callback specific payload */
    void *data2;
    double num;              /* callback specific number */
    double num2;
    char *str;               /* callback specific string */
    int pip_state;
    bool toggle_active;
    bool disable_button_unused;
    Col pipcol, pipcol2;
    char *instance_type_unused;
    /* card related payloads */
    void *card;
    void *center;
    const char *card_key;
    const char *center_key;
    char *key;
    void *tab_definition;   /* tab callbacks */
    void *args;             /* arbitrary args table */
    const char *prev_loc_unused;
    bool filler;
    bool refresh_movement;
};

/* UIConfig.scale == 0 means nil (-> 1); Lua callbacks that set scale = 0 to
 * collapse a text node use this instead */
#define UI_SCALE_ZERO 1e-9
#define UICFG_DEFAULTS .w = NAN, .h = NAN, .r = NAN, .outline = NAN
#define CFG(...) cfg_dup(&(UIConfig){ UICFG_DEFAULTS, __VA_ARGS__ })
UIConfig *cfg_dup(const UIConfig *c);
UIConfig *cfg_new(void);

struct UIDef {
    int n;
    UIConfig *config;
    UIDef **nodes;
    int count, cap;
};

UIDef *uidef(int n, UIConfig *config, ...);       /* children, NULL terminated */
UIDef *uidef_list(int n, UIConfig *config, UIDef **children, int count);
UIDef *uidef_add(UIDef *parent, UIDef *child);    /* returns parent */
UIDef *uidef_add_all(UIDef *parent, UIDef *list); /* moves children of list into parent */
UIDef *uinodes(void);                              /* an empty list container (n = 0) */
/* placeholder for a Lua `cond and node or nil` child: skipped by uidef/uidef_add */
extern UIDef uidef_skip_sentinel;
#define UISKIP (&uidef_skip_sentinel)
#define UIIF(cond, node) ((cond) ? (node) : UISKIP)
/* (cfg, children...) - plain __VA_ARGS__ so nested UIR/UIC calls expand */
#define UIN(type, ...) uidef((type), __VA_ARGS__, (UIDef *)NULL)
#define UIROOT(...) UIN(UIT_ROOT, __VA_ARGS__)
#define UIR(...) UIN(UIT_R, __VA_ARGS__)
#define UIC(...) UIN(UIT_C, __VA_ARGS__)
#define UIT(cfg) UIN(UIT_T, cfg)
#define UIO(cfg) UIN(UIT_O, cfg)
#define UIB(cfg) UIN(UIT_B, cfg)

struct UIBoxConfig {
    const char *align;      /* or type */
    bool has_offset; Vec2 offset;
    Node *major;
    Node *parent;
    int bond;               /* -1 = Strong default */
    int xy_bond, r_bond, wh_bond, scale_bond; /* -1 = default */
    bool lr_clamp;
    int can_collide;        /* 0 nil, 1, -1 */
    const char *instance_type;
    bool no_esc;
    const char *id;
    double offset_unused;
};
#define UIBCFG(...) uibcfg_dup(&(UIBoxConfig){ .bond = -1, .xy_bond = -1, .r_bond = -1, .wh_bond = -1, .scale_bond = -1, __VA_ARGS__ })
UIBoxConfig *uibcfg_dup(const UIBoxConfig *c);

struct UIBox {
    Node n;
    UIDef *definition;
    UIBoxConfig *config;
    UIElement *UIRoot;
    PtrVec draw_layers;     /* index = draw_layer (1-based); entries may be NULL */
    /* misc flags used by game code */
    bool attention_text;
    UIElement *jimbo_unused;
    struct CardCharacter *Jimbo;
    UIBox *content;
    PtrVec highlights;
    const char *button_listen_name;
    UIFunc button_listen;
    bool no_esc;
    int joker_unlock_table;
    int step; bool step_complete, skip_steps;   /* tutorial overlay */
};

struct UIElement {
    Node n;
    UIElement *parent_uie;    /* parent element (NULL for root) */
    int UIT;
    UIBox *UIBox;
    UIConfig *config;
    struct { double w, h; } content_dimensions;
    double last_clicked;      /* -inf = nil */
    bool button_clicked;
    bool disable_button;
    double focus_timer; bool focus_timer_set;
    double object_focus_timer; bool object_focus_timer_set;
    struct {
        bool valid;
        double w, h, sw, sh, progress, parallax;
        VEC(float) fill, shadow, line, emboss, line_emboss;
    } pixellated_rect;
    struct { double sx, sy; } text_parallax;
};

extern const NodeVT UIBOX_VT;
extern const NodeVT UIELEMENT_VT;

typedef struct {
    double x, y, w, h;
    UIDef *definition;
    UIBoxConfig *config;
} UIBoxArgs;

UIBox *uibox_new(UIDef *definition, UIBoxConfig *config);
UIBox *uibox_new_T(UIDef *definition, UIBoxConfig *config, double x, double y, double w, double h);
UIElement *UIBox_get_UIE_by_ID(UIBox *b, const char *id, UIElement *node);
void UIBox_calculate_xywh(UIBox *b, UIElement *node, Transform *T, bool recalculate, double scale, double *ow, double *oh, bool *ok);
void UIBox_remove_group(UIBox *b, UIElement *node, const char *group);
void UIBox_get_group(UIBox *b, UIElement *node, const char *group, PtrVec *out);
void UIBox_set_parent_child(UIBox *b, UIDef *def, UIElement *parent);
void UIBox_recalculate(UIBox *b);
void UIBox_add_child(UIBox *b, UIDef *def, UIElement *parent);
void UIBox_set_container(Node *n, Node *container);
void UIBox_remove(Node *n);
void UIBox_draw(Node *n);
void UIBox_move(Node *n, double dt);

void UIElement_set_values(UIElement *e, Transform *T, bool recalculate);
void UIElement_update_text(UIElement *e);
void UIElement_update_object(UIElement *e);
void UIElement_draw_self(Node *n);
void UIElement_draw_children(Node *n);
void UIElement_draw_pixellated_rect(UIElement *e, const char *type, double parallax, double emboss, double progress);
void UIElement_click(Node *n);
void UIElement_hover(Node *n);
void UIElement_stop_hover(Node *n);
void UIElement_set_wh(Node *n, double *w, double *h, bool *ok);
void UIElement_set_alignments(Node *n);
void UIElement_initialize_VT(Node *n);
void UIElement_align(Node *n, double x, double y);
UIElement *uie_child(UIElement *e, int i);         /* 1-based child UIElement */
int uie_child_count(UIElement *e);
bool is_UI_containter(UIElement *e);

/* G.FUNCS registry: name -> function (for string based lookups) */
void funcs_register(const char *name, UIFunc f);
UIFunc funcs_get(const char *name);
const char *funcs_name(UIFunc f);

/* ------------------------------------------------------------------ */
/* DynaText                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    char *string;           /* literal */
    Ref ref;                /* or bound value */
    char *prefix, *suffix;
    Col colour, outer_colour;
    double scale;           /* 0 = nil */
} DynaTextPart;

typedef struct {
    char letter[8];
    double scale;
    Vec2 offset;
    Vec2 dims;
    double pop_in;
    Col prefix, suffix, colour;
    double r;
    bool r_set;
} DynaLetter;

typedef struct {
    char *string;
    VEC(DynaLetter) letters;
    double W, H, W_offset, H_offset;
    bool exists;
} DynaString;

typedef struct {
    DynaTextPart *parts; int nparts;   /* config.string */
    Col *colours; int ncolours;
    bool shadow;
    double scale;           /* 0 = nil -> 1 */
    double pop_in_rate, bump_rate, bump_amount;
    void *font;             /* Font* */
    double x_offset, y_offset;
    bool silent;
    double pop_in; bool has_pop_in;
    double pop_out; bool has_pop_out;
    double maxw;
    double pop_delay; bool has_pop_delay;
    double X, Y;
    double text_rot;
    bool rotate; int rotate_dir;   /* rotate = true or 2 */
    bool float_;
    bool bump;
    double spacing;
    bool reset_pop_in;
    double min_cycle_time; bool has_min_cycle_time;
    bool random_element;
    double pitch_shift;
    double offset_y;
    Col shadow_colour;
    bool non_recalc;
    struct { double speed, width, start, amount; bool active; } pulse;
    struct { double speed, amount; bool active; } quiver;
    double W, H;
} DynaTextConfig;

struct DynaText {
    Node n;
    DynaTextConfig config;
    bool shadow;
    double scale;
    double pop_in_rate, bump_rate, bump_amount;
    void *font;
    const char *string;
    Vec2 text_offset;
    Col *colours; int ncolours;
    double created_time;
    bool silent;
    bool start_pop_in;
    bool reset_pop_in;
    VEC(DynaString) strings;
    int focused_string;
    double pop_delay;
    double pop_out_time;
    bool pop_cycle;
};
extern const NodeVT DYNATEXT_VT;
DynaText *dynatext_new(DynaTextConfig cfg);
void DynaText_update_text(DynaText *t, bool first_pass);
void DynaText_pop_out(DynaText *t, double timer);
void DynaText_pop_in(DynaText *t, double timer);
void DynaText_align_letters(DynaText *t);
void DynaText_set_quiver(DynaText *t, double amt);
void DynaText_pulse(DynaText *t, double amt);
/* helpers to build part arrays */
DynaTextPart *dt_parts_str(const char *s);                  /* single literal string */
DynaTextPart *dt_parts_strs(const char **s, int n);
DynaTextPart dt_part_ref(Ref r);
DynaTextPart *dt_parts_ref(Ref r, const char *prefix, const char *suffix);   /* single bound part */
#define DT_REF(r) .parts = dt_parts_ref((r), NULL, NULL), .nparts = 1
#define DT_STR(s) .parts = dt_parts_str(s), .nparts = 1
#define DT_COL1(c) .colours = DT_COLS(c), .ncolours = 1
Col *dt_cols(int n, ...);
#define DT_COLS(...) dt_cols_arr((Col[]){ __VA_ARGS__ }, (int)(sizeof((Col[]){ __VA_ARGS__ }) / sizeof(Col)))
Col *dt_cols_arr(const Col *c, int n);
#define DYNATEXT(...) dynatext_new((DynaTextConfig){ __VA_ARGS__ })

/* ------------------------------------------------------------------ */
/* Particles                                                           */
/* ------------------------------------------------------------------ */
typedef struct {
    bool draw;
    double dir, facing, size, age, velocity, r_vel, e_prev, e_curr, e_vel, scale, visible_scale, time;
    bool e_vel_set;
    Col colour;
    Vec2 offset;
} Particle;

typedef struct {
    bool fill;
    double padding;
    Node *attach;
    double timer;             /* 0 = 0.5 */
    TimerKind timer_type; bool has_timer_type;
    double lifespan;          /* 0 = 1 */
    double speed;             /* 0 = 1 unless has_speed */
    bool has_speed;
    double max;               /* 0 = huge unless has_max */
    bool has_max;
    double pulse_max;
    double vel_variation; bool has_vel_variation;
    double scale;             /* 0 = 1 */
    Col *colours; int ncolours;
    bool initialize;
} ParticlesConfig;

struct Particles {
    Node n;
    bool fill;
    double padding;
    double timer;
    TimerKind timer_type;
    double last_real_time, last_drawn;
    double lifespan;
    double fade_alpha;
    double speed, max, pulse_max, pulsed, vel_variation, scale;
    VEC(Particle) particles;
    Col *colours; int ncolours;
};
extern const NodeVT PARTICLES_VT;
Particles *particles_new(double x, double y, double w, double h, ParticlesConfig cfg);
void Particles_fade(Particles *p, double delay, double to);
#define PARTICLES(X, Y, W, H, ...) particles_new((X), (Y), (W), (H), (ParticlesConfig){ __VA_ARGS__ })

/* ------------------------------------------------------------------ */
/* Controller                                                          */
/* ------------------------------------------------------------------ */
typedef struct { Node *target; bool handled; Node *prev_target; } TargetState;
typedef struct { Vec2 T; Node *target; double time; bool handled; Node *prev_target; } CursorState;

typedef struct {
    Node *node;
    bool menu;
    bool click;
} RegistryEntry;

typedef struct {
    char name[32];
    VEC(RegistryEntry) entries;
} ButtonRegistry;

typedef struct {
    Node *node;
    Vec2 cursor_pos;
    bool interrupt;
    bool valid;
} CursorContext;

typedef struct {
    TargetState clicked, focused, dragging, hovering, released_on;
    PtrVec collision_list;
    PtrVec nodes_at_cursor;
    CursorState cursor_down, cursor_up, cursor_hover;
    Node *cursor_collider;
    Vec2 cursor_position;
    StrSet pressed_keys, held_keys, released_keys;
    NumMap held_key_times;
    StrSet pressed_buttons, held_buttons, released_buttons;
    NumMap held_button_times;
    struct { bool focus; bool stack; } interrupt;
    struct { bool wipe, frame, frame_set, load, no_space, chosen_card, unused[4]; StrSet other; } locks;
    bool locked;
    struct { char current[16], previous[16]; } axis_buttons[4]; /* l_stick, r_stick, l_trig, r_trig */
    double axis_cursor_speed;
    VEC(ButtonRegistry) button_registry;
    struct { int type; Node *node; Vec2 T; bool active; } snap_cursor_to; /* type 1 node 2 transform */
    struct { int layer; VEC(CursorContext) stack; } cursor_context;
    StrMap cardarea_context;  /* name -> (rank) as intptr */
    struct { char last_type[16]; bool dpad, pointer, touch, controller, mouse, axis_cursor; } HID;
    struct { int object; bool present; char mapping[64]; char name[128]; } GAMEPAD;
    char GAMEPAD_CONSOLE[16];
    bool is_cursor_down;
    double overlay_timer;
    bool frame_buttonpress;
    bool COYOTE_FOCUS;
    bool no_holdcap;
    double repress_timer; bool repress_timer_set;
    struct { double x, y; bool set; } L_cursor_queue;
    UIElement *text_input_hook;
    bool capslock;
    UIBox *screen_keyboard;
    bool lock_input;
    double last_touch_time;
} Controller;

void controller_init(Controller *c);
void Controller_set_gamepad(Controller *c, int gamepad);
void Controller_set_HID_flags(Controller *c, const char *HID_type);
void Controller_set_cursor_position(Controller *c);
void Controller_update(Controller *c, double dt);
void Controller_cull_registry(Controller *c);
void Controller_add_to_registry(Controller *c, Node *node, const char *registry);
void Controller_process_registry(Controller *c);
void Controller_mod_cursor_context_layer(Controller *c, int delta);
void Controller_snap_to_node(Controller *c, Node *node);
void Controller_snap_to_T(Controller *c, Vec2 T);
void Controller_snap_to(Controller *c, Node *node, Vec2 *T);
bool Controller_save_cardarea_focus(Controller *c, const char *cardarea);
void Controller_recall_cardarea_focus(Controller *c, const char *cardarea);
void Controller_recall_cardarea_focus_area(Controller *c, CardArea *area);
void Controller_update_cursor(Controller *c, Vec2 *hard_set_T);
void Controller_button_press(Controller *c, const char *button);
void Controller_button_release(Controller *c, const char *button);
void Controller_key_press(Controller *c, const char *key);
void Controller_key_press_update(Controller *c, const char *key, double dt);
void Controller_key_release(Controller *c, const char *key);
void Controller_queue_L_cursor_press(Controller *c, double x, double y);
void Controller_queue_R_cursor_press(Controller *c, double x, double y);
void Controller_L_cursor_press(Controller *c, double x, double y, bool has_xy);
void Controller_L_cursor_release(Controller *c, double x, double y, bool has_xy);
bool Controller_is_node_focusable(Controller *c, Node *node);
void Controller_update_focus(Controller *c, char dir);
bool Controller_capture_focused_input(Controller *c, const char *button, const char *input_type, double dt);
void Controller_navigate_focus(Controller *c, char dir);
void Controller_get_cursor_collision(Controller *c, Transform *cursor_trans);
void Controller_set_cursor_hover(Controller *c);
void Controller_clear_target_refs(Controller *c, Node *n);

/* ------------------------------------------------------------------ */
/* Sound manager                                                       */
/* ------------------------------------------------------------------ */
void sound_init(void);
void sound_play(const char *sound_code, double per, double vol);   /* play_sound(); per/vol NAN = nil */
void sound_modulate(double dt);
void sound_stop_all(void);
void sound_reset_states(int state);
void sound_restart_music(void);

/* ------------------------------------------------------------------ */
/* Misc engine helpers                                                 */
/* ------------------------------------------------------------------ */
void prep_draw(Node *n, double scale, double rotate, Vec2 *offset, bool has_rotate);
void add_to_drawhash(Node *n);
void reset_drawhash(void);
void point_translate(Vec2 *p, Vec2 d);
void point_rotate(Vec2 *p, double angle);
double Vector_Dist(Vec2 a, Vec2 b);
void remove_all_nodes(PtrVec *v);   /* remove_all for lists of nodes */
void remove_all_children(Node *n);   /* remove_all(self.children) */
Col HEX(const char *hex);
Col col_new(float r, float g, float b, float a);
Col col_copy(const float *c);
Col mix_colours(const float *a, const float *b, double p);
Col lighten(const float *c, double p);
Col darken(const float *c, double p);
Col adjust_alpha(const float *c, double a);
void col_set(float *dst, const float *src);
void get_chosen_triangle_from_rect(double x, double y, double w, double h, bool vert, float out[6]);

#endif
