// Xbox 360 wired-controller USB interfaces (vendor class FF/5D/01) as a TinyUSB app class driver.
// One interface per wireless controller.
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "device/usbd_pvt.h"
#include "xinput.h"

class XInputInterface : public Adafruit_USBD_Interface {
public:
  uint16_t getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t *buf, uint16_t bufsize) override {
    (void)itfnum_deprecated;
    const uint16_t len = 9 + 17 + 7 + 7;
    if (!buf) return len;
    if (bufsize < len) return 0;
    uint8_t itf = TinyUSBDevice.allocInterface(1);
    uint8_t ep_in = TinyUSBDevice.allocEndpoint(1);
    uint8_t ep_out = TinyUSBDevice.allocEndpoint(0);
    uint8_t d[] = {
      9, TUSB_DESC_INTERFACE, itf, 0, 2, 0xFF, 0x5D, 0x01, 0,
      17, 0x21, 0x00, 0x01, 0x01, 0x25, ep_in, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13, ep_out, 0x08, 0x00, 0x00,
      7, TUSB_DESC_ENDPOINT, ep_in, TUSB_XFER_INTERRUPT, 32, 0, 1,
      7, TUSB_DESC_ENDPOINT, ep_out, TUSB_XFER_INTERRUPT, 32, 0, 8,
    };
    memcpy(buf, d, len);
    return len;
  }
};
static XInputInterface xi_itf[XI_MAX];

struct XiPort { uint8_t ep_in, ep_out; uint8_t out_buf[32]; uint8_t tx[32]; };
static XiPort port[XI_MAX];
static uint8_t n_open = 0;
volatile uint8_t xinput_rumble_l[XI_MAX], xinput_rumble_r[XI_MAX];

static void xi_init(void) {}
static bool xi_deinit(void) { return true; }
static void xi_reset(uint8_t rhport) { (void)rhport; memset(port, 0, sizeof port); n_open = 0; }
static uint16_t xi_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len) {
  if (itf->bInterfaceClass != 0xFF || itf->bInterfaceSubClass != 0x5D || n_open >= XI_MAX) return 0;
  XiPort &P = port[n_open];
  uint8_t const *p = (uint8_t const *)itf;
  uint8_t const *end = p + max_len;
  uint16_t used = tu_desc_len(p);
  p = tu_desc_next(p);
  uint8_t eps = 0;
  while (p < end && eps < itf->bNumEndpoints) {
    if (tu_desc_type(p) == TUSB_DESC_INTERFACE) break;
    if (tu_desc_type(p) == TUSB_DESC_ENDPOINT) {
      tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
      if (!usbd_edpt_open(rhport, ep)) return 0;
      if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) P.ep_in = ep->bEndpointAddress;
      else P.ep_out = ep->bEndpointAddress;
      eps++;
    }
    used += tu_desc_len(p);
    p = tu_desc_next(p);
  }
  if (P.ep_out) usbd_edpt_xfer(rhport, P.ep_out, P.out_buf, sizeof P.out_buf);
  n_open++;
  return used;
}
static bool xi_control(uint8_t, uint8_t, tusb_control_request_t const *) { return false; }
static bool xi_xfer(uint8_t rhport, uint8_t ep, xfer_result_t, uint32_t n) {
  for (int i = 0; i < n_open; i++) {
    if (ep == port[i].ep_out) {
      uint8_t *b = port[i].out_buf;
      if (n >= 5 && b[0] == 0x00 && b[1] == 0x08) { xinput_rumble_l[i] = b[3]; xinput_rumble_r[i] = b[4]; }
      usbd_edpt_xfer(rhport, ep, b, sizeof port[i].out_buf);
    }
  }
  return true;
}
static usbd_class_driver_t const xi_driver = {
  .name = "XINPUT", .init = xi_init, .deinit = xi_deinit, .reset = xi_reset, .open = xi_open,
  .control_xfer_cb = xi_control, .xfer_cb = xi_xfer, .sof = NULL,
};
extern "C" usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *count) { *count = 1; return &xi_driver; }

void xinput_add(uint8_t count) { for (int i = 0; i < count && i < XI_MAX; i++) TinyUSBDevice.addInterface(xi_itf[i]); }
bool xinput_ready(uint8_t i) { return i < n_open && port[i].ep_in && tud_ready() && !usbd_edpt_busy(0, port[i].ep_in); }
bool xinput_send(uint8_t i, const uint8_t *buf, uint16_t len) {
  if (!usbd_edpt_claim(0, port[i].ep_in)) return false;
  memcpy(port[i].tx, buf, len);
  bool ok = usbd_edpt_xfer(0, port[i].ep_in, port[i].tx, len);
  if (!ok) usbd_edpt_release(0, port[i].ep_in);
  return ok;
}
