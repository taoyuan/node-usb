
#include <libusb.h>
#include "usb_ch9.h"
#include "node_usb.h"

Nan::Persistent<FunctionTemplate> Poller::constructor_template;

void __eio_poll(uv_work_t *req) {
  PollBaton *baton = static_cast<PollBaton *>(req->data);

  baton->result = 0;

  int rc = LIBUSB_SUCCESS;

  switch (baton->attributes & USB_ENDPOINT_XFERTYPE_MASK) {
    case USB_ENDPOINT_XFER_CONTROL:
      snprintf(baton->error, sizeof(baton->error), "Can't send on a control endpoint.");
      break;
    case USB_ENDPOINT_XFER_ISOC:
      //TODO handle isochronous
      snprintf(baton->error, sizeof(baton->error), "Isochronous endpoints unhandled.");
      break;
    case USB_ENDPOINT_XFER_BULK:
      rc = libusb_bulk_transfer(baton->handle, baton->endpoint, baton->data, baton->length, &baton->result, baton->timeout);
      if (rc == LIBUSB_SUCCESS)
        snprintf(baton->error, sizeof(baton->error), "received bulk msg (%d bytes)", baton->result);
      break;
    case USB_ENDPOINT_XFER_INT:
      rc = libusb_interrupt_transfer(baton->handle, baton->endpoint, baton->data, baton->length, &baton->result, baton->timeout);
      if (rc == LIBUSB_SUCCESS)
        snprintf(baton->error, sizeof(baton->error), "received interrupt msg (%d bytes)", baton->result);
      break;
    default:;
  }
  if (rc != LIBUSB_SUCCESS) {
    baton->result = 0;
    snprintf(baton->error, sizeof(baton->error),
             "Transfer error on EP%02x (xfertype %d): %s",
             baton->endpoint & 0xFF,
             unsigned(baton->attributes & USB_ENDPOINT_XFERTYPE_MASK),
             libusb_strerror((libusb_error) rc)
    );
  }
}

void __eio_poll_done(uv_work_t *req) {
  Nan::HandleScope scope;

  PollBaton *baton = static_cast<PollBaton *>(req->data);

  Local<Value> argv[2];
  if (baton->error[0]) {
    argv[0] = Nan::Error(baton->error);
    argv[1] = Nan::Undefined();
  } else {
    argv[0] = Nan::Undefined();
    argv[1] = Nan::New(baton->result);
  }

  DEBUG_LOG("poll doen (device: %d, endpoint: %d, result: %d)",
            baton->handle,
            baton->endpoint,
            baton->result);

  baton->callback->Call(2, argv);

  baton->buffer.Reset();
  delete baton->callback;
  delete baton;
  delete req;
}

Poller::Poller() {

}

Poller::~Poller() {

}

NAN_METHOD(Poller::New) {
  ENTER_CONSTRUCTOR(1);

  UNWRAP_ARG(Device, device, 0);


  Poller *p = new Poller();
  p->_device = device;

  p->Wrap(info.This());
  p->This.Reset(info.This());
  info.GetReturnValue().Set(info.This());
}

NAN_METHOD(Poller::Poll) {
  ENTER_METHOD(Poller, 5);

  int endpoint, attributes, timeout;
  INT_ARG(endpoint, 0);
  INT_ARG(attributes, 1);

  if (!Buffer::HasInstance(info[2])) {
    THROW_BAD_ARGS("Buffer arg [2] must be Buffer");
  }
  Local<Object> buffer = info[2]->ToObject();
  INT_ARG(timeout, 3);
  CALLBACK_ARG(4);

  if (!self->_device->device_handle){
    THROW_ERROR("Device is not open");
  }

  unsigned char *data = (unsigned char *)node::Buffer::Data(buffer);
  int length = (int) node::Buffer::Length(buffer);

  PollBaton *baton = new PollBaton();
  memset(baton, 0, sizeof(PollBaton));
  baton->handle = self->_device->device_handle;
  baton->endpoint = (unsigned char) endpoint;
  baton->attributes = (unsigned char) attributes;
  baton->buffer.Reset(buffer);
  baton->data = data;
  baton->length = length;
  baton->timeout = (unsigned int) (timeout < 10 ? 10 : timeout);
  baton->callback = new Nan::Callback(callback);

  uv_work_t* req = new uv_work_t();
  req->data = baton;

  uv_queue_work(uv_default_loop(), req, __eio_poll, (uv_after_work_cb) __eio_poll_done);

  info.GetReturnValue().Set(Nan::New(req));

}

NAN_METHOD(Poller::Cancel) {

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
