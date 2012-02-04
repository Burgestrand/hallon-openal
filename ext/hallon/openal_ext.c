#include <ruby.h>
#include <OpenAL/alc.h>
#include <OpenAL/al.h>
#include <unistd.h>

#if DEBUG_H
#  define DEBUG printf
#else
#  define DEBUG(...) //
#endif

// How many audio buffers to keep
#define NUM_BUFFERS 3

// Globals
ID oa_iv_playing;
ID oa_iv_format;
ID oa_id_call;
ID oa_id_puts;
VALUE oa_key_channels;
VALUE oa_key_rate;
VALUE oa_key_type;

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

#define SYM2STR(x) rb_id2name(SYM2ID((x)))
#define STR2SYM(x) ID2SYM(rb_intern((x)))

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

static inline void oa_ensure_playing(VALUE self)
{
  ALint state;
  ALuint source = oa_struct(self)->source;
  VALUE playing = rb_ivar_get(self, oa_iv_playing);

  alGetSourcei(source, AL_SOURCE_STATE, &state);
  OA_CHECK_ERRORS("AL_SOURCE_STATE");

  if (RTEST(playing) && state != AL_PLAYING)
  {
    alSourcePlay(source);
    OA_CHECK_ERRORS("alSourcePlay (forced continue)");
  }
}

static inline void oa_puts(const char *message)
{
  VALUE rbmessage = rb_str_new2(message);
  rb_funcall(rb_cObject, oa_id_puts, 1, rbmessage);
}

static VALUE oa_format_get(VALUE self);

static inline int _oa_format_channels(VALUE self)
{
  VALUE channels = rb_hash_aref(oa_format_get(self), oa_key_channels);
  return FIX2INT(channels);
}

static inline int _oa_format_rate(VALUE self)
{
  VALUE rate = rb_hash_aref(oa_format_get(self), oa_key_rate);
  return FIX2LONG(rate);
}

static inline VALUE _oa_format_type(VALUE self)
{
  VALUE size = rb_hash_aref(oa_format_get(self), oa_key_type);
  return size;
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

static VALUE oa_initialize(VALUE self)
{
  oa_struct_t *data_ptr;
  ALenum error = AL_NO_ERROR;

  // initialize openal
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
}

static VALUE oa_play(VALUE self)
{
  rb_ivar_set(self, oa_iv_playing, Qtrue);
  alSourcePlay(oa_struct(self)->source);
  return self;
}

static VALUE oa_stop(VALUE self)
{
  ALuint source = oa_struct(self)->source;
  rb_ivar_set(self, oa_iv_playing, Qfalse);

  // remove all buffers from the source
  alSourcei(source, AL_BUFFER, 0);
  OA_CHECK_ERRORS("remove buffers!");

  // and stop the source
  alSourceStop(source);
  OA_CHECK_ERRORS("stop!");

  return self;
}

static VALUE oa_pause(VALUE self)
{
  rb_ivar_set(self, oa_iv_playing, Qfalse);
  alSourcePause(oa_struct(self)->source);
  OA_CHECK_ERRORS("pause!");
  return self;
}

static VALUE oa_drops(VALUE self)
{
  return INT2NUM(0);
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
    int processed;
    struct timeval poll_time;
    poll_time.tv_sec  = 0;
    poll_time.tv_usec = 100;	/* 0.000100 sec */

    for (processed = 0; processed == 0; rb_thread_wait_for(poll_time))
    {
      alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
      OA_CHECK_ERRORS("AL_BUFFERS_PROCESSED");
    }

    alSourceUnqueueBuffers(source, 1, &empty_buffer);
    OA_CHECK_ERRORS("alSourceUnqueueBuffers");
  }

  return empty_buffer;
}

static VALUE oa_format_get(VALUE self)
{
  return rb_ivar_get(self, oa_iv_format);
}

static VALUE oa_format_set(VALUE self, VALUE format)
{
  rb_ivar_set(self, oa_iv_format, format);
  return format;
}

static VALUE oa_stream(VALUE self)
{
  VALUE int16ne = STR2SYM("int16");
  ALuint source = oa_struct(self)->source;
  signed short *sample_ary = NULL;

  for (;;)
  {
    // make sure we have no preattached buffers
    alSourcei(source, AL_BUFFER, 0);
    OA_CHECK_ERRORS("detach all buffers from the source");

    // make sure we’re not playing audio
    alSourceStop(source);
    OA_CHECK_ERRORS("reset driver");

    // read the format (it never changes in the inner loop)
    int f_channels = _oa_format_channels(self);
    int f_rate     = _oa_format_rate(self);
    VALUE f_type   = _oa_format_type(self);
    size_t f_size  = 0;

    DEBUG("%d channels %d rate", f_channels, f_rate);

    // each time we buffer down there, it’ll be for about .5s of audio each time
    int sample_ary_frames = f_rate / 2;
    int sample_ary_length = sample_ary_frames * f_channels; // integer division
    xfree(sample_ary); // ruby handles NULL pointer too

    if (rb_eql(f_type, int16ne))
    {
      sample_ary = ALLOCA_N(short, sample_ary_length);
      f_size     = sizeof(short);
    }
    else
    {
      printf("Hallon::OpenAL#stream -> cannot handle format size %s!", SYM2STR(f_type));
      rb_notimplement();
    }

    for (;;)
    {
      // pull some audio out of hallon
      VALUE frames = rb_yield(INT2FIX(sample_ary_frames));

      // if we received nil, it means format changed; the new
      // format is already in @format, so we reinitialize!
      if ( ! RTEST(frames))
      {
        break;
      }

      ALuint buffer = find_empty_buffer(self);
      OA_CHECK_ERRORS("find_empty_buffer");

      // convert the frames from ruby to C
      int num_current_samples = ((int) RARRAY_LEN(frames)) * f_channels;

      VALUE frame, sample;
      int i, rb_i, rb_j;
      for (i = 0; i < num_current_samples; ++i)
      {
        rb_i = i / f_channels; // integer division
        rb_j = i % f_channels;

        frame  = RARRAY_PTR(frames)[rb_i];
        sample = RARRAY_PTR(frame)[rb_j];

        long value = FIX2LONG(sample);

        sample_ary[i] = (short) value;
      }

      DEBUG("%d +%d\n", buffer, num_current_samples);

      // pucker up all the params
      ALenum type  = f_channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
      ALsizei size = f_size * num_current_samples;
      ALsizei freq = f_rate;

      // queue the data!
      alBufferData(buffer, type, sample_ary, size, freq);
      OA_CHECK_ERRORS("buffer data");

      alSourceQueueBuffers(source, 1, &buffer);
      OA_CHECK_ERRORS("queue a buffer");

      // OpenAL transitions to :stopped state if we cannot
      // keep buffers properly feeded — but we want it to
      // play as soon as it can if we’re playing, so fix it
      oa_ensure_playing(self);
    }
  }

  return Qtrue;
}

void Init_openal_ext(void)
{
  VALUE mHallon = rb_const_get(rb_cObject, rb_intern("Hallon"));
  VALUE cOpenAL = rb_define_class_under(mHallon, "OpenAL", rb_cObject);

  oa_iv_playing = rb_intern("@playing");
  oa_id_call    = rb_intern("call");
  oa_id_puts    = rb_intern("puts");
  oa_iv_format  = rb_intern("format");
  oa_key_channels = STR2SYM("channels");
  oa_key_rate     = STR2SYM("rate");
  oa_key_type     = STR2SYM("type");

  rb_define_alloc_func(cOpenAL, oa_allocate);

  rb_define_method(cOpenAL, "initialize", oa_initialize, 0);
  rb_define_method(cOpenAL, "play", oa_play, 0);
  rb_define_method(cOpenAL, "stop", oa_stop, 0);
  rb_define_method(cOpenAL, "pause", oa_pause, 0);
  rb_define_method(cOpenAL, "stream", oa_stream, 0);
  rb_define_method(cOpenAL, "format=", oa_format_set, 1);
  rb_define_method(cOpenAL, "format", oa_format_get, 0);

  rb_define_method(cOpenAL, "drops", oa_drops, 0);
}
