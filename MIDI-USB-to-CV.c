#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_midi_host.h"

// 125MHz/ 127 / 30 = 32kHz PWM
const uint WRAP_VAL = 127;
const float CKL_DIV = 30.0f;

// Raspberry Pi Pico GPIO pins
// NOTE_PIN and VELOCITY_PIN are used for PWM output
// GATE_PIN is used to control the gate signal
// TRIGGER_PIN outputs a momentary signal when a note is played
const uint8_t NOTE_PIN = 27;
const uint8_t VELOCITY_PIN = 26;
const uint8_t GATE_PIN = 22;
const uint8_t TRIGGER_PIN = 21;

static uint8_t midi_device_address = 0;

// PWM slice number and channel for note and velocity
uint slicenum;
uint note_channel;
uint velocity_channel;

uint8_t current_note = 0;
bool note_released = false;
bool sustain_active = false;

// Function prototypes
void note_on(uint8_t note, uint8_t velocity);
void note_off(uint8_t note);
void init_pwm();
void init_pins();
int64_t trigger_callback(alarm_id_t id, __unused void *user_data);

int main()
{
  // Initialize the board and peripherals
  stdio_init_all();
  board_init();
  tusb_init();
  init_pins();
  init_pwm();

  while (true)
  {
    tuh_task();
  }
}

// Initialise GPIO pins for digital output
void init_pins()
{
  gpio_init(GATE_PIN);
  gpio_set_dir(GATE_PIN, GPIO_OUT);
  gpio_init(TRIGGER_PIN);
  gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
}

// Initialise GPIO pins for PWM output
void init_pwm()
{
  gpio_set_function(NOTE_PIN, GPIO_FUNC_PWM);
  gpio_set_function(VELOCITY_PIN, GPIO_FUNC_PWM);

  slicenum = pwm_gpio_to_slice_num(NOTE_PIN);
  note_channel = pwm_gpio_to_channel(NOTE_PIN);
  velocity_channel = pwm_gpio_to_channel(VELOCITY_PIN);

  // set top register and clock division
  pwm_set_wrap(slicenum, WRAP_VAL);
  pwm_set_clkdiv(slicenum, CKL_DIV);

  pwm_set_mask_enabled(1u << slicenum);
}


int64_t trigger_callback(alarm_id_t id, __unused void *user_data)
{
  gpio_put(TRIGGER_PIN, false);
  return 0;
}


void note_on(uint8_t note, uint8_t velocity)
{
  note_released = false;
  gpio_put(GATE_PIN, true);
  gpio_put(TRIGGER_PIN, true);
  add_alarm_in_ms(50, trigger_callback, NULL, false);
  current_note = note;
  pwm_set_chan_level(slicenum, note_channel, note);
  pwm_set_chan_level(slicenum, velocity_channel, velocity);
}

void note_off(uint8_t note)
{
  note_released = true;
  if (note == current_note && !sustain_active)
  {
    gpio_put(GATE_PIN, false);
    pwm_set_chan_level(slicenum, note_channel, 0);
    pwm_set_chan_level(slicenum, velocity_channel, 0);
  }
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
  printf("MIDI device address = %u, IN endpoint %u has %u cables, OUT endpoint %u has %u cables\r\n",
         device_addr, in_ep & 0xf, num_cables_rx, out_ep & 0xf, num_cables_tx);

  if (midi_device_address == 0)
  {
    // then no MIDI device is currently connected
    midi_device_address = device_addr;
  }
  else
  {
    printf("A different USB MIDI Device is already connected.\r\nOnly one device at a time is supported in this program\r\nDevice is disabled\r\n");
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_midi_umount_cb(uint8_t device_addr, uint8_t instance)
{
  if (device_addr == midi_device_address)
  {
    midi_device_address = 0;
    printf("MIDI device address = %d, instance = %d is unmounted\r\n", device_addr, instance);
  }
  else
  {
    printf("Unused MIDI device address = %d, instance = %d is unmounted\r\n", device_addr, instance);
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
          note_on(buffer[1], buffer[2]);
          break;
        case 0x80: // note off (note number, velocity)
          note_off(buffer[1]);
          break;
        case 0xE0: // pitch wheel (LSB, MSB)
          break;
        case 0xB0: // MIDI CC
          switch (buffer[1])
          {
          case 0x01: // modwheel MSB
            break;
          case 0x07: // volume slider
            break;
          case 0x40: // sustain pedal
            if(buffer[2] < 0x3F)
            {
              sustain_active = false;
              if(note_released)
              {
                note_off(current_note);
              }
            } else {
              sustain_active = true;
            }
            break;
          case 0x7B: // all notes off
            sustain_active = false;
            note_released = true;
            note_off(current_note);
            break;
          default:
            break;
          }
        case 0xC0: // program change (program number)
        default:
          break;
        }

        // print out messages for debug
        for (uint32_t idx = 0; idx < bytes_read; idx++)
        {
          printf("%02x ", buffer[idx]);
        }
        printf("\r\n");
      }
    }
  }
}

void tuh_midi_tx_cb(uint8_t device_address)
{
  (void)device_address;
}