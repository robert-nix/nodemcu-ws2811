/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#define bool _Bool

#include "driver/adc.h"
#include "driver/gpio.h"
#include "driver/hw_timer.h"
#include "esp8266/gpio_struct.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#include "font.h"

typedef struct {
  uint8_t r, g, b;
} color_t;

#define NUM_GRADIENT 140

color_t gradient[NUM_GRADIENT];

color_t stops[7] = {
    {255, 0, 0},   // red
    {255, 32, 0},  // orange
    {255, 255, 0}, // yellow
    {0, 255, 0},   // green
    {0, 255, 255}, // cyan
    {0, 0, 255},   // blue
    {255, 0, 255}, // magenta
};

#define LED_WIDTH 50
#define LED_HEIGHT 6
#define NUM_PIXEL (LED_WIDTH * LED_HEIGHT)

// LED data wires on D1, D2, D4
#define LED_A_PIN (GPIO_NUM_5)
#define LED_B_PIN (GPIO_NUM_4)
#define LED_C_PIN (GPIO_NUM_2)

int led_pins[3] = {LED_A_PIN, LED_B_PIN, LED_C_PIN};

color_t pixels[NUM_PIXEL];

void clear_display();
void show_text(char *s, int scroll);
int get_text_width(char *s);

void write_ws2811_bit(int pin, int b);
void write_ws2811_data();

void timer_callback(void *);

static int scroller(char *);
static int snowy();
static int rainbow_drops();

void app_main() {
  printf("=> xmas22 starting\n");

  // Print chip information
  //   esp_chip_info_t chip_info;
  //   esp_chip_info(&chip_info);
  //   printf("This is ESP8266 chip with %d CPU cores, WiFi, ",
  //   chip_info.cores);

  //   printf("silicon revision %d, ", chip_info.revision);

  //   printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
  //          (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded"
  //                                                        : "external");

  // TOUT (this is a nodeMCU) is ADC0 divided by 220kOhm and 100kOhm
  // ADC0 is then divided by 20kOhm and 100kOhm
  // 4.71V results in approx. 270
  // 12.2V (our battery pack) results in approx. 700
  // this leaves half a bit on the floor, but better to be safe
  adc_config_t adc_config = {
      .mode = ADC_READ_TOUT_MODE,
      .clk_div = 8,
  };
  adc_init(&adc_config);

  //   hw_timer_init(timer_callback, NULL);
  //   hw_timer_set_reload(1);
  //   hw_timer_set_clkdiv(TIMER_CLKDIV_1);
  //   hw_timer_set_load_data(400); // 100kHz square wave
  //   hw_timer_enable(1);

  for (int i = 0; i < 3; i++) {
    gpio_config_t io_conf;
    int pin = led_pins[i];
    io_conf.pin_bit_mask = (1 << pin);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(pin, 0);

    taskENTER_CRITICAL();
    for (int i = 0; i < 100; i++) {
      write_ws2811_bit(pin, 0);
    }
    taskEXIT_CRITICAL();
  }

  {
    color_t *g = &gradient[0];
    for (int j = 0; j < 7; j++) {
      color_t *a = &stops[j];
      color_t *b = &stops[j == 6 ? 0 : j + 1];
      for (int i = 0; i < 20; i++) {
        g->r = (uint8_t)((a->r * (20 - i) + b->r * i) / 20);
        g->g = (uint8_t)((a->g * (20 - i) + b->g * i) / 20);
        g->b = (uint8_t)((a->b * (20 - i) + b->b * i) / 20);
        g++;
      }
    }
  }

  srand(esp_random());
  int mode = 0;
  char text[128];
  sprintf(text, "hello");
  while (1) {
    clear_display();
    int next = 0;
    switch (mode) {
    case 0:
      next = scroller(text);
      break;
    case 1:
      next = snowy();
      break;
    case 2:
      next = rainbow_drops();
      break;
    }

    if (next) {
      mode = (mode + 1) % 3;
      if (mode == 0) {
        uint16_t adc = 0;
        adc_read(&adc);
        uint32_t voltage = ((uint32_t)adc * 4336) >> 8;
        sprintf(text, "Voltage: %d mV", voltage);
      }
    }

    write_ws2811_data();

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

static int flip = 0;

void timer_callback(void *arg) {
  gpio_set_level(GPIO_NUM_5, flip);
  flip ^= 1;
}

void write_ws2811_bit(int p, int b) {
  uint32_t pin_bit = (1 << p);
  if (b) {
    // 1.2us high, 1.3us low (+/- 150ns)
    // 80Mhz * 1.2us = 96 cycles
    // 80Mhz * 1.3us = 104 cycles
    // 1 iteration = ~2.5 cycles (bnez+nop+addi, but op fusion yay)
    GPIO.out_w1ts = pin_bit;
#pragma GCC unroll 0
    for (int i = 39; i; --i) {
      asm volatile("nop");
    }
    GPIO.out_w1tc = pin_bit;
#pragma GCC unroll 0
    for (int i = 41; i; --i) {
      asm volatile("nop");
    }
  } else {
    // 0.5us high, 2.0us low (+/- 150ns)
    // 80Mhz * 0.5us = 40 cycles
    // 80Mhz * 2.0us = 160 cycles
    GPIO.out_w1ts = pin_bit;
#pragma GCC unroll 0
    for (int i = 14; i; --i) {
      asm volatile("nop");
    }
    GPIO.out_w1tc = pin_bit;
#pragma GCC unroll 0
    for (int i = 64; i; --i) {
      asm volatile("nop");
    }
  }
}

void write_ws2811_data() {
  taskENTER_CRITICAL();
  for (int j = 0; j < LED_HEIGHT; j++) {
    int reverse = j % 2;
    int pin;
    switch (j / 2) {
    case 0:
      pin = LED_A_PIN;
      break;
    case 1:
      pin = LED_B_PIN;
      break;
    case 2:
      pin = LED_C_PIN;
      break;
    }
    for (int i = 0; i < LED_WIDTH; i++) {
      color_t *c = &pixels[LED_WIDTH * j + (reverse ? LED_WIDTH - i - 1 : i)];
      for (int bi = 0; bi < 8; bi++) {
        write_ws2811_bit(pin, (c->b & (1 << (7 - bi))) ? 1 : 0);
      }
      for (int ri = 0; ri < 8; ri++) {
        write_ws2811_bit(pin, (c->r & (1 << (7 - ri))) ? 1 : 0);
      }
      for (int gi = 0; gi < 8; gi++) {
        write_ws2811_bit(pin, (c->g & (1 << (7 - gi))) ? 1 : 0);
      }
    }
  }
  taskEXIT_CRITICAL();
}

void clear_display() { memset(pixels, 0, sizeof(pixels)); }

void show_text(char *s, int scroll) {
  int col = -scroll;
  while (*s) {
    char c = *s++;
    if (c < ' ' || c > '~') {
      c = ' ';
    }
    int gi = (c - ' ');
    uint8_t *g = &font_glyph_bitmap[gi * GLYPH_HEIGHT];
    uint8_t w = font_glyph_widths[gi];
    for (int j = 0; j < GLYPH_HEIGHT; j++) {
      for (int i = 0; i < w; i++) {
        if (col + i < 0)
          continue;
        if (col + i >= LED_WIDTH) {
          continue;
        }
        if (g[j] & (1 << (7 - i))) {
          pixels[LED_WIDTH * j + col + i] = gradient[col + i];
        }
      }
    }
    col += w + 1;
    if (col >= LED_WIDTH) {
      break;
    }
  }
}

int get_text_width(char *s) {
  int w = 0;
  while (*s) {
    char c = *s++;
    if (c < ' ' || c > '~') {
      c = ' ';
    }
    w += font_glyph_widths[c - ' '] + 1;
  }
  return w;
}

static int scroller(char *scroll_text) {
  static int scroll = 0;
  static int wait = 30;
  show_text(scroll_text, scroll);
  if (!(--wait)) {
    scroll++;
    wait = 3;
    if (scroll >= get_text_width(scroll_text) - (LED_WIDTH / 2)) {
      scroll = 0;
      wait = 30;
      return 1;
    }
  }
  return 0;
}

// white and three shades of ice blue
static color_t snow_colors[4] = {
    {255, 255, 255},
    {190, 240, 255},
    {100, 230, 255},
    {50, 220, 255},
};

typedef struct {
  uint16_t x : 10; // 6.4
  uint16_t y : 6;  // 3.3
} particle_pos_t;

#define NUM_SNOW_PARTICLE 50
#define NUM_SNOW_TICKS 200

static int snowy() {
  static particle_pos_t particles[NUM_SNOW_PARTICLE];
  static int timer = NUM_SNOW_TICKS;
  static int snow_color_idx = 0;
  if (timer == NUM_SNOW_TICKS) {
    snow_color_idx = rand() & 3;
    for (int i = 0; i < NUM_SNOW_PARTICLE; i++) {
      particles[i].x = rand() % (LED_WIDTH << 4);
      particles[i].y = rand() % (LED_HEIGHT << 3);
    }
  }
  for (int i = 0; i < NUM_SNOW_PARTICLE; i++) {
    int x = particles[i].x >> 4;
    int y = particles[i].y >> 3;
    if (x >= 0 && x < LED_WIDTH && y >= 0 && y < LED_HEIGHT) {
      pixels[LED_WIDTH * y + x] =
          snow_colors[(y < 3 ? 2 : 0) | ((snow_color_idx ^ i) & 3)];
    }
    particles[i].x += 1 + (i & 1);
    if (particles[i].x >= (LED_WIDTH << 4)) {
      particles[i].x = 0;
      particles[i].y = rand() % (LED_HEIGHT << 3);
    }
  }
  timer--;
  if (timer == 0) {
    timer = NUM_SNOW_TICKS;
    return 1;
  }
  return 0;
}

#define NUM_RAINBOW_DROPS 4
#define NUM_RAINBOW_TICKS 200

static int rainbow_drops() {
  static particle_pos_t particles[NUM_RAINBOW_DROPS];
  static uint8_t gradient_offsets[NUM_RAINBOW_DROPS];
  static int radii[NUM_RAINBOW_DROPS];
  static uint8_t life[NUM_RAINBOW_DROPS];
  static int timer = NUM_RAINBOW_TICKS;
  if (timer == NUM_RAINBOW_TICKS) {
    for (int i = 0; i < NUM_RAINBOW_DROPS; i++) {
      particles[i].x = rand() % (LED_WIDTH << 4);
      particles[i].y = rand() % (LED_HEIGHT << 3);
      gradient_offsets[i] = rand() % NUM_GRADIENT;
      radii[i] = 0;
    }
  }

  for (int i = 0; i < NUM_RAINBOW_DROPS; i++) {
    life[i] = 0;
  }

  for (int y = 0; y < LED_HEIGHT; y++) {
    for (int x = 0; x < LED_WIDTH; x++) {
      for (int i = 0; i < NUM_RAINBOW_DROPS; i++) {
        int dx = (x << 4) - particles[i].x;
        int dy = (y << 3) - particles[i].y;
        // scale dy for aspect ratio correction
        dy *= 4;
        // correct for fixed point (4+1 to match fractions)
        int d2 = ((dx * dx) >> 5) + ((dy * dy) >> 3);
        int a = radii[i] - (2 << 3);
        int b = radii[i] + (2 << 3);
        int ai = radii[i] - (1 << 3);
        int bi = radii[i] + (1 << 3);
        int a2 = (a > 0) ? ((a * a) >> 3) : 0;
        int b2 = (b > 0) ? ((b * b) >> 3) : 0;
        int ai2 = (ai > 0) ? ((ai * ai) >> 3) : 0;
        int bi2 = (bi > 0) ? ((bi * bi) >> 3) : 0;

        if (d2 >= a2 && d2 <= b2) {
          life[i] = 1;
          color_t *c = &pixels[LED_WIDTH * y + x];
          *c = gradient[(gradient_offsets[i] + (d2 >> 4)) % NUM_GRADIENT];
          if (d2 >= bi2 || d2 <= ai2) { // fade edges
            c->r >>= 1;
            c->g >>= 1;
            c->b >>= 1;
          }
        }
      }
    }
  }

  for (int i = 0; i < NUM_RAINBOW_DROPS; i++) {
    if (life[i] == 0) {
      particles[i].x = rand() % (LED_WIDTH << 4);
      particles[i].y = rand() % (LED_HEIGHT << 3);
      gradient_offsets[i] = rand() % NUM_GRADIENT;
      radii[i] = 0;
    } else {
      radii[i] += 3;
    }
  }

  timer--;
  if (timer == 0) {
    timer = NUM_RAINBOW_TICKS;
    return 1;
  }
  return 0;
}
