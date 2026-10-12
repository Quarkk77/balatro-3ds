/* event.c - port of engine/event.lua */
#include "../game/game.h"

typedef VEC(Event *) EventQueue;
static EventQueue queues[Q_COUNT];
static double queue_timer, queue_dt = 1.0 / 60, queue_last_processed;
/* Lua iterates `pairs(self.queues)`; the order of the hash part is not
 * defined, but in practice it is stable. We process in a fixed order. */
static const QueueKind QUEUE_ORDER[Q_COUNT] = { Q_UNLOCK, Q_BASE, Q_TUTORIAL, Q_ACHIEVEMENT, Q_OTHER };

double timer_value(TimerKind k) { return G.TIMERS.arr[k]; }

Event *event_new(const EventDef *d) {
    Event *e = NEW(Event);
    e->trigger = d->trigger;
    e->blocking = d->blocking < 0 ? true : d->blocking != 0;
    e->blockable = d->blockable < 0 ? true : d->blockable != 0;
    e->complete = false;
    e->start_timer = d->start_timer;
    e->func = d->func;
    e->ctx = d->ctx;
    e->ctx_free = d->ctx_free ? d->ctx_free : (d->ctx ? free : NULL);
    e->delay = d->delay;
    e->no_delete = d->no_delete;
    e->created_on_pause = d->pause_force || G.SETTINGS.paused;
    e->timer = d->timer >= 0 ? (TimerKind)d->timer : (e->created_on_pause ? TIMER_REAL : TIMER_TOTAL);
    if (e->trigger == TRIG_EASE) {
        e->ease.type = d->ease;
        e->ease.ref = d->ref;
        e->ease.start_val = ref_num(d->ref);
        e->ease.end_val = d->ease_to;
        e->ease.started = false;
        e->ease.func = d->ease_func;
    }
    if (e->trigger == TRIG_CONDITION) {
        e->condition.ref = d->ref;
        e->condition.stop_val = d->stop_val;
        e->condition.stop_bool = d->stop_bool;
        e->condition.is_bool = d->cond_is_bool;
    }
    e->time = G.TIMERS.arr[e->timer];
    return e;
}

static bool call_func(Event *e) {
    if (e->func) return e->func(e, e->ctx);
    return true;
}

typedef struct { bool blocking, completed, time_done, pause_skip; } EventResults;

static double ease_apply(Event *e, double v) { return e->ease.func ? e->ease.func(v, e->ctx) : v; }

static void event_handle(Event *e, EventResults *r) {
    r->blocking = e->blocking;
    r->completed = e->complete;
    if (e->created_on_pause == false && G.SETTINGS.paused) { r->pause_skip = true; return; }
    double now = G.TIMERS.arr[e->timer];
    if (!e->start_timer) { e->time = now; e->start_timer = true; }
    if (e->trigger == TRIG_AFTER) {
        if (e->time + e->delay <= now) {
            r->time_done = true;
            r->completed = call_func(e);
        }
    }
    if (e->trigger == TRIG_EASE) {
        if (!e->ease.started) {
            e->ease.started = true;
            e->ease.start_time = now;
            e->ease.end_time = now + e->delay;
            e->ease.start_val = ref_num(e->ease.ref);
        }
        if (!e->complete) {
            if (e->ease.end_time >= now) {
                double pd = (e->ease.end_time - now) / (e->ease.end_time - e->ease.start_time);
                if (e->ease.end_time - e->ease.start_time == 0) pd = 0;
                if (e->ease.type == EASE_ELASTIC) pd = -pow(2, 10 * pd - 10) * sin((pd * 10 - 10.75) * 2 * M_PI / 3);
                else if (e->ease.type == EASE_QUAD) pd = pd * pd;
                ref_set_num(e->ease.ref, ease_apply(e, pd * e->ease.start_val + (1 - pd) * e->ease.end_val));
            } else {
                ref_set_num(e->ease.ref, ease_apply(e, e->ease.end_val));
                e->complete = true;
                r->completed = true;
                r->time_done = true;
            }
        }
    }
    if (e->trigger == TRIG_CONDITION) {
        if (!e->complete) {
            if (e->func) r->completed = call_func(e);
            else if (e->condition.is_bool) r->completed = ref_truthy(e->condition.ref) == e->condition.stop_bool;
            else r->completed = ref_num(e->condition.ref) == e->condition.stop_val;
        }
        r->time_done = true;
    }
    if (e->trigger == TRIG_BEFORE) {
        if (!e->complete) r->completed = call_func(e);
        if (e->time + e->delay <= now) r->time_done = true;
    }
    if (e->trigger == TRIG_IMMEDIATE) {
        r->completed = call_func(e);
        r->time_done = true;
    }
    if (r->completed) e->complete = true;
}

/* Events cleared while the queue is being processed (e.g. an event that
 * calls delete_run) must stay valid until processing finishes. */
static VEC(Event *) dead_events;
static int processing_depth;
static void event_free_now(Event *e) {
    if (e->ctx_free && e->ctx) e->ctx_free(e->ctx);
    free(e);
}
static void event_free(Event *e) {
    if (processing_depth > 0) vec_push(&dead_events, e);
    else event_free_now(e);
}

void event_manager_init(void) {
    for (int i = 0; i < Q_COUNT; i++) vec_clear(&queues[i]);
    queue_timer = G.TIMERS.REAL;
    queue_dt = 1.0 / 60;
    queue_last_processed = G.TIMERS.REAL;
}

void add_event_q(Event *e, QueueKind q, bool front) {
    if (!e) return;
    if (front) vec_insert(&queues[q], 0, e);
    else vec_push(&queues[q], e);
}
void add_event(Event *e) { add_event_q(e, Q_BASE, false); }

static void clear_one(EventQueue *v) {
    int i = 0;
    while (i < v->count) {
        if (!v->items[i]->no_delete) {
            Event *e = v->items[i];
            vec_remove_at(v, i);
            event_free(e);
        } else i++;
    }
}
void event_clear_queue(int queue, int exception) {
    if (queue < 0 && exception < 0) {
        for (int i = 0; i < Q_COUNT; i++) clear_one(&queues[i]);
    } else if (exception >= 0) {
        for (int i = 0; i < Q_COUNT; i++) if (i != exception) clear_one(&queues[i]);
    } else clear_one(&queues[queue]);
}

void event_manager_update(double dt, bool forced) {
    processing_depth++;
    queue_timer += dt;
    if (queue_timer >= queue_last_processed + queue_dt || forced) {
        queue_last_processed += forced ? 0 : queue_dt;
        for (int qi = 0; qi < Q_COUNT; qi++) {
            EventQueue *v = &queues[QUEUE_ORDER[qi]];
            bool blocked = false;
            int i = 0;
            while (i < v->count) {
                EventResults r = { false, false, false, false };
                Event *e = v->items[i];
                if (!blocked || !e->blockable) event_handle(e, &r);
                if (r.pause_skip) i++;
                else {
                    if (!blocked && r.blocking) blocked = true;
                    if (r.completed && r.time_done) {
                        /* the event may have changed queue contents; find it again */
                        int idx = (i < v->count && v->items[i] == e) ? i : -1;
                        if (idx < 0) for (int j = 0; j < v->count; j++) if (v->items[j] == e) { idx = j; break; }
                        if (idx >= 0) { vec_remove_at(v, idx); event_free(e); }
                        else i++;
                    } else i++;
                }
            }
        }
    }
    processing_depth--;
    if (processing_depth == 0) {
        for (int i = 0; i < dead_events.count; i++) event_free_now(dead_events.items[i]);
        vec_clear(&dead_events);
    }
}

int event_queue_length(QueueKind q) { return (q >= 0 && q < Q_COUNT) ? queues[q].count : 0; }
