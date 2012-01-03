#include <ruby.h>
#include <OpenAL/alc.h>
#include <OpenAL/al.h>
#include <unistd.h>

// How many audio buffers to keep
#define NUM_BUFFERS 3

// Globals
ID oa_iv_block;
ID oa_iv_format;
ID oa_iv_thread;
ID oa_id_call;
ID oa_id_kill;
ID oa_id_kill_thread;

// struct information stored
// with the OpenAL driver instance
typedef struct
{
  ALCdevice  *device;
  ALCcontext *context;
  ALuint buffers[NUM_BUFFERS];
  ALuint source;
} oa_struct_t;

// Utility

#define OA_CHECK_ERRORS do {                                \
  ALenum _error;                                            \
  if ((_error = alGetError()) != AL_NO_ERROR)               \
  {                                                         \
    rb_raise(rb_eRuntimeError, "OpenAL error: %d", _error); \
  }                                                         \
} while(0)

static inline oa_struct_t* oa_struct(VALUE self)
{
  oa_struct_t *data_ptr;
  Data_Get_Struct(self, oa_struct_t, data_ptr);
  return data_ptr;
}

static inline VALUE oa_get_audio(VALUE self, int frames)
{
  VALUE block   = rb_ivar_get(self, oa_iv_block);
  return rb_funcall(block, oa_id_call, 1, INT2FIX(frames));
}

// implementation

static void oa_free(oa_struct_t *data_ptr)
{
  int i;

  // exit early if we have no data_ptr at all
  if ( ! data_ptr) return;

  // deleting a source stops it from playing and
  // then destroys it
  if (data_ptr->source)
    alDeleteSources(1, &data_ptr->source);

  // now that the source is gone, we can safely delete buffers
  if (data_ptr->buffers[0])
    alDeleteBuffers(NUM_BUFFERS, data_ptr->buffers);

  // docs tell us to do this to make sure
  // our source is not the current one
  alcMakeContextCurrent(NULL);

  if (data_ptr->context)
    alcDestroyContext(data_ptr->context);

  if (data_ptr->device)
    alcCloseDevice(data_ptr->device);

  xfree(data_ptr);
}

static VALUE oa_allocate(VALUE klass)
{
  oa_struct_t *data_ptr;
  return Data_Make_Struct(klass, oa_struct_t, NULL, oa_free, data_ptr);
}

static VALUE oa_initialize(int argc, VALUE *argv, VALUE self)
{
  VALUE format, block;
  oa_struct_t *data_ptr;
  ALenum error = AL_NO_ERROR;

  rb_scan_args(argc, argv, "1&", &format, &block);

  // make sure we got the block argument
  if ( ! RTEST(block))
  {
    rb_raise(rb_eArgError, "missing block argument");
  }

  // we received a proc that we can call to
  // receive audio frames; so we store it
  rb_ivar_set(self, oa_iv_block, block);

  Data_Get_Struct(self, oa_struct_t, data_ptr);

  data_ptr->device = alcOpenDevice(NULL);
  if ( ! data_ptr->device)
  {
    rb_raise(rb_eRuntimeError, "failed to open device");
  }

  data_ptr->context = alcCreateContext(data_ptr->device, NULL);
  if ( ! data_ptr->context)
  {
    rb_raise(rb_eRuntimeError, "failed to create context");
  }

  // reset error state
  alGetError();

  alcMakeContextCurrent(data_ptr->context);
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

static VALUE oa_start(VALUE self)
{
  alSourcePlay(oa_struct(self)->source);
  OA_CHECK_ERRORS;
  return self;
}

static VALUE oa_stop(VALUE self)
{
  alSourceStop(oa_struct(self)->source);
  OA_CHECK_ERRORS;
  return self;
}

static VALUE oa_pause(VALUE self)
{
  alSourcePause(oa_struct(self)->source);
  OA_CHECK_ERRORS;
  return self;
}

static VALUE oa_drops(VALUE self)
{
  return INT2NUM(0);
}

static VALUE oa_set_format(VALUE self, VALUE format)
{
  rb_raise(rb_eNotImpError, "#format= is not yet implemented");
  return format;
}

static VALUE oa_get_format(VALUE self)
{
  return rb_ivar_get(self, oa_iv_format);
}

static VALUE oa_kill_thread(VALUE self)
{
  VALUE old_thread = rb_ivar_get(self, oa_iv_thread);

  if (RTEST(old_thread))
  {
    rb_funcall(old_thread, oa_id_kill, 0);
  }

  return self;
}

// ruby threading gooey stuff
typedef struct
{
  ALuint source;
  int abort;
} oa_wait_t;

static VALUE wait_for_empty_buffer(void *wait_ptr)
{
  oa_wait_t *wait = (oa_wait_t*)wait_ptr;
  int processed = 0;
  ALuint empty_buffer;

  while ( ! wait->abort && processed == 0)
  {
    alGetSourcei(wait->source, AL_BUFFERS_PROCESSED, &processed);
    usleep(100);
  }

   alSourceUnqueueBuffers(wait->source, 1, &empty_buffer);
   return (VALUE)empty_buffer;
}

static void abort_wait_for_empty_buffer(void *wait_ptr)
{
  oa_wait_t *wait = (oa_wait_t*)wait_ptr;
  wait->abort = 1;
}

static VALUE oa_thread_loop(void *id_self)
{
  const int frames_per_buffer = 22000;
  VALUE self  = (VALUE)id_self;
  VALUE block = rb_ivar_get(self, oa_iv_block);
  VALUE result = Qnil;

  oa_struct_t *data_ptr = oa_struct(self);

  oa_wait_t wait =
  {
    .source = data_ptr->source,
    .abort  = 0
  };

  while ( ! wait.abort)
  {
    result = rb_thread_blocking_region(wait_for_empty_buffer, &wait, abort_wait_for_empty_buffer, &wait);
    if ( ! wait.abort)
    {
      // check for errors, just to be safe
      OA_CHECK_ERRORS;

      // read the format
      int channels  = 2;
      int sample_rate = 441000;
      int format_size = sizeof(short);

      // pull 22,000 frames from Hallon; each frame is an array
      // of `channels` items, OpenAL wants it as a single array
      int num_samples = frames_per_buffer * channels;
      unsigned short samples[num_samples];
      VALUE frames = oa_get_audio(self, 22000);

      VALUE frame, sample;
      int i, rb_i, rb_j;
      for (i = 0; i < num_samples; ++i)
      {
        rb_i = i / channels; // integer division
        rb_j = i % channels;

        frame  = RARRAY_PTR(frames)[rb_i];
        sample = RARRAY_PTR(frame)[rb_j];

        unsigned long value = FIX2LONG(sample);

        samples[i] = value & 0xFFFF; // shorts are 16 bits
      }

      // pucker up all the params
      ALuint buffer = (ALuint)result;
      ALenum format = channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
      ALsizei size  = sizeof(samples);
      ALsizei freq  = sample_rate;

      // queue the data!
      alBufferData(buffer, format, samples, size, sample_rate);
    }
  }

  return Qtrue;
}

static VALUE oa_spawn_thread(VALUE self)
{
  // kill of any possible existing thread
  oa_kill_thread(self);

  VALUE thread = rb_thread_create(oa_thread_loop, (void *)self);
  rb_ivar_set(self, oa_iv_thread, thread);

  return self;
}


void Init_openal_ext(void)
{
  VALUE mHallon = rb_const_get(rb_cObject, rb_intern("Hallon"));
  VALUE cOpenAL = rb_define_class_under(mHallon, "OpenAL", rb_cObject);

  oa_iv_block  = rb_intern("@block");
  oa_iv_format = rb_intern("@format");
  oa_iv_thread = rb_intern("@thread");
  oa_id_call   = rb_intern("call");
  oa_id_kill   = rb_intern("kill");
  oa_id_kill_thread = rb_intern("kill_thread");

  rb_define_alloc_func(cOpenAL, oa_allocate);
  rb_define_method(cOpenAL, "initialize", oa_initialize, -1);

  rb_define_method(cOpenAL, "start", oa_start, 0);
  rb_define_method(cOpenAL, "stop", oa_stop, 0);
  rb_define_method(cOpenAL, "pause", oa_pause, 0);

  rb_define_method(cOpenAL, "drops", oa_drops, 0);

  rb_define_method(cOpenAL, "format=", oa_set_format, 1);
  rb_define_method(cOpenAL, "format", oa_get_format, 0);

  rb_define_private_method(cOpenAL, "spawn_thread", oa_spawn_thread, 0);
  rb_define_private_method(cOpenAL, "kill_thread", oa_kill_thread, 0);
}
