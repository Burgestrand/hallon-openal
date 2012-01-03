#include <ruby.h>
#include <OpenAL/alc.h>
#include <OpenAL/al.h>

// Macros

#define OA_CHECK_ERRORS do {                                 \
  if ((error = alGetError()) != AL_NO_ERROR)                \
  {                                                         \
    rb_raise(rb_eRuntimeError, "OpenAL error: %d", error);  \
  }                                                         \
} while(0)

// How many audio buffers to keep
#define NUM_BUFFERS 3

// struct information stored
// with the OpenAL driver instance
typedef struct
{
  ALCdevice  *device;
  ALuint buffers[NUM_BUFFERS];
  ALuint source;
} oa_struct_t;

// Globals
ID oa_iv_block;

static VALUE oa_free(int *device)
{
}

static VALUE oa_allocate(VALUE klass)
{
  oa_struct_t *data_ptr;
  return Data_Make_Struct(klass, oa_struct_t, NULL, oa_free, data_ptr);
}

static VALUE oa_initialize(int argc, VALUE *argv, VALUE self)
{
  VALUE format, block;

  rb_scan_args(argc, argv, "1&", &format, &block);

  // make sure we got the block argument
  if ( ! RTEST(block))
  {
    rb_raise(rb_eArgError, "missing block argument");
  }

  // we received a proc that we can call to
  // receive audio frames; so we store it
  rb_ivar_set(self, oa_iv_block, block);

  oa_struct_t *data_ptr;
  Data_Get_Struct(self, oa_struct_t, data_ptr);

  data_ptr->device = alcOpenDevice(NULL);
  if ( ! data_ptr->device)
  {
    rb_raise(rb_eRuntimeError, "failed to open device");
  }

  ALCcontext *context = alcCreateContext(data_ptr->device, NULL);
  if ( ! context)
  {
    rb_raise(rb_eRuntimeError, "failed to create context");
  }

  // reset error state
  ALenum error = AL_NO_ERROR;
  alGetError();

  alcMakeContextCurrent(context);
  OA_CHECK_ERRORS;

  // Set some defualt properties
  alListenerf(AL_GAIN, 1.0f);
	alDistanceModel(AL_NONE);
  OA_CHECK_ERRORS;

  // generate some buffers
	alGenBuffers((ALsizei)NUM_BUFFERS, data_ptr->buffers);
  OA_CHECK_ERRORS;

  // generate our source
	alGenSources(1, &data_ptr->source);
  OA_CHECK_ERRORS;
}

void Init_openal_ext(void)
{
  oa_iv_block = rb_intern("@block");

  VALUE mHallon = rb_const_get(rb_cObject, rb_intern("Hallon"));
  VALUE cOpenAL = rb_define_class_under(mHallon, "OpenAL", rb_cObject);

  rb_define_alloc_func(cOpenAL, oa_allocate);
  rb_define_method(cOpenAL, "initialize", oa_initialize, -1);
}
