#define bool _Bool

#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp8266/gpio_struct.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

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

#define LED_WIDTH 100
#define LED_HEIGHT 3
#define NUM_PIXEL (LED_WIDTH * LED_HEIGHT)

// LED data wires on D1, D2, D4
#define LED_A_PIN (GPIO_NUM_5)
#define LED_B_PIN (GPIO_NUM_4)
#define LED_C_PIN (GPIO_NUM_2)

int led_pins[3] = {LED_A_PIN, LED_B_PIN, LED_C_PIN};

color_t pixels[NUM_PIXEL];

void clear_display();

void write_ws2811_bit(int pin, int b);
void write_ws2811_data();

void timer_callback(void *);

static int snowy();
static int rainbow_drops();
static int candy_cane();

void app_main() {
  printf("=> xmas22 starting\n");

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
  while (1) {
    int next = 0;
    switch (mode) {
    case 0:
      next = snowy();
      break;
    case 1:
      next = rainbow_drops();
      break;
    case 2:
      next = candy_cane();
      break;
    }

    if (next) {
      mode = (mode + 1) % 3;
    }

    write_ws2811_data();

    vTaskDelay(20 / portTICK_PERIOD_MS);
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
    int pin;
    switch (j) {
    default:
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
      color_t *c = &pixels[LED_WIDTH * j + i];
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

// white and three shades of ice blue
static color_t snow_colors[4] = {
    {255, 255, 255},
    {190, 240, 255},
    {100, 230, 255},
    {50, 220, 255},
};

typedef struct {
  uint16_t x : 14; // 10.4
  uint16_t y : 2;
} particle_pos_t;

#define NUM_SNOW_PARTICLE 50
#define NUM_SNOW_TICKS 1000

static int snowy() {
  static particle_pos_t particles[NUM_SNOW_PARTICLE];
  static int timer = NUM_SNOW_TICKS;
  static int snow_color_idx = 0;

  if (timer == NUM_SNOW_TICKS) {
    snow_color_idx = rand() & 3;
    for (int i = 0; i < NUM_SNOW_PARTICLE; i++) {
      particles[i].x = rand() % (LED_WIDTH << 4);
      particles[i].y = rand() % LED_HEIGHT;
    }
  }

  clear_display();
  for (int i = 0; i < NUM_SNOW_PARTICLE; i++) {
    int x = particles[i].x >> 4;
    int y = particles[i].y;
    if (x >= 0 && x < LED_WIDTH && y >= 0 && y < LED_HEIGHT) {
      pixels[LED_WIDTH * y + x] = snow_colors[(snow_color_idx ^ i) & 3];
    }
    particles[i].x += 1 + (i & 1);
    if (particles[i].x >= (LED_WIDTH << 4)) {
      particles[i].x = 0;
      particles[i].y = rand() % LED_HEIGHT;
    }
  }

  timer--;
  if (timer == 0) {
    timer = NUM_SNOW_TICKS;
    return 1;
  }
  return 0;
}

#define NUM_RAINBOW_DROPS 6
#define NUM_RAINBOW_TICKS 1000

static int rainbow_drops() {
  static int xs[NUM_RAINBOW_DROPS];
  static uint8_t ys[NUM_RAINBOW_DROPS];
  static int xvs[NUM_RAINBOW_DROPS];
  static uint8_t gradient_offsets[NUM_RAINBOW_DROPS];
  static int timer = NUM_RAINBOW_TICKS;

  if (timer == NUM_RAINBOW_TICKS) {
    clear_display();
    for (int i = 0; i < NUM_RAINBOW_DROPS; i++) {
      xs[i] = rand() % (LED_WIDTH << 4);
      ys[i] = rand() % LED_HEIGHT;
      xvs[i] = 3 + (rand() & 3);
      if (rand() & 1) {
        xvs[i] = -xvs[i];
      }
      gradient_offsets[i] = rand() % NUM_GRADIENT;
    }
  } else {
    for (int j = 0; j < LED_HEIGHT; j++) {
      for (int i = 0; i < LED_WIDTH; i++) {
        int idx = LED_WIDTH * j + i;
        pixels[idx].r = (uint8_t)(((int)pixels[idx].r * 3) >> 2);
        pixels[idx].g = (uint8_t)(((int)pixels[idx].g * 3) >> 2);
        pixels[idx].b = (uint8_t)(((int)pixels[idx].b * 3) >> 2);
      }
    }
  }

  for (int i = 0; i < NUM_RAINBOW_DROPS; i++) {
    int x = xs[i];
    if (x < 0 || x >= (LED_WIDTH << 4)) {
      // birth
      x = xs[i] = rand() % (LED_WIDTH << 4);
      ys[i] = rand() % LED_HEIGHT;
      xvs[i] = 3 + (rand() & 3);
      if (rand() & 1) {
        xvs[i] = -xvs[i];
      }
      gradient_offsets[i] = rand() % NUM_GRADIENT;
    }
    int y = ys[i];
    pixels[LED_WIDTH * y + (x >> 4)] = gradient[gradient_offsets[i]];
    gradient_offsets[i] = (gradient_offsets[i] + 1) % NUM_GRADIENT;
    xs[i] += xvs[i];
  }

  timer--;
  if (timer == 0) {
    timer = NUM_RAINBOW_TICKS;
    return 1;
  }
  return 0;
}

#define NUM_CANDY_CANE_TICKS 1000

static color_t white = {255, 255, 255};

static int candy_cane() {
  // scroller with a wwgrrrgww block
  static int x[LED_HEIGHT];
  static int timer = NUM_CANDY_CANE_TICKS;

  if (timer == NUM_CANDY_CANE_TICKS) {
    for (int i = 0; i < LED_HEIGHT; i++) {
      x[i] = rand() % LED_WIDTH;
    }
  }

  clear_display();
  for (int j = 0; j < LED_HEIGHT; j++) {
    int n = x[j];
    for (int i = 0; i < LED_WIDTH; i++) {
      n++;
      if (n >= LED_WIDTH) {
        n -= LED_WIDTH;
      }
      switch (i & 31) {
      case 0:
        pixels[LED_WIDTH * j + n] = white;
        break;
      case 1:
        pixels[LED_WIDTH * j + n] = white;
        break;
      case 2:
        pixels[LED_WIDTH * j + n] = stops[3];
        break;
      case 3:
        pixels[LED_WIDTH * j + n] = stops[0];
        break;
      case 4:
        pixels[LED_WIDTH * j + n] = stops[0];
        break;
      case 5:
        pixels[LED_WIDTH * j + n] = stops[0];
        break;
      case 6:
        pixels[LED_WIDTH * j + n] = stops[3];
        break;
      case 7:
        pixels[LED_WIDTH * j + n] = white;
        break;
      case 8:
        pixels[LED_WIDTH * j + n] = white;
        break;
      }
    }
    x[j]++;
    if (x[j] >= LED_WIDTH) {
      x[j] -= LED_WIDTH;
    }
  }

  timer--;
  if (timer == 0) {
    timer = NUM_CANDY_CANE_TICKS;
    return 1;
  }
  return 0;
}
