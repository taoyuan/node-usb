#ifndef SRC_NODE_USB_H
#define SRC_NODE_USB_H

#include <assert.h>
#include <string>
#include <map>

#ifdef _WIN32
#include <WinSock2.h>
#endif

#include <libusb.h>
#include <v8.h>

#include <node.h>
#include <node_buffer.h>
#include <uv.h>

using namespace v8;
using namespace node;

#include "helpers.h"

//#define DEBUG

Local<Value> libusbException(int errorno);

struct DeviceObject {
  Nan::Persistent<Object> obj;
};

struct Device : public Nan::ObjectWrap {
  libusb_device *device;
  libusb_device_handle *device_handle;

  static void Init(Local<Object> exports);

  static Local<Object> get(libusb_device *handle);

  inline void ref() { Ref(); }

  inline void unref() { Unref(); }

  inline bool canClose() { return refs_ == 0; }

  inline void attach(Local<Object> o) { Wrap(o); }

  ~Device();

  static void unpin(libusb_device *device);

protected:
  // static std::map<libusb_device*, WeakCallbackInfo<libusb_device>*> byPtr;
  // static std::map<libusb_device*, Persistent<Value>*> byPtr;
  static std::map<libusb_device *, DeviceObject *> byPtr;

  Device(libusb_device *d);
};


struct Transfer : public Nan::ObjectWrap {
  libusb_transfer *transfer;
  Device *device;
  Nan::Persistent<Object> v8buffer;
  Nan::Persistent<Function> v8callback;

  static void Init(Local<Object> exports);

  inline void ref() { Ref(); }

  inline void unref() { Unref(); }

  inline void attach(Local<Object> o) { Wrap(o); }

  Transfer();

  ~Transfer();
};

struct PollBaton {
public:
  bool active;

  libusb_device_handle *handle;
  unsigned char endpoint;
  int attributes;
  unsigned int timeout;
  Nan::Callback *callback;

  Nan::Persistent<Object> buffer;
  unsigned char *data;
  int length;

  int result;
  int errcode;

  uv_work_t *req;

  void reset() {
    buffer.Reset();
    data = 0;
    length = 0;

    if (req) delete req;
    req = 0;

    result = 0;
    errcode = 0;
  }

  void destory() {
    reset();
    if (callback) delete callback;
    callback = 0;
  }
};

class Poller : public Nan::ObjectWrap {
public:
  static void Init(Local<Object> exports);

  static NAN_METHOD(New);

  static NAN_METHOD(Poll);

  static NAN_METHOD(Cancel);

  PollBaton baton;

private:

  Poller();

  ~Poller();

//	int poll(int endpoint, int attributes, char *buffer, int len, Nan::Persistent<Function> callback);

private:
  Nan::Persistent<v8::Object> This;

  Device *device;

  static Nan::Persistent<FunctionTemplate> constructor_template;
};

#define CHECK_USB(r) \
  if (r < LIBUSB_SUCCESS) { \
    return Nan::ThrowError(libusbException(r)); \
  }

#define CALLBACK_ARG(CALLBACK_ARG_IDX) \
  Local<Function> callback; \
  if (info.Length() > (CALLBACK_ARG_IDX)) { \
    if (!info[CALLBACK_ARG_IDX]->IsFunction()) { \
      return Nan::ThrowTypeError("Argument " #CALLBACK_ARG_IDX " must be a function"); \
    } \
    callback = Local<Function>::Cast(info[CALLBACK_ARG_IDX]); \
  } \

#ifdef DEBUG
#define DEBUG_HEADER fprintf(stderr, "node-usb [%s:%s() %d]: ", __FILE__, __FUNCTION__, __LINE__);
#define DEBUG_FOOTER fprintf(stderr, "\n");
#define DEBUG_LOG(...) DEBUG_HEADER fprintf(stderr, __VA_ARGS__); DEBUG_FOOTER
#else
#define DEBUG_LOG(...)
#endif

#endif
