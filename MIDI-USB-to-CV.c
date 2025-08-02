#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"
#include "pico/binary_info.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_midi_host.h"

#include "hardware/irq.h"

// 125,000,000 / 127 / 30 = 32kHz
const uint WRAP_VAL = 127;
const float CKL_DIV = 30.0f;

const uint8_t NOTE_PIN = 27;
const uint8_t VELOCITY_PIN = 26;
const uint8_t GATE_PIN = 22;
const uint8_t TRIGGER_PIN = 21;

static uint8_t midi_dev_addr = 0;

uint slicenum;
uint note_channel;
uint velocity_channel;

bool should_trigger = false;
uint wrap_count = 0;

void on_pwm_wrap();
void note_on(uint8_t note, uint8_t velocity);
void note_off();
void init_pwm();
void init_pins();

int main()
{
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

void init_pins()
{
  gpio_init(GATE_PIN);
  gpio_set_dir(GATE_PIN, GPIO_OUT);
  gpio_init(TRIGGER_PIN);
  gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
}

void init_pwm()
{
  gpio_set_function(NOTE_PIN, GPIO_FUNC_PWM);
  gpio_set_function(VELOCITY_PIN, GPIO_FUNC_PWM);

  slicenum = pwm_gpio_to_slice_num(NOTE_PIN);
  note_channel = pwm_gpio_to_channel(NOTE_PIN);
  velocity_channel = pwm_gpio_to_channel(VELOCITY_PIN);

  // register the interrupt handler for the PWM slice
  // interrupt fires when one PWM cycle is completed
  pwm_clear_irq(slicenum);
  pwm_set_irq_enabled(slicenum, true);
  irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
  irq_set_enabled(PWM_IRQ_WRAP, true);

  // set top register and clock division
  pwm_set_wrap(slicenum, WRAP_VAL);
  pwm_set_clkdiv(slicenum, CKL_DIV);

  pwm_set_mask_enabled(1u << slicenum);
}

// TODO: replace trigger code with timer rather than using PWM IRQ
void on_pwm_wrap()
{
  pwm_clear_irq(slicenum);

  if (should_trigger)
  {
    gpio_put(TRIGGER_PIN, true);
    wrap_count++;
    if (wrap_count > 3000)
    {
      wrap_count = 0;
      should_trigger = false;
      gpio_put(TRIGGER_PIN, false);
    }
  }
}

void note_on(uint8_t note, uint8_t velocity)
{
  gpio_put(GATE_PIN, true);
  should_trigger = true;
  pwm_set_chan_level(slicenum, note_channel, note);
  pwm_set_chan_level(slicenum, velocity_channel, velocity);
}

void note_off()
{
  gpio_put(GATE_PIN, false);
  pwm_set_chan_level(slicenum, note_channel, 0);
  pwm_set_chan_level(slicenum, velocity_channel, 0);
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep, uint8_t num_cables_rx, uint16_t num_cables_tx)
{
  printf("MIDI device address = %u, IN endpoint %u has %u cables, OUT endpoint %u has %u cables\r\n",
         dev_addr, in_ep & 0xf, num_cables_rx, out_ep & 0xf, num_cables_tx);

  if (midi_dev_addr == 0)
  {
    // then no MIDI device is currently connected
    midi_dev_addr = dev_addr;
  }
  else
  {
    printf("A different USB MIDI Device is already connected.\r\nOnly one device at a time is supported in this program\r\nDevice is disabled\r\n");
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  if (dev_addr == midi_dev_addr)
  {
    midi_dev_addr = 0;
    printf("MIDI device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
  }
  else
  {
    printf("Unused MIDI device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
  }
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
  if (midi_dev_addr == dev_addr)
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
          note_off();
          break;
        case 0xE0: // pitch wheel (LSB, MSB)
          break;
        case 0xB0: // MIDI CC
          switch (buffer[1])
          {
          case 0x01: // modwheel MSB
          case 0x07: // volume slider
          case 0x40: // sustain pedal
          case 0x7B: // all notes off
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

void tuh_midi_tx_cb(uint8_t dev_addr)
{
  (void)dev_addr;
}