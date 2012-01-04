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
ID oa_id_spawn_thread;

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

#define OA_CHECK_ERRORS(msg) do {                           \
  ALenum _error;                                            \
  if ((_error = alGetError()) != AL_NO_ERROR)               \
  {                                                         \
    rb_raise(rb_eRuntimeError, "OpenAL error: %u (%s)", _error, (msg)); \
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
  OA_CHECK_ERRORS("context current");

  // Set some defualt properties
  alListenerf(AL_GAIN, 1.0f);
	alDistanceModel(AL_NONE);
  OA_CHECK_ERRORS("listener/distance");

  // generate some buffers
	alGenBuffers((ALsizei)NUM_BUFFERS, data_ptr->buffers);
  OA_CHECK_ERRORS("generate buffers");

  // generate our source
	alGenSources(1, &data_ptr->source);
  OA_CHECK_ERRORS("gen sources");

  // spawn thread
  rb_funcall(self, oa_id_spawn_thread, 0);
}

static VALUE oa_start(VALUE self)
{
  alSourcePlay(oa_struct(self)->source);
  OA_CHECK_ERRORS("play!");
  return self;
}

static VALUE oa_stop(VALUE self)
{
  alSourceStop(oa_struct(self)->source);
  OA_CHECK_ERRORS("stop!");
  return self;
}

static VALUE oa_pause(VALUE self)
{
  alSourcePause(oa_struct(self)->source);
  OA_CHECK_ERRORS("pause!");
  return self;
}

static VALUE oa_drops(VALUE self)
{
  return INT2NUM(0);
}

static VALUE oa_set_format(VALUE self, VALUE format)
{
  // rb_raise(rb_eNotImpError, "#format= is not yet implemented");
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

static ALuint find_empty_buffer(VALUE self)
{
  ALuint empty_buffer;

  oa_struct_t *data_ptr = oa_struct(self);
  ALuint source   = data_ptr->source;
  ALuint *buffers = data_ptr->buffers;

  ALint num_queued = 0;
  alGetSourcei(source, AL_BUFFERS_QUEUED, &num_queued);
  OA_CHECK_ERRORS("AL_BUFFERS_QUEUED");

  if (num_queued < NUM_BUFFERS)
  {
    empty_buffer = buffers[num_queued];
  }
  else
  {
    ALint state;
    int processed;
    struct timeval poll_time;
    poll_time.tv_sec  = 0;
    poll_time.tv_usec = 100;	/* 0.000100 sec */

    for (processed = 0; processed == 0; rb_thread_wait_for(poll_time))
    {
      alGetSourcei(source, AL_SOURCE_STATE, &state);
      OA_CHECK_ERRORS("AL_SOURCE_STATE");

      if (state != AL_PLAYING) continue;

      alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
      OA_CHECK_ERRORS("AL_BUFFERS_PROCESSED");
    }

    alSourceUnqueueBuffers(source, 1, &empty_buffer);
    OA_CHECK_ERRORS("alSourceUnqueueBuffers");
  }

  return empty_buffer;
}

static VALUE oa_thread_loop(void *id_self)
{
  const int max_frames_per_buffer = 22000;
  VALUE self = (VALUE)id_self;
  ALuint source = oa_struct(self)->source;

  for (;;)
  {
    ALuint buffer = find_empty_buffer(self);
    OA_CHECK_ERRORS("find_empty_buffer");

    // pull some audio out of hallon
    VALUE frames = oa_get_audio(self, max_frames_per_buffer);

    // read the format
    int channels    = 2;
    int sample_rate = 44100;
    int format_size = sizeof(short);

    // convert the frames from ruby to C
    int max_samples = max_frames_per_buffer * channels;
    signed short samples[max_samples];
    long num_samples = RARRAY_LEN(frames) * channels;

    VALUE frame, sample;
    int i, rb_i, rb_j;
    for (i = 0; i < num_samples; ++i)
    {
      rb_i = i / channels; // integer division
      rb_j = i % channels;

      frame  = RARRAY_PTR(frames)[rb_i];
      sample = RARRAY_PTR(frame)[rb_j];

      long value = FIX2LONG(sample);

      samples[i] = (short) value; // shorts are 16 bits
    }

    printf("%d +%ld\n", buffer, num_samples);

    // pucker up all the params
    ALenum format = channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    ALsizei size  = sizeof(short) * num_samples;
    ALsizei freq  = sample_rate;

    // queue the data!
    alBufferData(buffer, format, samples, size, sample_rate);
    OA_CHECK_ERRORS("buffer data");

    alSourceQueueBuffers(source, 1, &buffer);
    OA_CHECK_ERRORS("queue a buffer");
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
  oa_id_spawn_thread = rb_intern("spawn_thread");

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
