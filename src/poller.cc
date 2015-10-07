#include <libusb.h>
#include "usb_ch9.h"
#include "node_usb.h"

Nan::Persistent<FunctionTemplate> Poller::constructor_template;

void __eio_poll(uv_work_t *req) {
  PollBaton *baton = static_cast<PollBaton *>(req->data);

  baton->result = 0;

  int rc = LIBUSB_SUCCESS;

  while (baton->active) {
    switch (baton->attributes & USB_ENDPOINT_XFERTYPE_MASK) {
      case USB_ENDPOINT_XFER_CONTROL:
      DEBUG_LOG("Can't send on a control endpoint.");
        break;
      case USB_ENDPOINT_XFER_ISOC:
        //TODO handle isochronous
      DEBUG_LOG("Isochronous endpoints unhandled.");
        break;
      case USB_ENDPOINT_XFER_BULK:
        rc = libusb_bulk_transfer(baton->handle, baton->endpoint, baton->data, baton->length, &baton->result,
                                  baton->timeout);
        if (rc == LIBUSB_SUCCESS && baton->result) {
          DEBUG_LOG("received bulk msg (%d bytes)", baton->result);
        }
        break;
      case USB_ENDPOINT_XFER_INT:
        rc = libusb_interrupt_transfer(baton->handle, baton->endpoint, baton->data, baton->length, &baton->result,
                                       baton->timeout);
        if (rc == LIBUSB_SUCCESS && baton->result) {
          DEBUG_LOG("received interrupt msg (%d bytes)", baton->result);
        }
        break;
      default:;
    }
    baton->errcode = rc;
    if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_TIMEOUT) {
      baton->result = 0;
      DEBUG_LOG("Transfer error on EP%02x (xfertype %d): %s", baton->endpoint & 0xFF,
                unsigned(baton->attributes & USB_ENDPOINT_XFERTYPE_MASK), libusb_strerror((libusb_error) rc));
      break;
    }
    if (baton->result) break;
  }
}

void __eio_poll_done(uv_work_t *req, int status) {
  Nan::HandleScope scope;

  PollBaton *baton = static_cast<PollBaton *>(req->data);

  if (status == UV_ECANCELED) {
    DEBUG_LOG("Pollation of %p cancelled", req->data);
  } else {
    DEBUG_LOG("poll done (device: %p, endpoint: %d, result: %d)", baton->handle, baton->endpoint, baton->result);

    if (!baton->callback->IsEmpty()) {
      Local<Value> error = Nan::Undefined();
      if (baton->errcode != 0) {
        error = libusbException(baton->errcode);
      }
      Local<Object> buffer = Nan::New<Object>(baton->buffer);

      Local<Value> argv[] = {error, buffer, Nan::New(baton->result)};

      // reset baton before callback for poll in callback
      baton->reset();

      DEBUG_LOG("call poll callback");
      Nan::TryCatch try_catch;
      baton->callback->Call(3, argv);
      if (try_catch.HasCaught()) {
        Nan::FatalException(try_catch);
      }

      return;
    }
  }

  baton->reset();

}

Poller::Poller() {
  DEBUG_LOG("Created Poller %p", this);
}

Poller::~Poller() {
  DEBUG_LOG("Freed Poller %p", this);
  baton.destory();
}

// Poller(device, endpoint, attributes, timeout, callback)
NAN_METHOD(Poller::New) {
  ENTER_CONSTRUCTOR(5);

  UNWRAP_ARG(Device, device, 0);
  int endpoint, attributes, timeout;
  INT_ARG(endpoint, 1);
  INT_ARG(attributes, 2);
  INT_ARG(timeout, 3);
  CALLBACK_ARG(4);

  Poller *p = new Poller();
  memset(&p->baton, 0, sizeof(PollBaton));

  p->device = device;

  p->baton.endpoint = (unsigned char) endpoint;
  p->baton.attributes = (unsigned char) attributes;
  p->baton.timeout = (unsigned int) (timeout < 10 ? 10 : timeout);
  p->baton.callback = new Nan::Callback(callback);

  p->Wrap(info.This());
  p->This.Reset(info.This());
  info.GetReturnValue().Set(info.This());
}

// poll(buffer)
NAN_METHOD(Poller::Poll) {
  ENTER_METHOD(Poller, 1);

  if (!self->device->device_handle) {
    THROW_ERROR("Device is not open");
  }

  if (self->baton.req) {
    THROW_ERROR("Poller is already active");
  }

  if (!Buffer::HasInstance(info[0])) {
    THROW_BAD_ARGS("Buffer arg [0] must be Buffer");
  }
  Local<Object> buffer = info[0]->ToObject();

  unsigned char *data = (unsigned char *) node::Buffer::Data(buffer);
  int length = (int) node::Buffer::Length(buffer);

  self->baton.active = true;
  self->baton.handle = self->device->device_handle;
  self->baton.buffer.Reset(buffer);
  self->baton.data = data;
  self->baton.length = length;

  uv_work_t *req = new uv_work_t();
  req->data = &self->baton;
  self->baton.req = req;

  DEBUG_LOG(
    "Polling (device_handle: %p, device_handle: %p, endpoint: 0x%x, attributes: %i, timeout: %i, length: %i, buffer: %p)",
    self,
    self->device->device_handle,
    self->baton.endpoint,
    self->baton.attributes,
    self->baton.timeout,
    self->baton.length,
    self->baton.data
  );

  uv_queue_work(uv_default_loop(), req, __eio_poll, (uv_after_work_cb) __eio_poll_done);

  info.GetReturnValue().SetUndefined();

}

// cancel()
NAN_METHOD(Poller::Cancel) {
  ENTER_METHOD(Poller, 0);

  if (self->baton.req) { // not executed
    int rc = uv_cancel((uv_req_t *) self->baton.req);
    if (rc != 0) { // executing
      self->baton.active = false;
    }
  }

  info.GetReturnValue().SetUndefined();
}

void Poller::Init(Local<Object> target) {
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);

  tpl->SetClassName(Nan::New("Poller").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  Nan::SetPrototypeMethod(tpl, "poll", Poll);
  Nan::SetPrototypeMethod(tpl, "cancel", Cancel);

  constructor_template.Reset(tpl);
  target->Set(Nan::New("Poller").ToLocalChecked(), tpl->GetFunction());
}
