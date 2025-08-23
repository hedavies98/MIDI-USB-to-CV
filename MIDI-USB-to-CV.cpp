#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_midi_host.h"
#include "pico-ssd1306/ssd1306.h"
#include "pico-ssd1306/textRenderer/TextRenderer.h"
#include "hardware/i2c.h"

struct PWMConfig
{
  uint8_t pin;
  uint8_t slice_num;
  uint8_t channel;
};


// 125MHz/ 127 / 30 = 32kHz PWM
const uint WRAP_VAL = 127;
const float CKL_DIV = 30.0f;

// Raspberry Pi Pico GPIO pins
// GATE_PIN is used to control the gate signal
// TRIGGER_PIN outputs a momentary signal when a note is played
const uint8_t GATE_PIN = 22;
const uint8_t TRIGGER_PIN = 21;

PWMConfig note_pwm = {27, 0, 0};
PWMConfig velocity_pwm = {26, 0, 0};
PWMConfig modulation_pwm = {22, 0, 0};

static uint8_t midi_device_address = 0;

// Global variables to track state
uint8_t current_note = 0;
uint8_t current_velocity = 0;
uint8_t modulation_level = 0;
bool sustain_active = false;

// Function prototypes
void init_pwm(PWMConfig &pwm_config);
void init_pins();
void update_display(pico_ssd1306::SSD1306 &display);
void update_outputs();
int64_t trigger_callback(alarm_id_t id, __unused void *user_data);


int main()
{
  // Initialize the board and peripherals
  stdio_init_all();
  board_init();
  tusb_init();
  init_pins();
  init_pwm(note_pwm);
  init_pwm(velocity_pwm);
  init_pwm(modulation_pwm);
  
  pico_ssd1306::SSD1306 display = pico_ssd1306::SSD1306(i2c0, 0x3C, pico_ssd1306::Size::W128xH64);
  sleep_ms(250);
  display.setOrientation(0);
  
  while (true)
  {
    tuh_task();
    update_display(display);
    update_outputs();
  }
}

// Initialise GPIO pins for digital output
void init_pins()
{
  gpio_init(GATE_PIN);
  gpio_set_dir(GATE_PIN, GPIO_OUT);
  gpio_init(TRIGGER_PIN);
  gpio_set_dir(TRIGGER_PIN, GPIO_OUT);

  // Init i2c0 controller
  i2c_init(i2c0, 1000000);
  // Set up pins 12 and 13
  gpio_set_function(12, GPIO_FUNC_I2C);
  gpio_set_function(13, GPIO_FUNC_I2C);
  gpio_pull_up(12);
  gpio_pull_up(13);
}

// Initialise GPIO pins for PWM output
void init_pwm(PWMConfig &pwm_config)
{
  gpio_set_function(pwm_config.pin, GPIO_FUNC_PWM);
  pwm_config.slice_num = pwm_gpio_to_slice_num(pwm_config.pin);
  pwm_config.channel = pwm_gpio_to_channel(pwm_config.pin);

  // set top register and clock division
  pwm_set_wrap(pwm_config.slice_num, WRAP_VAL);
  pwm_set_clkdiv(pwm_config.slice_num, CKL_DIV);
  
  pwm_set_enabled(pwm_config.slice_num, true);
}

int64_t trigger_callback(alarm_id_t id, __unused void *user_data)
{
  gpio_put(TRIGGER_PIN, false);
  return 0;
}

void update_display(pico_ssd1306::SSD1306 &display)
{
  display.clear();
  drawText(&display, font_12x16, "Note", 0 ,0);
  char note_string[10];
  sprintf(note_string, "%03d", current_note);
  drawText(&display, font_12x16, note_string, 50, 0);

  drawText(&display, font_12x16, "Vel", 0, 20);
  char velocity_string[10];
  sprintf(velocity_string, "%03d", current_velocity);
  drawText(&display, font_12x16, velocity_string, 50, 20);

  if (sustain_active)
  {
    drawText(&display, font_12x16, "Sustain:ON", 0, 40);
  } else {
    drawText(&display, font_12x16, "Sustain:OFF", 0, 40);
  }

  display.sendBuffer();

}

void update_outputs()
{
  if(current_note > 0)
  {
    gpio_put(GATE_PIN, true);
    // gpio_put(TRIGGER_PIN, true);
    // add_alarm_in_ms(50, trigger_callback, NULL, false);
    pwm_set_chan_level(note_pwm.slice_num, note_pwm.channel, current_note);
    pwm_set_chan_level(velocity_pwm.slice_num, velocity_pwm.channel, current_velocity);
  } 
  else if (!sustain_active) 
  {
    gpio_put(GATE_PIN, false);
    pwm_set_chan_level(note_pwm.slice_num, note_pwm.channel, 0);
    pwm_set_chan_level(velocity_pwm.slice_num, velocity_pwm.channel, 0);
  }
  pwm_set_chan_level(modulation_pwm.slice_num, modulation_pwm.channel, modulation_level);
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_midi_mount_cb(uint8_t device_addr, uint8_t in_ep, uint8_t out_ep, uint8_t num_cables_rx, uint16_t num_cables_tx)
{
  if (midi_device_address == 0)
  {
    // then no MIDI device is currently connected
    midi_device_address = device_addr;
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_midi_umount_cb(uint8_t device_addr, uint8_t instance)
{
  if (device_addr == midi_device_address)
  {
    midi_device_address = 0;
  }
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
  if (midi_device_address == dev_addr)
  {
    if (num_packets != 0)
    {
      uint8_t cable_num;
      uint8_t buffer[48];
      while (1)
      {
        uint32_t bytes_read = tuh_midi_stream_read(dev_addr, &cable_num, buffer, sizeof(buffer));
        if (bytes_read == 0)
        {
          return;
        }

        switch (buffer[0])
        {
        case 0x90: // note on (note number, velocity)
          current_note = buffer[1];
          current_velocity = buffer[2];
          break;
        case 0x80: // note off (note number, velocity)
          if (buffer[1] == current_note)
          {
            current_note = 0;
          }
          break;
        case 0xE0: // pitch wheel (LSB, MSB)
          break;
        case 0xB0: // MIDI CC
          switch (buffer[1])
          {
          case 0x01: // modwheel MSB
            modulation_level = buffer[2];
            break;
          case 0x07: // volume slider
            break;
          case 0x40: // sustain pedal
            sustain_active = buffer[2] < 0x3F ? false : true;
            break;
          case 0x7B: // all notes off
            sustain_active = false;
            current_note = 0;
            break;
          default:
            break;
          }
        case 0xC0: // program change (program number)
        default:
          break;
        }

        // // print out messages for debug
        // for (uint32_t idx = 0; idx < bytes_read; idx++)
        // {
        //   printf("%02x ", buffer[idx]);
        // }
        // printf("\r\n");
      }
    }
  }
}

void tuh_midi_tx_cb(uint8_t device_address)
{
  (void)device_address;
}