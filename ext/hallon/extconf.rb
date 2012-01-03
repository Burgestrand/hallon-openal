require 'mkmf'

def error(message)
  abort "[ERROR] #{message}"
end

error 'Missing ruby header' unless have_header 'ruby.h'
error 'Missing OpenAL/alc.h' unless have_header 'OpenAL/alc.h'
error 'Missing OpenAL/al.h' unless have_header 'OpenAL/al.h'

with_ldflags('-framework OpenAL') { RUBY_PLATFORM =~ /darwin/ }

create_makefile('openal_ext')
