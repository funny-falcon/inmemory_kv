# coding: utf-8
lib = File.expand_path('../lib', __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)
require 'inmemory_kv/version'

Gem::Specification.new do |spec|
  spec.name          = "inmemory_kv"
  spec.version       = InMemoryKV::VERSION
  spec.authors       = ["Sokolov Yura aka funny_falcon"]
  spec.email         = ["funny.falcon@gmail.com"]
  spec.summary       = %q{Simple in memory string/string hash}
  spec.description   = %q{Simple in memory string/string hash}
  spec.homepage      = ""
  spec.license       = "MIT"

  spec.files         = `git ls-files -z`.split("\x0")
  spec.executables   = spec.files.grep(%r{^bin/}) { |f| File.basename(f) }
  spec.test_files    = spec.files.grep(%r{^(test|spec|features)/})
  spec.extensions    = ["ext/extconf.rb"]
  spec.require_paths = ["lib", "ext"]

  spec.add_development_dependency "bundler", "~> 1.7"
  spec.add_development_dependency "rake", "~> 10.0"
  spec.add_development_dependency "minitest"
end
