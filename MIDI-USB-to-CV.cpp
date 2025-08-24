#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_midi_host.h"
#include "pico-ssd1306/ssd1306.h"
#include "pico-ssd1306/textRenderer/TextRenderer.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"

struct PWMConfig
{
  uint8_t pin;
  uint8_t slice_num;
  uint8_t channel;
};

// 125MHz/ 127 / 30 = 32kHz PWM
const uint WRAP_VAL = 127;
const float CKL_DIV = 30.0f;

const uint8_t MAX_ADC_SAMPLES = 4; 
const uint INPUT_POLL_INTERVAL_MS = 50;
const uint DISPLAY_UPDATE_INTERVAL_MS = 200;

// Raspberry Pi Pico GPIO pins
// GATE_PIN outputs high when a note is being played
// ARPEGGIATOR_PIN reads the state of a button to toggle arpeggiator mode
// BPM_PIN reads a potentiometer to set the BPM
const uint8_t BPM_PIN = 28;
const uint8_t ARPEGGIATOR_PIN = 6;
const uint8_t GATE_PIN = 22;

PWMConfig note_pwm = {27, 0, 0};
PWMConfig velocity_pwm = {26, 0, 0};
PWMConfig modulation_pwm = {22, 0, 0};

static uint8_t midi_device_address = 0;

// Global state variable
struct
{
  uint8_t current_note = 0;
  uint8_t current_velocity = 0;
  uint8_t modulation_level = 0;
  uint8_t current_bpm = 0;
  bool sustain_active = false;
  bool arpeggiator_active = false;
} program_state;

uint16_t adc_samples[MAX_ADC_SAMPLES];
uint8_t adc_sample_index = 0;

// Function prototypes
void init_pwm(PWMConfig &pwm_config);
void init_pins();
void poll_inputs();
void update_display(pico_ssd1306::SSD1306 &display);
void update_outputs();

int main()
{
  // Initialize the board and peripherals
  stdio_init_all();
  board_init();
  adc_init();
  tusb_init();
  init_pins();
  init_pwm(note_pwm);
  init_pwm(velocity_pwm);
  init_pwm(modulation_pwm);
  
  pico_ssd1306::SSD1306 display = pico_ssd1306::SSD1306(i2c0, 0x3C, pico_ssd1306::Size::W128xH64);
  sleep_ms(250);
  display.setOrientation(0);
  
  uint32_t display_update_time = 0;
  uint32_t input_update_time = 0;

  while (true)
  {
    tuh_task();
    update_outputs();
    
    if (to_ms_since_boot(get_absolute_time()) >= input_update_time)
    {
      poll_inputs();
      input_update_time = to_ms_since_boot(get_absolute_time()) + INPUT_POLL_INTERVAL_MS; 
    }

    if (to_ms_since_boot(get_absolute_time()) >= display_update_time)
    {
      update_display(display);
      display_update_time = to_ms_since_boot(get_absolute_time()) + DISPLAY_UPDATE_INTERVAL_MS;
    }
  }
}

// Initialise GPIO pins for digital output
void init_pins()
{
  gpio_init(GATE_PIN);
  gpio_set_dir(GATE_PIN, GPIO_OUT);

  gpio_init(ARPEGGIATOR_PIN);
  gpio_set_dir(ARPEGGIATOR_PIN, GPIO_IN);

  adc_gpio_init(BPM_PIN);
  adc_select_input(BPM_PIN - ADC_BASE_PIN);

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

uint16_t map(uint16_t x, uint16_t in_min, uint16_t in_max, uint16_t out_min, uint16_t out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void poll_inputs()
{
  adc_samples[adc_sample_index] = adc_read();
  if (adc_sample_index < MAX_ADC_SAMPLES - 1) {
    adc_sample_index++;
  } else {
    adc_sample_index = 0;
    uint16_t adc_average = 0;
    for (uint8_t i = 0; i < MAX_ADC_SAMPLES; i++) {
      adc_average += adc_samples[i];
    }
    adc_average /= MAX_ADC_SAMPLES;
    program_state.current_bpm = map(adc_average >> 4, 0, 256-2, 30, 180);
  }

  program_state.arpeggiator_active = gpio_get(ARPEGGIATOR_PIN);
}

void update_display(pico_ssd1306::SSD1306 &display)
{
  display.clear();
  
  if (program_state.arpeggiator_active)
  {
    drawChar(&display, font_12x16, 'A', 0, 64-16);
  }

  char bpm_string[1];
  sprintf(bpm_string, "%03d", program_state.current_bpm);
  drawText(&display, font_16x32, bpm_string, 40, 20);

  display.sendBuffer();
}

void update_outputs()
{
  if(program_state.current_note > 0)
  {
    gpio_put(GATE_PIN, true);
    pwm_set_chan_level(note_pwm.slice_num, note_pwm.channel, program_state.current_note);
    pwm_set_chan_level(velocity_pwm.slice_num, velocity_pwm.channel, program_state.current_velocity);
  } 
  else if (!program_state.sustain_active) 
  {
    gpio_put(GATE_PIN, false);
    pwm_set_chan_level(note_pwm.slice_num, note_pwm.channel, 0);
    pwm_set_chan_level(velocity_pwm.slice_num, velocity_pwm.channel, 0);
  }
  pwm_set_chan_level(modulation_pwm.slice_num, modulation_pwm.channel, program_state.modulation_level);
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
          program_state.current_note = buffer[1];
          program_state.current_velocity = buffer[2];
          break;
        case 0x80: // note off (note number, velocity)
          if (buffer[1] == program_state.current_note)
          {
            program_state.current_note = 0;
          }
          break;
        case 0xE0: // pitch wheel (LSB, MSB)
          break;
        case 0xB0: // MIDI CC
          switch (buffer[1])
          {
          case 0x01: // modwheel MSB
            program_state.modulation_level = buffer[2];
            break;
          case 0x07: // volume slider
            break;
          case 0x40: // sustain pedal
            program_state.sustain_active = buffer[2] < 0x3F ? false : true;
            break;
          case 0x7B: // all notes off
            program_state.sustain_active = false;
            program_state.current_note = 0;
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