# InMemoryKV

This is simple off-gc in-memory hash table.

Currently, `InMemoryKV::Str2Str` is a string to string hash table
with LRU built in (a lot like builtin Hash).

It doesn't participate in GC and not encounted in. It uses `malloc` for simplicity.

If you do not clone and not mutate it in a fork, than it is as fork-friendly as your malloc is.

It is not thread-safe, so protect it by you self. (builtin hash is also not thread-safe)

## Installation

Add this line to your application's Gemfile:

```ruby
gem 'inmemory_kv'
```

And then execute:

    $ bundle

Or install it yourself as:

    $ gem install inmemory_kv

## Usage

```ruby
s2s = InMemoryKV::Str2Str.new
s2s['asdf'] = 'qwer'
s2s.size
s2s.count
s2s.empty?
s2s.keys
s2s.values
s2s.entries
# attention: iteration is not as safe as iteration of builtin Hash.
# Builtin Hash checks if you mutate hash during iteratation, this hash doesn't.
s2s.each_key{|k| }
s2s.each_value{|k| }
s2s.each{|k,v| }

s2s.up(k) # touch entry to be most recent in LRU order
s2s.down(k) # touch entry to be first to expire
s2s.first # first/oldest entry in LRU
s2s.shift # shift oldest entry
s2s.data_size # size of key+value entries
s2s.total_size # size of key+value entries + internal structures
s2s.clear

# Str2Str is more memory efficient than storing string in a builtin hash
# also it is a bit faster.
# It tries to overwrite value inplace if it value's size not larger.
def timeit; t=Time.now; r=yield; ensure puts "Lasts: #{Time.now - t}"; r; end

timeit{ 1000000.times{|i| s2s[i.to_s] = "qwer#{i}"} }
timeit{ 1000000.times{|i| s2s[i.to_s] = "qwer#{i}"} }
timeit{ GC.start }
timeit{ GC.start }

# Compare with classic hash
hsh = {}
timeit{ 1000000.times{|i| hsh[i.to_s] = "qwer#{i}"} }
timeit{ 1000000.times{|i| hsh[i.to_s] = "qwer#{i}"} }
timeit{ GC.start }
timeit{ GC.start }

# cloning is made to be very fast:
# it does not copy key/value entries
# only internal structures are alloced and copied with memcpy
# key/value's reference count is incremented
timeit{ sts.dup }
timeit{ hsh.dup }
# clone is copy on write
cpy = sts.dup
sts['2'] = '!'
cpy['3'] = '!!'
sts['2'] != cpy['2']
sts['3'] != cpy['3']
```

## Contributing

1. Fork it ( https://github.com/funny-falcon/inmemory_kv/fork )
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Add some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create a new Pull Request
