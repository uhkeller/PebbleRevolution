// Copyright (c) 2013 Douwe Maan <http://www.douwemaan.com/>
// The above copyright notice shall be included in all copies or substantial portions of the program.

// Envisioned as a watchface by Jean-Noël Mattern
// Based on the display of the Freebox Revolution, which was designed by Philippe Starck.

// Adapted for Pebble Time 2 (Emery, 200x228) by Ulrich Keller

#include <pebble.h>


// Settings
#define USE_AMERICAN_DATE_FORMAT      false
#define VIBE_ON_HOUR                  false
#define TIME_SLOT_ANIMATION_DURATION  500

// Magic numbers — Pebble Time 2 (Emery) dimensions
#define SCREEN_WIDTH        200
#define SCREEN_HEIGHT       228

// Time digit images: 2×2 grid fills the full-width square area.
// With MARGIN=1 and TIME_SLOT_SPACE=2: 2*1 + 2*98 + 2 = 200 ✓
#define TIME_IMAGE_WIDTH    98
#define TIME_IMAGE_HEIGHT   98

// Date digit images: 5-section segments, footer height = 28 px
#define DATE_IMAGE_WIDTH    25
#define DATE_IMAGE_HEIGHT   25

// Battery percentage digit images
#define BATTERY_IMAGE_WIDTH  15
#define BATTERY_IMAGE_HEIGHT 15

// Day-of-week label images (scaled 1.5× from original 20×10)
#define DAY_IMAGE_WIDTH     30
#define DAY_IMAGE_HEIGHT    15

#define MARGIN              1
#define TIME_SLOT_SPACE     2
#define DATE_PART_SPACE     4


// Images
#define NUMBER_OF_TIME_IMAGES 10
const int TIME_IMAGE_RESOURCE_IDS[NUMBER_OF_TIME_IMAGES] = {
  RESOURCE_ID_IMAGE_TIME_0,
  RESOURCE_ID_IMAGE_TIME_1, RESOURCE_ID_IMAGE_TIME_2, RESOURCE_ID_IMAGE_TIME_3,
  RESOURCE_ID_IMAGE_TIME_4, RESOURCE_ID_IMAGE_TIME_5, RESOURCE_ID_IMAGE_TIME_6,
  RESOURCE_ID_IMAGE_TIME_7, RESOURCE_ID_IMAGE_TIME_8, RESOURCE_ID_IMAGE_TIME_9
};

#define NUMBER_OF_DATE_IMAGES 10
const int DATE_IMAGE_RESOURCE_IDS[NUMBER_OF_DATE_IMAGES] = {
  RESOURCE_ID_IMAGE_DATE_0,
  RESOURCE_ID_IMAGE_DATE_1, RESOURCE_ID_IMAGE_DATE_2, RESOURCE_ID_IMAGE_DATE_3,
  RESOURCE_ID_IMAGE_DATE_4, RESOURCE_ID_IMAGE_DATE_5, RESOURCE_ID_IMAGE_DATE_6,
  RESOURCE_ID_IMAGE_DATE_7, RESOURCE_ID_IMAGE_DATE_8, RESOURCE_ID_IMAGE_DATE_9
};

#define NUMBER_OF_BATTERY_IMAGES 10
const int BATTERY_IMAGE_RESOURCE_IDS[NUMBER_OF_BATTERY_IMAGES] = {
  RESOURCE_ID_IMAGE_BATTERY_0,
  RESOURCE_ID_IMAGE_BATTERY_1, RESOURCE_ID_IMAGE_BATTERY_2, RESOURCE_ID_IMAGE_BATTERY_3,
  RESOURCE_ID_IMAGE_BATTERY_4, RESOURCE_ID_IMAGE_BATTERY_5, RESOURCE_ID_IMAGE_BATTERY_6,
  RESOURCE_ID_IMAGE_BATTERY_7, RESOURCE_ID_IMAGE_BATTERY_8, RESOURCE_ID_IMAGE_BATTERY_9
};

#define NUMBER_OF_DAY_IMAGES 7
const int DAY_IMAGE_RESOURCE_IDS[NUMBER_OF_DAY_IMAGES] = {
  RESOURCE_ID_IMAGE_DAY_0, RESOURCE_ID_IMAGE_DAY_1, RESOURCE_ID_IMAGE_DAY_2,
  RESOURCE_ID_IMAGE_DAY_3, RESOURCE_ID_IMAGE_DAY_4, RESOURCE_ID_IMAGE_DAY_5,
  RESOURCE_ID_IMAGE_DAY_6
};


// General
static Window *window;


#define EMPTY_SLOT -1
typedef struct Slot {
  int         number;
  GBitmap     *image;
  BitmapLayer *image_layer;
  int         state;
} Slot;

// Time
typedef struct TimeSlot {
  Slot              slot;
  int               new_state;
  PropertyAnimation *slide_out_animation;
  PropertyAnimation *slide_in_animation;
  bool              updating;
} TimeSlot;

#define NUMBER_OF_TIME_SLOTS 4
static Layer *time_layer;
static TimeSlot time_slots[NUMBER_OF_TIME_SLOTS];

// Footer
static Layer *footer_layer;

// Day
typedef struct DayItem {
  GBitmap     *image;
  BitmapLayer *image_layer;
  Layer       *layer;
  bool       loaded;
} DayItem;
static DayItem day_item;

// Date
#define NUMBER_OF_DATE_SLOTS 4
static Layer *date_layer;
static Slot date_slots[NUMBER_OF_DATE_SLOTS];

// Battery
#define NUMBER_OF_BATTERY_SLOTS 2
static Layer *battery_layer;
static Slot battery_slots[NUMBER_OF_BATTERY_SLOTS];
static int s_battery_percent;

// State
static bool s_bt_connected;
static bool s_quiet_mode;


// General
void destroy_property_animation(PropertyAnimation **prop_animation);
BitmapLayer *load_digit_image_into_slot(Slot *slot, int digit_value, Layer *parent_layer, GRect frame, const int *digit_resource_ids);
void unload_digit_image_from_slot(Slot *slot);

// Time
void display_time(struct tm *tick_time);
void display_time_value(int value, int row_number);
void update_time_slot(TimeSlot *time_slot, int digit_value);
GRect frame_for_time_slot(TimeSlot *time_slot);
void slide_in_digit_image_into_time_slot(TimeSlot *time_slot, int digit_value);
void time_slot_slide_in_animation_stopped(Animation *slide_in_animation, bool finished, void *context);
void slide_out_digit_image_from_time_slot(TimeSlot *time_slot);
void time_slot_slide_out_animation_stopped(Animation *slide_out_animation, bool finished, void *context);

// Day
void display_day(struct tm *tick_time);
void unload_day_item();

// Date
void display_date(struct tm *tick_time);
void display_date_value(int value, int part_number);
void update_date_slot(Slot *date_slot, int digit_value);

// Battery
void display_battery(int percent);
void update_battery_slot(Slot *battery_slot, int digit_value);
void handle_battery_change(BatteryChargeState state);

// State / Color
GColor get_fg_color();
void tint_bitmap(GBitmap *bitmap);
void refresh_images();

// Connection
void handle_bt_connection_change(bool connected);

// Handlers
int main(void);
void init();
void handle_second_tick(struct tm *tick_time, TimeUnits units_changed);
void deinit();


// General
void destroy_property_animation(PropertyAnimation **animation) {
  if (*animation == NULL)
    return;

  if (animation_is_scheduled((Animation *)*animation)) {
    animation_unschedule((Animation *)*animation);
  }

  property_animation_destroy(*animation);
  *animation = NULL;
}

BitmapLayer *load_digit_image_into_slot(Slot *slot, int digit_value, Layer *parent_layer, GRect frame, const int *digit_resource_ids) {
  if (digit_value < 0 || digit_value > 9)
    return NULL;

  if (slot->state != EMPTY_SLOT)
    return NULL;

  slot->state = digit_value;

  slot->image = gbitmap_create_with_resource(digit_resource_ids[digit_value]);
  tint_bitmap(slot->image);

  slot->image_layer = bitmap_layer_create(frame);
  bitmap_layer_set_bitmap(slot->image_layer, slot->image);
  layer_add_child(parent_layer, bitmap_layer_get_layer(slot->image_layer));

  return slot->image_layer;
}

void unload_digit_image_from_slot(Slot *slot) {
  if (slot->state == EMPTY_SLOT)
    return;

  layer_remove_from_parent(bitmap_layer_get_layer(slot->image_layer));
  bitmap_layer_destroy(slot->image_layer);

  gbitmap_destroy(slot->image);

  slot->state = EMPTY_SLOT;
}

// Time
void display_time(struct tm *tick_time) {
  int hour = tick_time->tm_hour;

  if (!clock_is_24h_style()) {
    hour = hour % 12;
    if (hour == 0) {
      hour = 12;
    }
  }

  display_time_value(hour,              0);
  display_time_value(tick_time->tm_min, 1);
}

void display_time_value(int value, int row_number) {
  value = value % 100; // Maximum of two digits per row.

  for (int column_number = 1; column_number >= 0; column_number--) {
    int time_slot_number = (row_number * 2) + column_number;

    TimeSlot *time_slot = &time_slots[time_slot_number];

    update_time_slot(time_slot, value % 10);

    value = value / 10;
  }
}

void update_time_slot(TimeSlot *time_slot, int digit_value) {
  if (time_slot->slot.state == digit_value)
    return;

  if (time_slot->updating) {
    // Otherwise we'll crash when the animation is replaced by a new animation before we're finished.
    return;
  }

  if (time_slot->slot.state == EMPTY_SLOT) {
    GRect frame = frame_for_time_slot(time_slot);
    load_digit_image_into_slot(&time_slot->slot, digit_value, time_layer, frame, TIME_IMAGE_RESOURCE_IDS);
  }
  else {
    time_slot->updating = true;
    time_slot->new_state = digit_value;
    slide_out_digit_image_from_time_slot(time_slot);
  }
}

GRect frame_for_time_slot(TimeSlot *time_slot) {
  int x = MARGIN + (time_slot->slot.number % 2) * (TIME_IMAGE_WIDTH + TIME_SLOT_SPACE);
  int y = MARGIN + (time_slot->slot.number / 2) * (TIME_IMAGE_HEIGHT + TIME_SLOT_SPACE);

  return GRect(x, y, TIME_IMAGE_WIDTH, TIME_IMAGE_HEIGHT);
}

void slide_in_digit_image_into_time_slot(TimeSlot *time_slot, int digit_value) {
  destroy_property_animation(&time_slot->slide_in_animation);

  GRect to_frame = frame_for_time_slot(time_slot);

  int from_x = to_frame.origin.x;
  int from_y = to_frame.origin.y;
  switch (time_slot->slot.number) {
    case 0:
      from_x -= TIME_IMAGE_WIDTH + MARGIN;
      break;
    case 1:
      from_y -= TIME_IMAGE_HEIGHT + MARGIN;
      break;
    case 2:
      from_y += TIME_IMAGE_HEIGHT + MARGIN;
      break;
    case 3:
      from_x += TIME_IMAGE_WIDTH + MARGIN;
      break;
  }
  GRect from_frame = GRect(from_x, from_y, TIME_IMAGE_WIDTH, TIME_IMAGE_HEIGHT);

  BitmapLayer *image_layer = load_digit_image_into_slot(&time_slot->slot, digit_value, time_layer, from_frame, TIME_IMAGE_RESOURCE_IDS);

  time_slot->slide_in_animation = property_animation_create_layer_frame(bitmap_layer_get_layer(image_layer), &from_frame, &to_frame);

  Animation *animation = (Animation *)time_slot->slide_in_animation;
  animation_set_duration( animation,  TIME_SLOT_ANIMATION_DURATION);
  animation_set_curve(    animation,  AnimationCurveLinear);
  animation_set_handlers( animation,  (AnimationHandlers){
    .stopped = (AnimationStoppedHandler)time_slot_slide_in_animation_stopped
  }, (void *)time_slot);

  animation_schedule(animation);
}

void time_slot_slide_in_animation_stopped(Animation *slide_in_animation, bool finished, void *context) {
  TimeSlot *time_slot = (TimeSlot *)context;

  destroy_property_animation(&time_slot->slide_in_animation);

  time_slot->updating = false;
}

void slide_out_digit_image_from_time_slot(TimeSlot *time_slot) {
  destroy_property_animation(&time_slot->slide_out_animation);

  GRect from_frame = frame_for_time_slot(time_slot);

  int to_x = from_frame.origin.x;
  int to_y = from_frame.origin.y;
  switch (time_slot->slot.number) {
    case 0:
      to_y -= TIME_IMAGE_HEIGHT + MARGIN;
      break;
    case 1:
      to_x += TIME_IMAGE_WIDTH + MARGIN;
      break;
    case 2:
      to_x -= TIME_IMAGE_WIDTH + MARGIN;
      break;
    case 3:
      to_y += TIME_IMAGE_HEIGHT + MARGIN;
      break;
  }
  GRect to_frame = GRect(to_x, to_y, TIME_IMAGE_WIDTH, TIME_IMAGE_HEIGHT);

  BitmapLayer *image_layer = time_slot->slot.image_layer;

  time_slot->slide_out_animation = property_animation_create_layer_frame(bitmap_layer_get_layer(image_layer), &from_frame, &to_frame);

  Animation *animation = (Animation *)time_slot->slide_out_animation;
  animation_set_duration( animation,  TIME_SLOT_ANIMATION_DURATION);
  animation_set_curve(    animation,  AnimationCurveLinear);
  animation_set_handlers(animation, (AnimationHandlers){
    .stopped = (AnimationStoppedHandler)time_slot_slide_out_animation_stopped
  }, (void *)time_slot);

  animation_schedule(animation);
}

void time_slot_slide_out_animation_stopped(Animation *slide_out_animation, bool finished, void *context) {
  TimeSlot *time_slot = (TimeSlot *)context;

  destroy_property_animation(&time_slot->slide_out_animation);

  if (time_slot->new_state == EMPTY_SLOT) {
    time_slot->updating = false;
  }
  else {
    unload_digit_image_from_slot(&time_slot->slot);

    slide_in_digit_image_into_time_slot(time_slot, time_slot->new_state);

    time_slot->new_state = EMPTY_SLOT;
  }
}

// Day
void display_day(struct tm *tick_time) {
  unload_day_item();

  day_item.image = gbitmap_create_with_resource(DAY_IMAGE_RESOURCE_IDS[tick_time->tm_wday]);
  tint_bitmap(day_item.image);

  day_item.image_layer = bitmap_layer_create(gbitmap_get_bounds(day_item.image));
  bitmap_layer_set_bitmap(day_item.image_layer, day_item.image);
  layer_add_child(day_item.layer, bitmap_layer_get_layer(day_item.image_layer));

  day_item.loaded = true;
}

void unload_day_item() {
  if (!day_item.loaded)
    return;

  layer_remove_from_parent(bitmap_layer_get_layer(day_item.image_layer));
  bitmap_layer_destroy(day_item.image_layer);

  gbitmap_destroy(day_item.image);

  day_item.loaded = false;
}

// Date
void display_date(struct tm *tick_time) {
  int day   = tick_time->tm_mday;
  int month = tick_time->tm_mon + 1;

#if USE_AMERICAN_DATE_FORMAT
  display_date_value(month, 0);
  display_date_value(day,   1);
#else
  display_date_value(day,   0);
  display_date_value(month, 1);
#endif
}

void display_date_value(int value, int part_number) {
  value = value % 100; // Maximum of two digits per row.

  for (int column_number = 1; column_number >= 0; column_number--) {
    int date_slot_number = (part_number * 2) + column_number;

    Slot *date_slot = &date_slots[date_slot_number];

    update_date_slot(date_slot, value % 10);

    value = value / 10;
  }
}

void update_date_slot(Slot *date_slot, int digit_value) {
  if (date_slot->state == digit_value)
    return;

  int x = date_slot->number * (DATE_IMAGE_WIDTH + MARGIN);
  if (date_slot->number >= 2) {
    x += 3; // 3 extra pixels of space between the day and month
  }
  GRect frame = GRect(x, 0, DATE_IMAGE_WIDTH, DATE_IMAGE_HEIGHT);

  unload_digit_image_from_slot(date_slot);
  load_digit_image_into_slot(date_slot, digit_value, date_layer, frame, DATE_IMAGE_RESOURCE_IDS);
}

// State / Color
GColor get_fg_color() {
  if (!s_bt_connected) return GColorRed;
  if (s_quiet_mode)    return GColorYellow;
  return GColorWhite;
}

void tint_bitmap(GBitmap *bitmap) {
  if (!bitmap) return;
  GBitmapFormat format = gbitmap_get_format(bitmap);
  if (format != GBitmapFormat1BitPalette &&
      format != GBitmapFormat2BitPalette &&
      format != GBitmapFormat4BitPalette) return;
  GColor *palette = gbitmap_get_palette(bitmap);
  if (!palette) return;
  int palette_size;
  switch (format) {
    case GBitmapFormat1BitPalette: palette_size = 2;  break;
    case GBitmapFormat2BitPalette: palette_size = 4;  break;
    case GBitmapFormat4BitPalette: palette_size = 16; break;
    default: return;
  }
  GColor target = get_fg_color();
  for (int i = 0; i < palette_size; i++) {
    if (gcolor_equal(palette[i], GColorWhite)) {
      palette[i] = target;
    }
  }
}

void refresh_images() {
  for (int i = 0; i < NUMBER_OF_TIME_SLOTS; i++) {
    TimeSlot *ts = &time_slots[i];
    if (ts->slot.state != EMPTY_SLOT && !ts->updating) {
      int digit = ts->slot.state;
      unload_digit_image_from_slot(&ts->slot);
      GRect frame = frame_for_time_slot(ts);
      load_digit_image_into_slot(&ts->slot, digit, time_layer, frame, TIME_IMAGE_RESOURCE_IDS);
    }
  }
  for (int i = 0; i < NUMBER_OF_DATE_SLOTS; i++) {
    int digit = date_slots[i].state;
    if (digit != EMPTY_SLOT) {
      unload_digit_image_from_slot(&date_slots[i]);
      update_date_slot(&date_slots[i], digit);
    }
  }
  if (day_item.loaded) {
    time_t now = time(NULL);
    display_day(localtime(&now));
  }
  for (int i = 0; i < NUMBER_OF_BATTERY_SLOTS; i++) {
    int digit = battery_slots[i].state;
    if (digit != EMPTY_SLOT) {
      unload_digit_image_from_slot(&battery_slots[i]);
      update_battery_slot(&battery_slots[i], digit);
    }
  }
}

// Battery
void update_battery_slot(Slot *battery_slot, int digit_value) {
  if (battery_slot->state == digit_value)
    return;

  GRect frame = GRect(
    battery_slot->number * (BATTERY_IMAGE_WIDTH + MARGIN),
    0,
    BATTERY_IMAGE_WIDTH,
    BATTERY_IMAGE_HEIGHT
  );

  unload_digit_image_from_slot(battery_slot);
  load_digit_image_into_slot(battery_slot, digit_value, battery_layer, frame, BATTERY_IMAGE_RESOURCE_IDS);
}

void display_battery(int percent) {
  s_battery_percent = percent;
  if (percent > 99) percent = 99;

  for (int slot_number = 1; slot_number >= 0; slot_number--) {
    update_battery_slot(&battery_slots[slot_number], percent % 10);
    percent /= 10;
  }
}

void handle_battery_change(BatteryChargeState state) {
  display_battery(state.charge_percent);
}

// Connection
void handle_bt_connection_change(bool connected) {
  if (connected == s_bt_connected) return;
  s_bt_connected = connected;
  refresh_images();
}

// Handlers
int main(void) {
  init();
  app_event_loop();
  deinit();
}

void init() {
  window = window_create();
  window_stack_push(window, true /* Animated */);
  window_set_background_color(window, GColorBlack);

  Layer *root_layer = window_get_root_layer(window);

  // Time
  for (int i = 0; i < NUMBER_OF_TIME_SLOTS; i++) {
    TimeSlot *time_slot = &time_slots[i];
    time_slot->slot.number  = i;
    time_slot->slot.state   = EMPTY_SLOT;
    time_slot->new_state    = EMPTY_SLOT;
    time_slot->updating     = false;
  }

  time_layer = layer_create(GRect(0, 0, SCREEN_WIDTH, SCREEN_WIDTH));
  layer_set_clips(time_layer, true);
  layer_add_child(root_layer, time_layer);

  // Footer
  int footer_height = SCREEN_HEIGHT - SCREEN_WIDTH;

  footer_layer = layer_create(GRect(0, SCREEN_WIDTH, SCREEN_WIDTH, footer_height));
  layer_add_child(root_layer, footer_layer);

  // Day
  day_item.loaded = false;

  GRect day_layer_frame = GRect(
    MARGIN + 5,
    footer_height - DAY_IMAGE_HEIGHT - MARGIN,
    DAY_IMAGE_WIDTH,
    DAY_IMAGE_HEIGHT
  );
  day_item.layer = layer_create(day_layer_frame);
  layer_add_child(footer_layer, day_item.layer);

  // Date
  for (int i = 0; i < NUMBER_OF_DATE_SLOTS; i++) {
    Slot *date_slot = &date_slots[i];
    date_slot->number = i;
    date_slot->state  = EMPTY_SLOT;
  }

  GRect date_layer_frame = GRectZero;
  date_layer_frame.size.w   = DATE_IMAGE_WIDTH + MARGIN + DATE_IMAGE_WIDTH + DATE_PART_SPACE + DATE_IMAGE_WIDTH + MARGIN + DATE_IMAGE_WIDTH;
  date_layer_frame.size.h   = DATE_IMAGE_HEIGHT;
  date_layer_frame.origin.x = (SCREEN_WIDTH - date_layer_frame.size.w) / 2;
  date_layer_frame.origin.y = footer_height - DATE_IMAGE_HEIGHT - MARGIN;

  date_layer = layer_create(date_layer_frame);
  layer_add_child(footer_layer, date_layer);

  // Battery
  for (int i = 0; i < NUMBER_OF_BATTERY_SLOTS; i++) {
    battery_slots[i].number = i;
    battery_slots[i].state  = EMPTY_SLOT;
  }

  GRect battery_layer_frame = GRect(
    SCREEN_WIDTH - (BATTERY_IMAGE_WIDTH + MARGIN + BATTERY_IMAGE_WIDTH) - 5,
    footer_height - BATTERY_IMAGE_HEIGHT - MARGIN,
    BATTERY_IMAGE_WIDTH + MARGIN + BATTERY_IMAGE_WIDTH,
    BATTERY_IMAGE_HEIGHT
  );
  battery_layer = layer_create(battery_layer_frame);
  layer_add_child(footer_layer, battery_layer);

  // Initial state
  s_bt_connected = connection_service_peek_pebble_app_connection();
  s_quiet_mode = quiet_time_is_active();
  s_battery_percent = 0;

  // Display
  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);
  display_time(tick_time);
  display_day(tick_time);
  display_date(tick_time);
  display_battery(battery_state_service_peek().charge_percent);

  battery_state_service_subscribe(handle_battery_change);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = handle_bt_connection_change
  });

  tick_timer_service_subscribe(MINUTE_UNIT, handle_second_tick);
}

void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
  display_time(tick_time);

  bool new_quiet = quiet_time_is_active();
  if (new_quiet != s_quiet_mode) {
    s_quiet_mode = new_quiet;
    refresh_images();
  }

#if VIBE_ON_HOUR
  if ((units_changed & HOUR_UNIT) == HOUR_UNIT) {
    vibes_double_pulse();
  }
#endif

  if ((units_changed & DAY_UNIT) == DAY_UNIT) {
    display_day(tick_time);
    display_date(tick_time);
  }
}

void deinit() {
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();

  // Time
  for (int i = 0; i < NUMBER_OF_TIME_SLOTS; i++) {
    unload_digit_image_from_slot(&time_slots[i].slot);

    destroy_property_animation(&time_slots[i].slide_in_animation);
    destroy_property_animation(&time_slots[i].slide_out_animation);
  }
  layer_destroy(time_layer);

  // Day
  unload_day_item();
  layer_destroy(day_item.layer);

  // Date
  for (int i = 0; i < NUMBER_OF_DATE_SLOTS; i++) {
    unload_digit_image_from_slot(&date_slots[i]);
  }
  layer_destroy(date_layer);

  // Battery
  for (int i = 0; i < NUMBER_OF_BATTERY_SLOTS; i++) {
    unload_digit_image_from_slot(&battery_slots[i]);
  }
  layer_destroy(battery_layer);

  layer_destroy(footer_layer);

  window_destroy(window);
}
