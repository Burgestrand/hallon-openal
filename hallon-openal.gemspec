# -*- encoding: utf-8 -*-
require File.expand_path('../lib/hallon/openal/version', __FILE__)

Gem::Specification.new do |gem|
  gem.name          = "hallon-openal"

  gem.authors       = ["Kim Burgestrand"]
  gem.email         = ["kim@burgestrand.se"]
  gem.summary       = %q{OpenAL audio drivers for Hallon: http://rubygems.org/gems/hallon}

  gem.files         = `git ls-files`.split("\n")
  gem.test_files    = `git ls-files -- {test,spec,features}/*`.split("\n")
  gem.require_paths = ["lib", "ext/hallon"]
  gem.version       = Hallon::OpenAL::VERSION

  gem.add_dependency 'hallon', '~> 0.12'
  gem.add_development_dependency 'rspec', '~> 2.7'
end
